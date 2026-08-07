#import "s3g_tracker_mixer_view.h"
#import "s3g_tracker_controls.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace {

using s3g::tracker::EventDestination;
using s3g::tracker::InstrumentCellState;
using s3g::tracker::NoteCellState;
using s3g::tracker::Track;
using s3g::tracker::app::TrackerViewState;

constexpr CGFloat kOuter = 14.0;
constexpr CGFloat kHeaderHeight = 54.0;
constexpr CGFloat kStripWidth = 108.0;
constexpr CGFloat kStripGap = 8.0;
constexpr CGFloat kMainGap = 18.0;
constexpr CGFloat kMainWidth = 136.0;
constexpr float kMinimumVelocityScale = 1.0f / 127.0f;
constexpr std::array<uint32_t, 8u> kLaneColors {
    0x78918c, 0x9a826c, 0x817a99, 0x956f73,
    0x71889a, 0x87916f, 0x987b6d, 0x748c7b,
};

NSColor* color(uint32_t rgb, CGFloat alpha = 1.0)
{
    return S3GTrackerColor(rgb, alpha);
}

NSFont* font(CGFloat size, NSFontWeight weight = NSFontWeightRegular)
{
    return S3GTrackerFont(size, weight);
}

void fill(NSRect rect, NSColor* value)
{
    [value setFill];
    NSRectFill(rect);
}

void stroke(NSRect rect, NSColor* value, CGFloat width = 1.0)
{
    [value setStroke];
    NSBezierPath* path = [NSBezierPath bezierPathWithRect:
        NSInsetRect(rect, width * 0.5, width * 0.5)];
    path.lineWidth = width;
    [path stroke];
}

void text(NSString* value, NSRect rect, NSColor* foreground, CGFloat size,
    NSFontWeight weight = NSFontWeightRegular,
    NSTextAlignment alignment = NSTextAlignmentLeft)
{
    NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc] init];
    paragraph.alignment = alignment;
    paragraph.lineBreakMode = NSLineBreakByTruncatingTail;
    [value drawInRect:rect withAttributes:@{
        NSFontAttributeName: font(size, weight),
        NSForegroundColorAttributeName: foreground,
        NSParagraphStyleAttributeName: paragraph,
    }];
}

NSString* string(const std::string& value)
{
    NSString* result = [NSString stringWithUTF8String:value.c_str()];
    return result ? result : @"";
}

NSString* routeName(EventDestination destination)
{
    switch (destination) {
    case EventDestination::Internal: return @"INT";
    case EventDestination::Both: return @"BTH";
    case EventDestination::None: return @"OFF";
    case EventDestination::Midi:
    default: return @"MIDI";
    }
}

NSString* instrumentName(uint32_t node)
{
    const auto* instrument = s3g::tracker::defaultRackInstrument(node);
    if (!instrument) return @"---";
    return [NSString stringWithUTF8String:
        std::string(instrument->mnemonic).c_str()];
}

NSString* instrumentRouteName(uint32_t node)
{
    const auto destination = s3g::tracker::destinationForInstrument(
        node, EventDestination::None);
    return routeName(destination);
}

bool sequencesInstruments(const Track& track)
{
    return std::any_of(track.instruments.begin(), track.instruments.end(),
        [](const auto& cell) {
            return cell.state == InstrumentCellState::Instrument;
        });
}

bool laneSoloed(const TrackerViewState* state, std::size_t lane)
{
    return state && state->mixerSoloActive
        && state->mixerSoloTrack == lane;
}

bool currentStepHit(const TrackerViewState* state, std::size_t lane)
{
    if (!state || !state->playing || lane >= state->session.pattern.tracks.size()
        || lane >= state->notePlayheads.size()) return false;
    const auto& track = state->session.pattern.tracks[lane];
    if (track.noteColumn.muted || track.notes.empty()) return false;
    const auto row = state->notePlayheads[lane] % track.notes.size();
    const auto kind = track.notes[row].state;
    return kind == NoteCellState::Note
        || kind == NoteCellState::RetriggerPrevious;
}

CGFloat laneX(std::size_t lane)
{
    return kOuter + static_cast<CGFloat>(lane) * (kStripWidth + kStripGap);
}

NSRect laneRect(std::size_t lane, CGFloat height)
{
    return NSMakeRect(laneX(lane), kHeaderHeight, kStripWidth,
        std::max<CGFloat>(1.0, height - kHeaderHeight - kOuter));
}

CGFloat mainX(std::size_t lanes)
{
    return laneX(lanes) + kMainGap;
}

NSRect mainRect(std::size_t lanes, CGFloat height)
{
    return NSMakeRect(mainX(lanes), kHeaderHeight, kMainWidth,
        std::max<CGFloat>(1.0, height - kHeaderHeight - kOuter));
}

NSRect faderRect(NSRect strip)
{
    return NSMakeRect(NSMidX(strip) - 13.0, NSMinY(strip) + 98.0, 26.0,
        std::max<CGFloat>(100.0, NSHeight(strip) - 260.0));
}

NSRect muteRect(NSRect strip)
{
    return NSMakeRect(NSMinX(strip) + 9.0, NSMaxY(strip) - 86.0, 40.0, 24.0);
}

NSRect soloRect(NSRect strip)
{
    return NSMakeRect(NSMinX(strip) + 58.0, NSMaxY(strip) - 86.0, 40.0, 24.0);
}

NSRect routeRect(NSRect strip)
{
    return NSMakeRect(NSMinX(strip) + 9.0, NSMaxY(strip) - 52.0,
        NSWidth(strip) - 18.0, 25.0);
}

float valueAtY(NSRect fader, CGFloat y)
{
    return static_cast<float>(std::clamp(
        (NSMaxY(fader) - y) / NSHeight(fader), 0.0, 1.0));
}

void drawButton(NSRect rect, NSString* label, bool active, NSColor* accent)
{
    fill(rect, active ? [accent colorWithAlphaComponent:0.24]
                      : S3GTrackerThemeColor(S3GTrackerThemeRole::Control));
    stroke(rect, active ? accent
                        : S3GTrackerThemeColor(S3GTrackerThemeRole::Border),
        active ? 1.5 : 1.0);
    text(label, NSInsetRect(rect, 3.0, 5.0), active ? accent
            : S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted),
        8.5, active ? NSFontWeightBold : NSFontWeightMedium,
        NSTextAlignmentCenter);
}

} // namespace

typedef NS_ENUM(NSInteger, S3GTrackerMixerAXKind) {
    S3GTrackerMixerAXTrackFader,
    S3GTrackerMixerAXTrackMute,
    S3GTrackerMixerAXTrackSolo,
    S3GTrackerMixerAXTrackRoute,
    S3GTrackerMixerAXMainFader,
    S3GTrackerMixerAXMainMute,
};

@class S3GTrackerMixerView;

@interface S3GTrackerMixerAXElement : NSAccessibilityElement
@property(nonatomic, weak) S3GTrackerMixerView* mixer;
@property(nonatomic, assign) S3GTrackerMixerAXKind kind;
@property(nonatomic, assign) std::size_t lane;
@end

@interface S3GTrackerMixerView ()
@property(nonatomic, assign) NSInteger dragKind;
@property(nonatomic, assign) std::size_t dragLane;
@property(nonatomic, strong)
    NSArray<S3GTrackerMixerAXElement*>* mixerAccessibilityChildren;
- (std::size_t)laneCount;
- (void)setSelectedStrip:(std::size_t)strip;
- (void)adjustSelected:(float)delta;
- (void)toggleMute:(std::size_t)selected;
- (void)toggleSolo:(std::size_t)selected;
- (void)restorePreSoloMutes;
- (void)cycleRoute:(std::size_t)selected;
- (void)refreshAccessibilityElements;
@end

@implementation S3GTrackerMixerAXElement

- (BOOL)accessibilityPerformPress
{
    S3GTrackerMixerView* mixer = self.mixer;
    if (!mixer) return NO;
    [mixer setSelectedStrip:self.lane];
    switch (self.kind) {
    case S3GTrackerMixerAXTrackMute:
    case S3GTrackerMixerAXMainMute:
        [mixer toggleMute:self.lane];
        break;
    case S3GTrackerMixerAXTrackSolo:
        [mixer toggleSolo:self.lane];
        break;
    case S3GTrackerMixerAXTrackRoute:
        [mixer cycleRoute:self.lane];
        break;
    case S3GTrackerMixerAXTrackFader:
    case S3GTrackerMixerAXMainFader:
        return NO;
    }
    NSAccessibilityPostNotification(
        self, NSAccessibilityValueChangedNotification);
    return YES;
}

- (BOOL)accessibilityPerformIncrement
{
    if (self.kind != S3GTrackerMixerAXTrackFader
        && self.kind != S3GTrackerMixerAXMainFader) return NO;
    S3GTrackerMixerView* mixer = self.mixer;
    if (!mixer) return NO;
    [mixer setSelectedStrip:self.lane];
    [mixer adjustSelected:0.02f];
    NSAccessibilityPostNotification(
        self, NSAccessibilityValueChangedNotification);
    return YES;
}

- (BOOL)accessibilityPerformDecrement
{
    if (self.kind != S3GTrackerMixerAXTrackFader
        && self.kind != S3GTrackerMixerAXMainFader) return NO;
    S3GTrackerMixerView* mixer = self.mixer;
    if (!mixer) return NO;
    [mixer setSelectedStrip:self.lane];
    [mixer adjustSelected:-0.02f];
    NSAccessibilityPostNotification(
        self, NSAccessibilityValueChangedNotification);
    return YES;
}

@end

@implementation S3GTrackerMixerView

- (instancetype)initWithState:(TrackerViewState*)state
{
    self = [super initWithFrame:NSZeroRect];
    if (self) {
        self.trackerState = state;
        self.dragKind = 0;
        self.accessibilityElement = YES;
        self.accessibilityRole = NSAccessibilityGroupRole;
        self.accessibilityLabel = @"Performance mixer";
        self.accessibilityHelp = @"Left and Right select a track or MAIN OUT. Up and Down adjust velocity trim or main audio gain. M toggles mute, S solos a track, R changes its rack instrument, and Space toggles playback.";
        [self reloadModel];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [self refreshAccessibilityElements];
}

- (std::size_t)laneCount
{
    return self.trackerState
        ? std::min<std::size_t>(s3g::tracker::kMaximumTrackCount,
              self.trackerState->session.pattern.tracks.size())
        : 0u;
}

- (CGFloat)preferredContentWidth
{
    const auto lanes = [self laneCount];
    return mainX(lanes) + kMainWidth + kOuter;
}

- (std::size_t)selectedStrip
{
    const auto lanes = [self laneCount];
    if (!self.trackerState || lanes == 0u) return lanes;
    return std::min(self.trackerState->mixerSelectedStrip, lanes);
}

- (void)setSelectedStrip:(std::size_t)strip
{
    auto* state = self.trackerState;
    const auto lanes = [self laneCount];
    if (!state) return;
    state->mixerSelectedStrip = std::min(strip, lanes);
    if (state->mixerSelectedStrip < lanes) {
        state->session.selectedTrack = state->mixerSelectedStrip;
        if (self.selectionChangeHandler) self.selectionChangeHandler();
    }
    [self reloadModel];
    NSAccessibilityPostNotification(
        self, NSAccessibilityValueChangedNotification);
}

- (void)refreshAccessibilityValue
{
    auto* state = self.trackerState;
    const auto lanes = [self laneCount];
    if (!state || lanes == 0u) {
        self.accessibilityValue = @"MAIN OUT";
        return;
    }
    const auto selected = [self selectedStrip];
    if (selected == lanes) {
        self.accessibilityValue = [NSString stringWithFormat:
            @"MAIN OUT, gain %.0f percent, %@, peak %.0f percent",
            state->mainOutputGain * 100.0f,
            state->mainOutputMuted ? @"muted" : @"audible",
            state->audioPeak * 100.0f];
        return;
    }
    const auto& track = state->session.pattern.tracks[selected];
    self.accessibilityValue = [NSString stringWithFormat:
        @"Track %lu, %@, velocity trim %.0f percent, instrument %@, %@",
        static_cast<unsigned long>(selected + 1u), string(track.name),
        track.velocityScale * 100.0f,
        instrumentName(track.initialInstrumentNodeId),
        track.noteColumn.muted ? @"NOTE muted" : @"NOTE active"];
}

- (NSRect)screenFrameForLocalRect:(NSRect)local
{
    if (!self.window) return NSZeroRect;
    return [self.window convertRectToScreen:
        [self convertRect:local toView:nil]];
}

- (void)refreshAccessibilityElements
{
    auto* state = self.trackerState;
    const auto lanes = [self laneCount];
    if (!state) {
        self.mixerAccessibilityChildren = @[];
        self.accessibilityChildren = @[];
        self.accessibilityChildrenInNavigationOrder = @[];
        return;
    }

    const NSUInteger expected = static_cast<NSUInteger>(lanes * 4u + 2u);
    if (self.mixerAccessibilityChildren.count != expected) {
        NSMutableArray<S3GTrackerMixerAXElement*>* created =
            [NSMutableArray arrayWithCapacity:expected];
        const auto append = [&](S3GTrackerMixerAXKind kind,
                                std::size_t lane) {
            S3GTrackerMixerAXElement* element =
                [[S3GTrackerMixerAXElement alloc] init];
            element.mixer = self;
            element.kind = kind;
            element.lane = lane;
            element.accessibilityParent = self;
            element.accessibilityEnabled = YES;
            [created addObject:element];
        };
        for (std::size_t lane = 0u; lane < lanes; ++lane) {
            append(S3GTrackerMixerAXTrackFader, lane);
            append(S3GTrackerMixerAXTrackMute, lane);
            append(S3GTrackerMixerAXTrackSolo, lane);
            append(S3GTrackerMixerAXTrackRoute, lane);
        }
        append(S3GTrackerMixerAXMainFader, lanes);
        append(S3GTrackerMixerAXMainMute, lanes);
        self.mixerAccessibilityChildren = created;
        self.accessibilityChildren = created;
        self.accessibilityChildrenInNavigationOrder =
            (NSArray<id<NSAccessibilityElement>>*)created;
    }

    NSUInteger index = 0u;
    for (std::size_t lane = 0u; lane < lanes; ++lane) {
        const auto& track = state->session.pattern.tracks[lane];
        const NSRect strip = laneRect(lane, NSHeight(self.bounds));
        NSString* prefix = [NSString stringWithFormat:@"Track %lu %@",
            static_cast<unsigned long>(lane + 1u), string(track.name)];

        S3GTrackerMixerAXElement* fader =
            self.mixerAccessibilityChildren[index++];
        fader.accessibilityRole = NSAccessibilitySliderRole;
        fader.accessibilityLabel = [prefix
            stringByAppendingString:@" velocity input trim"];
        fader.accessibilityHelp =
            @"Adjusts future Note On velocity for MIDI and internal audio; it is not a post-DSP audio fader.";
        fader.accessibilityValue = @(track.velocityScale * 100.0f);
        fader.accessibilityMinValue = @(kMinimumVelocityScale * 100.0f);
        fader.accessibilityMaxValue = @100.0f;
        fader.accessibilityOrientation = NSAccessibilityOrientationVertical;
        fader.accessibilityFrame = [self screenFrameForLocalRect:
            faderRect(strip)];

        S3GTrackerMixerAXElement* mute =
            self.mixerAccessibilityChildren[index++];
        mute.accessibilityRole = NSAccessibilityCheckBoxRole;
        mute.accessibilityLabel = [prefix
            stringByAppendingString:@" NOTE mute"];
        mute.accessibilityHelp =
            @"Mutes NOTE onsets while the other polymetric fields continue.";
        mute.accessibilityValue = @(track.noteColumn.muted);
        mute.accessibilityFrame = [self screenFrameForLocalRect:
            muteRect(strip)];

        S3GTrackerMixerAXElement* solo =
            self.mixerAccessibilityChildren[index++];
        solo.accessibilityRole = NSAccessibilityCheckBoxRole;
        solo.accessibilityLabel = [prefix
            stringByAppendingString:@" NOTE solo"];
        solo.accessibilityHelp =
            @"Temporarily NOTE-mutes every other lane and preserves prior NOTE mutes.";
        solo.accessibilityValue = @(laneSoloed(state, lane));
        solo.accessibilityFrame = [self screenFrameForLocalRect:
            soloRect(strip)];

        S3GTrackerMixerAXElement* route =
            self.mixerAccessibilityChildren[index++];
        route.accessibilityRole = NSAccessibilityButtonRole;
        route.accessibilityLabel = [prefix
            stringByAppendingString:@" default rack instrument"];
        route.accessibilityHelp =
            @"Cycles the lane default through the indexed rack. MIDI OUT is a rack instrument.";
        route.accessibilityValue = instrumentName(
            track.initialInstrumentNodeId);
        route.accessibilityFrame = [self screenFrameForLocalRect:
            routeRect(strip)];
    }

    const NSRect main = mainRect(lanes, NSHeight(self.bounds));
    S3GTrackerMixerAXElement* mainFader =
        self.mixerAccessibilityChildren[index++];
    mainFader.accessibilityRole = NSAccessibilitySliderRole;
    mainFader.accessibilityLabel = @"MAIN OUT post-decode audio gain";
    mainFader.accessibilityHelp =
        @"Adjusts internal audio after stereo or quad decode. MIDI bypasses MAIN OUT.";
    mainFader.accessibilityValue = @(state->mainOutputGain * 100.0f);
    mainFader.accessibilityMinValue = @0.0f;
    mainFader.accessibilityMaxValue = @100.0f;
    mainFader.accessibilityOrientation = NSAccessibilityOrientationVertical;
    mainFader.accessibilityFrame = [self screenFrameForLocalRect:
        faderRect(main)];

    S3GTrackerMixerAXElement* mainMute =
        self.mixerAccessibilityChildren[index];
    mainMute.accessibilityRole = NSAccessibilityCheckBoxRole;
    mainMute.accessibilityLabel = @"MAIN OUT internal audio mute";
    mainMute.accessibilityHelp =
        @"Mutes post-decode internal audio. MIDI bypasses MAIN OUT.";
    mainMute.accessibilityValue = @(state->mainOutputMuted);
    mainMute.accessibilityFrame = [self screenFrameForLocalRect:
        muteRect(main)];
}

- (void)reloadModel
{
    auto* state = self.trackerState;
    if (state && state->mixerSoloActive) {
        const auto& tracks = state->session.pattern.tracks;
        bool valid = state->mixerSoloTrack < tracks.size()
            && state->mixerSoloRestoreMutes.size() == tracks.size();
        for (std::size_t lane = 0u; valid && lane < tracks.size(); ++lane) {
            if (tracks[lane].noteColumn.muted
                != (lane != state->mixerSoloTrack)) valid = false;
        }
        if (!valid) {
            state->mixerSoloActive = false;
            state->mixerSoloTrack =
                s3g::tracker::app::kVisibleLaneCount;
            state->mixerSoloRestoreMutes.clear();
        }
    }
    [self refreshAccessibilityValue];
    [self refreshAccessibilityElements];
    [self setNeedsDisplay:YES];
}

- (void)publishPattern
{
    if (self.patternChangeHandler) self.patternChangeHandler();
    [self reloadModel];
    NSAccessibilityPostNotification(
        self, NSAccessibilityValueChangedNotification);
}

- (void)publishMainGain
{
    auto* state = self.trackerState;
    if (!state) return;
    const float audible = state->mainOutputMuted ? 0.0f
                                                 : state->mainOutputGain;
    if (self.masterGainChangeHandler)
        self.masterGainChangeHandler(audible);
    [self reloadModel];
    NSAccessibilityPostNotification(
        self, NSAccessibilityValueChangedNotification);
}

- (void)adjustSelected:(float)delta
{
    auto* state = self.trackerState;
    if (!state) return;
    const auto lanes = [self laneCount];
    const auto selected = [self selectedStrip];
    if (selected == lanes) {
        state->mainOutputGain = std::clamp(
            state->mainOutputGain + delta, 0.0f, 1.0f);
        [self publishMainGain];
    } else {
        auto& track = state->session.pattern.tracks[selected];
        track.velocityScale = std::clamp(
            track.velocityScale + delta, kMinimumVelocityScale, 1.0f);
        [self publishPattern];
    }
}

- (void)toggleMute:(std::size_t)selected
{
    auto* state = self.trackerState;
    const auto lanes = [self laneCount];
    if (!state) return;
    if (selected == lanes) {
        state->mainOutputMuted = !state->mainOutputMuted;
        [self publishMainGain];
    } else if (selected < lanes) {
        [self restorePreSoloMutes];
        auto& muted = state->session.pattern.tracks[selected].noteColumn.muted;
        muted = !muted;
        [self publishPattern];
    }
}

- (void)restorePreSoloMutes
{
    auto* state = self.trackerState;
    if (!state || !state->mixerSoloActive) return;
    auto& tracks = state->session.pattern.tracks;
    const auto restoreCount = std::min(
        tracks.size(), state->mixerSoloRestoreMutes.size());
    for (std::size_t lane = 0u; lane < restoreCount; ++lane)
        tracks[lane].noteColumn.muted =
            state->mixerSoloRestoreMutes[lane];
    state->mixerSoloActive = false;
    state->mixerSoloTrack = s3g::tracker::app::kVisibleLaneCount;
    state->mixerSoloRestoreMutes.clear();
}

- (void)toggleSolo:(std::size_t)selected
{
    auto* state = self.trackerState;
    const auto lanes = [self laneCount];
    if (!state || selected >= lanes) return;
    auto& tracks = state->session.pattern.tracks;
    if (laneSoloed(state, selected)) {
        [self restorePreSoloMutes];
        [self publishPattern];
        return;
    }
    if (!state->mixerSoloActive) {
        state->mixerSoloRestoreMutes.clear();
        state->mixerSoloRestoreMutes.reserve(tracks.size());
        for (const auto& track : tracks)
            state->mixerSoloRestoreMutes.push_back(
                track.noteColumn.muted);
    }
    state->mixerSoloActive = true;
    state->mixerSoloTrack = selected;
    for (std::size_t lane = 0u; lane < tracks.size(); ++lane)
        tracks[lane].noteColumn.muted = lane != selected;
    [self publishPattern];
}

- (void)cycleRoute:(std::size_t)selected
{
    auto* state = self.trackerState;
    const auto lanes = [self laneCount];
    if (!state || selected >= lanes) return;
    auto& track = state->session.pattern.tracks[selected];
    const auto next = s3g::tracker::cycleActiveInstrument(
        state->instrumentRack, track.initialInstrumentNodeId, 1);
    if (next == s3g::tracker::kInvalidInstrumentNode) return;
    track.initialInstrumentNodeId = next;
    track.destination = s3g::tracker::destinationForInstrument(
        track.initialInstrumentNodeId, EventDestination::None);
    [self publishPattern];
}

- (void)updateDragAt:(NSPoint)point
{
    auto* state = self.trackerState;
    const auto lanes = [self laneCount];
    if (!state || self.dragKind == 0) return;
    if (self.dragKind == 2) {
        state->mainOutputGain = valueAtY(faderRect(
            mainRect(lanes, NSHeight(self.bounds))), point.y);
        [self publishMainGain];
    } else if (self.dragLane < lanes) {
        auto& track = state->session.pattern.tracks[self.dragLane];
        track.velocityScale = std::max(kMinimumVelocityScale,
            valueAtY(faderRect(laneRect(self.dragLane,
                NSHeight(self.bounds))), point.y));
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseDown:(NSEvent*)event
{
    auto* state = self.trackerState;
    if (!state) return;
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    const auto lanes = [self laneCount];
    for (std::size_t lane = 0u; lane < lanes; ++lane) {
        const NSRect strip = laneRect(lane, NSHeight(self.bounds));
        if (!NSPointInRect(point, strip)) continue;
        [self setSelectedStrip:lane];
        [self.window makeFirstResponder:self];
        if (NSPointInRect(point, faderRect(strip))) {
            self.dragKind = 1;
            self.dragLane = lane;
            [self updateDragAt:point];
        } else if (NSPointInRect(point, muteRect(strip))) {
            [self toggleMute:lane];
        } else if (NSPointInRect(point, soloRect(strip))) {
            [self toggleSolo:lane];
        } else if (NSPointInRect(point, routeRect(strip))) {
            [self cycleRoute:lane];
        }
        return;
    }
    const NSRect main = mainRect(lanes, NSHeight(self.bounds));
    if (!NSPointInRect(point, main)) return;
    [self setSelectedStrip:lanes];
    [self.window makeFirstResponder:self];
    if (NSPointInRect(point, faderRect(main))) {
        self.dragKind = 2;
        [self updateDragAt:point];
    } else if (NSPointInRect(point, muteRect(main))) {
        [self toggleMute:lanes];
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    if (self.dragKind == 0) return;
    [self updateDragAt:[self convertPoint:event.locationInWindow fromView:nil]];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (self.dragKind == 1) [self publishPattern];
    self.dragKind = 0;
}

- (void)keyDown:(NSEvent*)event
{
    auto* state = self.trackerState;
    if (!state) { [super keyDown:event]; return; }
    const auto lanes = [self laneCount];
    auto selected = [self selectedStrip];
    const auto modifiers = event.modifierFlags
        & (NSEventModifierFlagCommand | NSEventModifierFlagControl
            | NSEventModifierFlagOption);
    if (modifiers != 0u) { [super keyDown:event]; return; }
    NSString* key = event.charactersIgnoringModifiers.lowercaseString;
    if ([key isEqualToString:@":"] || [key isEqualToString:@"`"]) {
        if (self.focusConsoleHandler) self.focusConsoleHandler();
        return;
    }
    if (event.keyCode == 123) {
        [self setSelectedStrip:selected == 0u ? 0u : selected - 1u];
        return;
    }
    if (event.keyCode == 124) {
        [self setSelectedStrip:std::min(selected + 1u, lanes)];
        return;
    }
    if (event.keyCode == 48) {
        const bool backwards = (event.modifierFlags
            & NSEventModifierFlagShift) != 0u;
        if (backwards) {
            if (selected > 0u)
                [self setSelectedStrip:selected - 1u];
            else
                [self.window selectPreviousKeyView:self];
        } else if (selected < lanes) {
            [self setSelectedStrip:selected + 1u];
        } else {
            [self.window selectNextKeyView:self];
        }
        return;
    }
    const float step = (event.modifierFlags & NSEventModifierFlagShift)
        ? 0.005f : 0.02f;
    if (event.keyCode == 126) { [self adjustSelected:step]; return; }
    if (event.keyCode == 125) { [self adjustSelected:-step]; return; }
    if ([key isEqualToString:@"m"]) { [self toggleMute:selected]; return; }
    if ([key isEqualToString:@"s"] && selected < lanes) {
        [self toggleSolo:selected]; return;
    }
    if ([key isEqualToString:@"r"] && selected < lanes) {
        [self cycleRoute:selected]; return;
    }
    if ([key isEqualToString:@" "]) {
        if (self.playbackHandler) self.playbackHandler();
        return;
    }
    [super keyDown:event];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    fill(self.bounds, S3GTrackerThemeColor(S3GTrackerThemeRole::Canvas));
    fill(NSMakeRect(0.0, 0.0, NSWidth(self.bounds), kHeaderHeight),
        S3GTrackerThemeColor(S3GTrackerThemeRole::Panel));
    fill(NSMakeRect(0.0, 0.0, NSWidth(self.bounds), 2.0),
        S3GTrackerThemeColor(S3GTrackerThemeRole::Focus));
    text(@"PERFORMANCE MIXER", NSMakeRect(kOuter, 10.0, 230.0, 17.0),
        S3GTrackerThemeColor(S3GTrackerThemeRole::TextSecondary), 11.0,
        NSFontWeightMedium);
    text(@"TRACKS: EVENT-STAGE VEL / INPUT TRIM + NOTE M/S     MAIN OUT: POST-DECODE AUDIO",
        NSMakeRect(kOuter, 31.0, NSWidth(self.bounds) - kOuter * 2.0, 14.0),
        S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted), 7.5,
        NSFontWeightMedium);

    auto* state = self.trackerState;
    const auto lanes = [self laneCount];
    if (!state) return;
    const auto selected = [self selectedStrip];
    for (std::size_t lane = 0u; lane < lanes; ++lane) {
        const auto& track = state->session.pattern.tracks[lane];
        const NSRect strip = laneRect(lane, NSHeight(self.bounds));
        const bool isSelected = selected == lane;
        const bool muted = track.noteColumn.muted;
        const bool soloed = laneSoloed(state, lane);
        NSColor* identityColor = color(
            kLaneColors[lane % kLaneColors.size()],
            muted ? 0.42 : 1.0);
        NSColor* accent = color(muted ? 0x787878
            : isSelected ? 0xd8d8d8 : 0xb8b8b8);
        fill(strip, color(isSelected ? 0x1e2224 : 0x151819));
        fill(NSMakeRect(NSMinX(strip), NSMinY(strip), NSWidth(strip), 4.0),
            identityColor);
        stroke(strip, isSelected
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::Focus)
                : S3GTrackerThemeColor(S3GTrackerThemeRole::Grid),
            isSelected ? 2.0 : 1.0);
        text([NSString stringWithFormat:@"%02lu",
                 static_cast<unsigned long>(lane + 1u)],
            NSMakeRect(NSMinX(strip) + 8.0, NSMinY(strip) + 12.0, 22.0, 13.0),
            color(0x788187), 8.0, NSFontWeightMedium);
        text(string(track.name),
            NSMakeRect(NSMinX(strip) + 8.0, NSMinY(strip) + 29.0,
                NSWidth(strip) - 16.0, 16.0),
            muted ? color(0x71777a) : color(0xd2d7da), 9.0,
            NSFontWeightSemibold);
        text([NSString stringWithFormat:@"%@  CH%u",
                 instrumentRouteName(track.initialInstrumentNodeId),
                 track.midiChannel],
            NSMakeRect(NSMinX(strip) + 8.0, NSMinY(strip) + 50.0,
                NSWidth(strip) - 16.0, 13.0),
            accent, 7.5, NSFontWeightMedium);
        NSString* instrument = sequencesInstruments(track) ? @"INS SEQ"
            : [@"INS " stringByAppendingString:
                  instrumentName(track.initialInstrumentNodeId)];
        text(instrument, NSMakeRect(NSMinX(strip) + 8.0,
                 NSMinY(strip) + 67.0, NSWidth(strip) - 16.0, 12.0),
            color(0xa0a0a0), 7.0, NSFontWeightMedium);

        const NSRect fader = faderRect(strip);
        fill(fader, color(0x0b0d0e));
        stroke(fader, color(0x42484c));
        const CGFloat fillHeight = NSHeight(fader) * track.velocityScale;
        fill(NSMakeRect(NSMinX(fader) + 4.0, NSMaxY(fader) - fillHeight,
                 NSWidth(fader) - 8.0, fillHeight),
            [accent colorWithAlphaComponent:0.62]);
        const CGFloat handleY = NSMaxY(fader)
            - NSHeight(fader) * track.velocityScale;
        fill(NSMakeRect(NSMinX(fader) - 4.0, handleY - 3.0,
                 NSWidth(fader) + 8.0, 6.0),
            isSelected ? S3GTrackerThemeColor(S3GTrackerThemeRole::Focus)
                : S3GTrackerThemeColor(S3GTrackerThemeRole::TextSecondary));
        text(@"VEL / INPUT", NSMakeRect(NSMinX(strip) + 5.0,
                 NSMinY(fader) - 19.0, NSWidth(strip) - 10.0, 12.0),
            color(0x717b80), 6.8, NSFontWeightMedium, NSTextAlignmentCenter);
        text([NSString stringWithFormat:@"%03d",
                 static_cast<int>(std::lround(track.velocityScale * 100.0f))],
            NSMakeRect(NSMinX(strip) + 5.0, NSMaxY(fader) + 9.0,
                NSWidth(strip) - 10.0, 14.0),
            accent, 8.5, NSFontWeightSemibold, NSTextAlignmentCenter);
        const bool active = currentStepHit(state, lane);
        fill(NSMakeRect(NSMinX(strip) + 9.0, NSMaxY(fader) + 31.0,
                 NSWidth(strip) - 18.0, 5.0),
            active ? accent : color(0x252a2d));
        text(@"STEP ACT", NSMakeRect(NSMinX(strip) + 7.0,
                 NSMaxY(fader) + 40.0, NSWidth(strip) - 14.0, 12.0),
            active ? accent : color(0x5e666b), 6.5, NSFontWeightMedium,
            NSTextAlignmentCenter);
        drawButton(muteRect(strip), @"M", muted,
            S3GTrackerThemeColor(S3GTrackerThemeRole::Danger));
        drawButton(soloRect(strip), @"S", soloed,
            S3GTrackerThemeColor(S3GTrackerThemeRole::Warning));
        drawButton(routeRect(strip), instrumentName(
            track.initialInstrumentNodeId), true, accent);
    }

    const NSRect main = mainRect(lanes, NSHeight(self.bounds));
    const bool mainSelected = selected == lanes;
    NSColor* mainAccent = S3GTrackerThemeColor(S3GTrackerThemeRole::Live);
    fill(main, color(mainSelected ? 0x222222 : 0x171717));
    fill(NSMakeRect(NSMinX(main), NSMinY(main), NSWidth(main), 5.0), mainAccent);
    stroke(main, mainSelected
            ? S3GTrackerThemeColor(S3GTrackerThemeRole::Focus)
            : S3GTrackerThemeColor(S3GTrackerThemeRole::Border),
        mainSelected ? 2.0 : 1.0);
    text(@"MAIN OUT", NSMakeRect(NSMinX(main) + 10.0, NSMinY(main) + 15.0,
             NSWidth(main) - 20.0, 18.0),
        color(0xd7e2dc), 10.5, NSFontWeightBold, NSTextAlignmentCenter);
    text(state->audioAvailable ? @"INTERNAL AUDIO" : @"AUDIO OFFLINE",
        NSMakeRect(NSMinX(main) + 8.0, NSMinY(main) + 42.0,
            NSWidth(main) - 16.0, 13.0),
        state->audioAvailable ? mainAccent : color(0x727a7e), 7.0,
        NSFontWeightMedium, NSTextAlignmentCenter);
    text([NSString stringWithFormat:@"%.1f kHz", state->audioSampleRate / 1000.0],
        NSMakeRect(NSMinX(main) + 8.0, NSMinY(main) + 60.0,
            NSWidth(main) - 16.0, 13.0),
        color(0x788187), 7.0, NSFontWeightRegular, NSTextAlignmentCenter);
    const NSRect mainFader = faderRect(main);
    fill(mainFader, color(0x0b0d0e));
    stroke(mainFader, color(0x505050));
    const CGFloat mainFill = NSHeight(mainFader) * state->mainOutputGain;
    fill(NSMakeRect(NSMinX(mainFader) + 4.0,
             NSMaxY(mainFader) - mainFill, NSWidth(mainFader) - 8.0, mainFill),
        [mainAccent colorWithAlphaComponent:0.64]);
    const CGFloat mainHandle = NSMaxY(mainFader)
        - NSHeight(mainFader) * state->mainOutputGain;
    fill(NSMakeRect(NSMinX(mainFader) - 4.0, mainHandle - 3.0,
             NSWidth(mainFader) + 8.0, 6.0),
        mainSelected ? S3GTrackerThemeColor(S3GTrackerThemeRole::Focus)
            : S3GTrackerThemeColor(S3GTrackerThemeRole::TextPrimary));
    text(@"POST-DECODE", NSMakeRect(NSMinX(main) + 5.0,
             NSMinY(mainFader) - 19.0, NSWidth(main) - 10.0, 12.0),
        color(0x708079), 6.8, NSFontWeightMedium, NSTextAlignmentCenter);
    text([NSString stringWithFormat:@"%03d",
             static_cast<int>(std::lround(state->mainOutputGain * 100.0f))],
        NSMakeRect(NSMinX(main) + 5.0, NSMaxY(mainFader) + 9.0,
            NSWidth(main) - 10.0, 14.0),
        mainAccent, 8.5, NSFontWeightSemibold, NSTextAlignmentCenter);
    const CGFloat peak = std::clamp(static_cast<CGFloat>(state->audioPeak),
        0.0, 1.0);
    const NSRect peakRect = NSMakeRect(NSMinX(main) + 17.0,
        NSMaxY(mainFader) + 34.0, NSWidth(main) - 34.0, 8.0);
    fill(peakRect, color(0x292929));
    fill(NSMakeRect(NSMinX(peakRect), NSMinY(peakRect),
             NSWidth(peakRect) * peak, NSHeight(peakRect)),
        peak > 0.9 ? S3GTrackerThemeColor(S3GTrackerThemeRole::Danger)
                   : S3GTrackerThemeColor(S3GTrackerThemeRole::Success));
    text(@"AUDIO PEAK", NSMakeRect(NSMinX(main) + 8.0,
             NSMaxY(peakRect) + 5.0, NSWidth(main) - 16.0, 12.0),
        color(0x718078), 6.8, NSFontWeightMedium, NSTextAlignmentCenter);
    drawButton(muteRect(main), @"M", state->mainOutputMuted,
        S3GTrackerThemeColor(S3GTrackerThemeRole::Danger));
    text(@"MIDI BYPASSES MAIN OUT", NSMakeRect(NSMinX(main) + 8.0,
             NSMaxY(main) - 48.0, NSWidth(main) - 16.0, 12.0),
        color(0x697277), 6.3, NSFontWeightMedium, NSTextAlignmentCenter);
}

@end
