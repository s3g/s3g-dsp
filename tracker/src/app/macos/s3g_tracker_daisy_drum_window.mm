#import "s3g_tracker_daisy_drum_window.h"

#import "s3g_tracker_controls.h"
#import "s3g_tracker_workspace.h"

#include "s3g/tracker/instrument_rack.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace {

using s3g::tracker::DaisyDrumParameterDefinition;
using s3g::tracker::InstrumentKind;
using s3g::tracker::app::TrackerViewState;
using s3g::tracker::app::WorkspaceCallbacks;

constexpr CGFloat kMargin = 20.0;

NSColor* color(uint32_t rgb, CGFloat alpha = 1.0)
{
    return S3GTrackerColor(rgb, alpha);
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
        NSInsetRect(rect, 0.5, 0.5)];
    path.lineWidth = width;
    [path stroke];
}

void text(NSString* value, NSRect rect, NSColor* foreground, CGFloat size,
    NSFontWeight weight = NSFontWeightRegular,
    NSTextAlignment alignment = NSTextAlignmentLeft)
{
    NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc] init];
    paragraph.alignment = alignment;
    paragraph.lineBreakMode = NSLineBreakByClipping;
    [value drawInRect:rect withAttributes:@{
        NSForegroundColorAttributeName: foreground,
        NSFontAttributeName: S3GTrackerFont(size, weight),
        NSParagraphStyleAttributeName: paragraph,
    }];
}

NSString* nsString(std::string_view value)
{
    NSString* result = [[NSString alloc] initWithBytes:value.data()
        length:value.size() encoding:NSUTF8StringEncoding];
    return result ? result : @"";
}

NSString* formatted(InstrumentKind kind,
    const DaisyDrumParameterDefinition& parameter, float normalized)
{
    const double native = s3g::tracker::daisyDrumNativeFromNormalized(
        kind, parameter.parameterId, normalized);
    if (parameter.unit == "Hz")
        return [NSString stringWithFormat:@"%.1f HZ", native];
    if (parameter.unit == "dB")
        return [NSString stringWithFormat:@"%+.1f DB", native];
    return [NSString stringWithFormat:@"%.3f", native];
}

std::size_t activeCount(const TrackerViewState* state,
    InstrumentKind kind) noexcept
{
    return state ? s3g::tracker::activeInstrumentCount(
        state->instrumentRack, kind) : 0u;
}

uint32_t activeNodeAt(const TrackerViewState* state, InstrumentKind kind,
    std::size_t activeIndex) noexcept
{
    if (!state) return s3g::tracker::kInvalidInstrumentNode;
    std::size_t current = 0u;
    for (const auto& instrument : state->instrumentRack.instruments) {
        if (!instrument.active || instrument.kind != kind) continue;
        if (current++ == activeIndex) return instrument.nodeId;
    }
    return s3g::tracker::kInvalidInstrumentNode;
}

std::size_t activeIndexForNode(const TrackerViewState* state,
    InstrumentKind kind, uint32_t nodeId) noexcept
{
    if (!state) return 0u;
    std::size_t current = 0u;
    for (const auto& instrument : state->instrumentRack.instruments) {
        if (!instrument.active || instrument.kind != kind) continue;
        if (instrument.nodeId == nodeId) return current;
        ++current;
    }
    return 0u;
}

uint8_t auditionNote(InstrumentKind kind) noexcept
{
    switch (kind) {
    case InstrumentKind::DaisyAnalogSnareDrum:
    case InstrumentKind::DaisySyntheticSnareDrum: return 38u;
    case InstrumentKind::DaisyHiHat: return 42u;
    default: return 36u;
    }
}

NSString* modelDescription(InstrumentKind kind)
{
    switch (kind) {
    case InstrumentKind::DaisyAnalogBassDrum:
        return @"RESONANT ANALOG MODEL\nATTACK FM  →  SELF FM  →  VCA";
    case InstrumentKind::DaisyAnalogSnareDrum:
        return @"FIVE RESONANT MODES\nSHELL  +  FILTERED NOISE  →  VCA";
    case InstrumentKind::DaisyHiHat:
        return @"SIX SQUARE OSCILLATORS\nMETALLIC CLUSTER  +  NOISE  →  HPF";
    case InstrumentKind::DaisySyntheticBassDrum:
        return @"DISTORTED SINE BODY\nCLICK  +  NOISE  +  FM ENVELOPE";
    case InstrumentKind::DaisySyntheticSnareDrum:
        return @"TWO COUPLED OSCILLATORS\nFM BODY  +  FILTERED SNARE NOISE";
    default:
        return @"DAISYSP DRUM VOICE";
    }
}

} // namespace

@interface S3GTrackerDaisyDrumView : NSView {
@private
    TrackerViewState* _state;
    WorkspaceCallbacks* _callbacks;
    std::array<NSRect, s3g::tracker::kDaisyDrumParameterCapacity> _barRects;
    std::array<NSRect, s3g::tracker::kDaisyDrumRackSlotCount> _tabRects;
    std::array<NSRect, s3g::tracker::kDaisyDrumPresetCount> _presetRects;
    NSInteger _activeParameter;
    NSInteger _focusedParameter;
    NSRect _auditionRect;
    NSRect _resetRect;
}
- (instancetype)initWithState:(TrackerViewState*)state
    callbacks:(WorkspaceCallbacks*)callbacks;
- (void)reloadModel;
@end

@implementation S3GTrackerDaisyDrumView

- (instancetype)initWithState:(TrackerViewState*)state
    callbacks:(WorkspaceCallbacks*)callbacks
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, 800.0, 560.0)];
    if (self) {
        _state = state;
        _callbacks = callbacks;
        _activeParameter = -1;
        _focusedParameter = 0;
        self.wantsLayer = YES;
        self.layer.backgroundColor = color(0x0b0b0b).CGColor;
        self.accessibilityRole = NSAccessibilityGroupRole;
        self.accessibilityLabel = @"DaisySP drum instrument editor";
        self.accessibilityHelp = @"Edit the selected indexed DaisySP drum voice. Double-click a parameter to restore its default; Space auditions the voice.";
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isAccessibilityElement { return YES; }

- (InstrumentKind)selectedKind
{
    if (_state) {
        const auto* instrument = s3g::tracker::rackInstrument(
            _state->instrumentRack, _state->selectedRackInstrument);
        if (instrument && s3g::tracker::isDaisyDrumKind(instrument->kind))
            return instrument->kind;
    }
    return InstrumentKind::DaisyAnalogBassDrum;
}

- (uint32_t)selectedNode
{
    const auto kind = [self selectedKind];
    if (_state && s3g::tracker::isDaisyDrumInstrumentNode(
            _state->selectedRackInstrument)
        && s3g::tracker::daisyDrumKindForNode(
            _state->selectedRackInstrument) == kind) {
        return _state->selectedRackInstrument;
    }
    return activeNodeAt(_state, kind, 0u);
}

- (void)reloadModel
{
    const auto count = s3g::tracker::daisyDrumParameterCount(
        [self selectedKind]);
    if (count == 0u) _focusedParameter = 0;
    else _focusedParameter = std::clamp<NSInteger>(_focusedParameter, 0,
        static_cast<NSInteger>(count) - 1);
    [self setNeedsDisplay:YES];
}

- (void)selectInstance:(std::size_t)index
{
    if (!_state) return;
    const auto nodeId = activeNodeAt(_state, [self selectedKind], index);
    if (nodeId == s3g::tracker::kInvalidInstrumentNode) return;
    _state->selectedRackInstrument = nodeId;
    [self setNeedsDisplay:YES];
}

- (void)setParameter:(std::size_t)index normalized:(float)normalized
{
    if (!_state) return;
    const auto kind = [self selectedKind];
    const auto* parameter = s3g::tracker::daisyDrumParameter(kind, index);
    if (!parameter) return;
    const float value = std::clamp(std::isfinite(normalized)
            ? normalized : 0.0f, 0.0f, 1.0f);
    const auto nodeId = [self selectedNode];
    if (!s3g::tracker::setDaisyDrumBaseParameter(
            _state->instrumentRack, nodeId,
            parameter->parameterId, value)) return;
    if (_callbacks && _callbacks->instrumentParameterChanged) {
        _callbacks->instrumentParameterChanged(
            nodeId, parameter->parameterId, value);
    }
    [self setNeedsDisplay:YES];
}

- (void)updateParameter:(std::size_t)index point:(NSPoint)point
{
    if (index >= _barRects.size()) return;
    const NSRect bar = _barRects[index];
    const float normalized = static_cast<float>(std::clamp(
        (point.x - NSMinX(bar)) / std::max<CGFloat>(1.0, NSWidth(bar)),
        0.0, 1.0));
    [self setParameter:index normalized:normalized];
}

- (void)applyPreset:(std::size_t)index
{
    if (!_state || index >= s3g::tracker::kDaisyDrumPresetCount) return;
    if (!s3g::tracker::applyDaisyDrumPreset(_state->instrumentRack,
            [self selectedNode], index)) return;
    if (_callbacks && _callbacks->instrumentRackChanged)
        _callbacks->instrumentRackChanged();
    [self setNeedsDisplay:YES];
}

- (void)resetPatch
{
    if (!_state) return;
    const auto defaults = s3g::tracker::makeDefaultInstrumentRack();
    const auto nodeId = [self selectedNode];
    const auto patchIndex = s3g::tracker::daisyDrumPatchIndex(nodeId);
    if (patchIndex >= _state->instrumentRack.daisyDrumPatches.size()) return;
    _state->instrumentRack.daisyDrumPatches[patchIndex]
        = defaults.daisyDrumPatches[patchIndex];
    if (_callbacks && _callbacks->instrumentRackChanged)
        _callbacks->instrumentRackChanged();
    [self setNeedsDisplay:YES];
}

- (void)audition
{
    const auto nodeId = [self selectedNode];
    if (nodeId == s3g::tracker::kInvalidInstrumentNode) return;
    if (_callbacks && _callbacks->auditionInstrument) {
        _callbacks->auditionInstrument(nodeId,
            auditionNote([self selectedKind]), 0.92f);
    }
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    const auto kind = [self selectedKind];
    for (std::size_t index = 0u; index < activeCount(_state, kind); ++index) {
        if (NSPointInRect(point, _tabRects[index])) {
            [self selectInstance:index];
            [self.window makeFirstResponder:self];
            return;
        }
    }
    for (std::size_t index = 0u; index < _presetRects.size(); ++index) {
        if (NSPointInRect(point, _presetRects[index])) {
            [self applyPreset:index];
            return;
        }
    }
    if (NSPointInRect(point, _auditionRect)) {
        [self audition];
        [self.window makeFirstResponder:self];
        return;
    }
    if (NSPointInRect(point, _resetRect)) {
        [self resetPatch];
        return;
    }
    const auto count = s3g::tracker::daisyDrumParameterCount(kind);
    for (std::size_t index = 0u; index < count; ++index) {
        if (!NSPointInRect(point, NSInsetRect(_barRects[index], 0.0, -9.0)))
            continue;
        _activeParameter = static_cast<NSInteger>(index);
        _focusedParameter = _activeParameter;
        if (event.clickCount >= 2) {
            const auto* parameter = s3g::tracker::daisyDrumParameter(kind,
                index);
            if (parameter) [self setParameter:index normalized:
                s3g::tracker::daisyDrumNormalizedFromNative(kind,
                    parameter->parameterId, parameter->defaultValue)];
        } else {
            [self updateParameter:index point:point];
        }
        [self.window makeFirstResponder:self];
        return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    if (_activeParameter < 0) return;
    [self updateParameter:static_cast<std::size_t>(_activeParameter)
        point:[self convertPoint:event.locationInWindow fromView:nil]];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _activeParameter = -1;
}

- (void)keyDown:(NSEvent*)event
{
    NSString* key = event.charactersIgnoringModifiers.lowercaseString;
    if (event.keyCode == 49u) { [self audition]; return; }
    if ([key isEqualToString:@"r"]) { [self resetPatch]; return; }
    const auto kind = [self selectedKind];
    if (key.length == 1u) {
        const unichar character = [key characterAtIndex:0u];
        const auto count = activeCount(_state, kind);
        if (character >= '1' && character < '1' + count) {
            [self selectInstance:static_cast<std::size_t>(character - '1')];
            return;
        }
    }
    const auto count = s3g::tracker::daisyDrumParameterCount(kind);
    if ((event.keyCode == 125 || event.keyCode == 126) && count > 0u) {
        const NSInteger delta = event.keyCode == 125 ? 1 : -1;
        _focusedParameter = std::clamp<NSInteger>(_focusedParameter + delta,
            0, static_cast<NSInteger>(count) - 1);
        [self setNeedsDisplay:YES];
        return;
    }
    if ((event.keyCode == 123 || event.keyCode == 124) && count > 0u) {
        const auto index = static_cast<std::size_t>(_focusedParameter);
        const auto* parameter = s3g::tracker::daisyDrumParameter(kind, index);
        if (!parameter || !_state) return;
        const float current = s3g::tracker::daisyDrumBaseParameter(
            _state->instrumentRack, [self selectedNode],
            parameter->parameterId);
        const float step = (event.modifierFlags & NSEventModifierFlagShift)
            != 0u ? 0.005f : 0.02f;
        [self setParameter:index normalized:current
            + (event.keyCode == 124 ? step : -step)];
        return;
    }
    [super keyDown:event];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    fill(self.bounds, color(0x0b0b0b));
    _barRects.fill(NSZeroRect);
    _tabRects.fill(NSZeroRect);
    _presetRects.fill(NSZeroRect);
    const auto kind = [self selectedKind];
    const auto nodeId = [self selectedNode];
    text(@"DAISYSP DRUM VOICE", NSMakeRect(kMargin, 14.0, 320.0, 24.0),
        color(0xa8a8a8), 16.0, NSFontWeightMedium);
    text(nsString(s3g::tracker::instrumentKindName(kind)),
        NSMakeRect(kMargin, 40.0, 520.0, 18.0), color(0x858585), 9.0,
        NSFontWeightMedium);

    const auto instanceCount = std::max<std::size_t>(1u,
        activeCount(_state, kind));
    const auto selectedInstance = activeIndexForNode(_state, kind, nodeId);
    const CGFloat tabWidth = (NSWidth(self.bounds) - kMargin * 2.0)
        / static_cast<CGFloat>(instanceCount);
    for (std::size_t index = 0u; index < instanceCount; ++index) {
        NSRect tab = NSMakeRect(kMargin + tabWidth * index, 68.0,
            tabWidth, 34.0);
        tab = NSInsetRect(tab, index == 0u ? 0.0 : 2.0, 0.0);
        _tabRects[index] = tab;
        const bool active = index == selectedInstance;
        fill(tab, active
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::Selection)
                : S3GTrackerThemeColor(S3GTrackerThemeRole::Control));
        stroke(tab, active
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::Focus)
                : S3GTrackerThemeColor(S3GTrackerThemeRole::Border));
        const auto tabNode = activeNodeAt(_state, kind, index);
        const auto rackIndex = _state
            ? s3g::tracker::rackIndexForNode(_state->instrumentRack, tabNode)
            : 0u;
        text([NSString stringWithFormat:@"%02lu  INSTANCE %lu",
                static_cast<unsigned long>(rackIndex),
                static_cast<unsigned long>(index + 1u)],
            NSInsetRect(tab, 7.0, 9.0),
            active ? S3GTrackerThemeColor(S3GTrackerThemeRole::TextPrimary)
                   : S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted),
            8.5, NSFontWeightSemibold, NSTextAlignmentCenter);
    }

    const NSRect model = NSMakeRect(kMargin, 118.0, 270.0,
        NSHeight(self.bounds) - 190.0);
    fill(model, color(0x121212));
    stroke(model, color(0x3f3f3f));
    text(@"VOICE MODEL", NSMakeRect(NSMinX(model) + 14.0,
        NSMinY(model) + 14.0, NSWidth(model) - 28.0, 14.0),
        color(0xa8a8a8), 8.5, NSFontWeightSemibold);
    text(modelDescription(kind), NSMakeRect(NSMinX(model) + 14.0,
        NSMinY(model) + 48.0, NSWidth(model) - 28.0, 44.0),
        color(0x8a8a8a), 7.5, NSFontWeightMedium);

    NSBezierPath* signal = [NSBezierPath bezierPath];
    const CGFloat signalY = NSMinY(model) + 125.0;
    [signal moveToPoint:NSMakePoint(NSMinX(model) + 18.0, signalY)];
    for (NSInteger step = 0; step < 9; ++step) {
        const CGFloat x = NSMinX(model) + 18.0 + step * 27.0;
        const CGFloat y = signalY + std::sin(step * 1.9) * (30.0 - step * 2.0);
        [signal lineToPoint:NSMakePoint(x, y)];
    }
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Instrument, 0.75) setStroke];
    signal.lineWidth = 1.5;
    [signal stroke];

    text(@"BASE PATCHES", NSMakeRect(NSMinX(model) + 14.0,
        NSMinY(model) + 182.0, NSWidth(model) - 28.0, 14.0),
        color(0x858585), 8.0, NSFontWeightSemibold);
    const auto selectedPreset = _state
        ? s3g::tracker::daisyDrumPresetIndex(_state->instrumentRack, nodeId)
        : s3g::tracker::kDaisyDrumPresetCount;
    for (std::size_t index = 0u;
         index < s3g::tracker::kDaisyDrumPresetCount; ++index) {
        const NSRect button = NSMakeRect(NSMinX(model) + 14.0,
            NSMinY(model) + 208.0 + index * 37.0,
            NSWidth(model) - 28.0, 29.0);
        _presetRects[index] = button;
        const bool active = selectedPreset == index;
        fill(button, active ? color(0x313131) : color(0x222222));
        stroke(button, active
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::Focus)
                : color(0x555555));
        const auto* preset = s3g::tracker::daisyDrumPreset(kind, index);
        text(preset ? nsString(preset->name) : @"PRESET",
            NSInsetRect(button, 8.0, 8.0),
            active ? color(0xd0d0d0) : color(0xaaaaaa), 8.0,
            NSFontWeightMedium, NSTextAlignmentCenter);
    }

    const CGFloat panelLeft = NSMaxX(model) + 24.0;
    const CGFloat panelWidth = NSWidth(self.bounds) - panelLeft - kMargin;
    const auto parameterCount = s3g::tracker::daisyDrumParameterCount(kind);
    const CGFloat rowHeight = std::min<CGFloat>(49.0,
        (NSHeight(self.bounds) - 178.0) /
            static_cast<CGFloat>(std::max<std::size_t>(1u, parameterCount)));
    for (std::size_t index = 0u; index < parameterCount; ++index) {
        const auto* parameter = s3g::tracker::daisyDrumParameter(kind, index);
        if (!parameter || !_state) continue;
        const CGFloat top = 120.0 + static_cast<CGFloat>(index) * rowHeight;
        const float normalized = s3g::tracker::daisyDrumBaseParameter(
            _state->instrumentRack, nodeId, parameter->parameterId);
        text([nsString(parameter->displayName) uppercaseString],
            NSMakeRect(panelLeft, top, panelWidth * 0.58, 15.0),
            color(0xc5c5c5), 9.0, NSFontWeightMedium);
        text(formatted(kind, *parameter, normalized),
            NSMakeRect(panelLeft + panelWidth * 0.58, top,
                panelWidth * 0.42, 15.0), color(0xa8a8a8), 8.0,
            NSFontWeightMedium, NSTextAlignmentRight);
        const NSRect bar = NSMakeRect(panelLeft, top + 22.0,
            panelWidth, 12.0);
        _barRects[index] = bar;
        fill(bar, color(0x1b1b1b));
        stroke(bar, index == static_cast<std::size_t>(_focusedParameter)
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::Focus)
                : S3GTrackerThemeColor(S3GTrackerThemeRole::Border));
        fill(NSMakeRect(NSMinX(bar) + 2.0, NSMinY(bar) + 2.0,
            std::max<CGFloat>(0.0, (NSWidth(bar) - 4.0) * normalized),
            NSHeight(bar) - 4.0),
            S3GTrackerThemeColor(S3GTrackerThemeRole::Instrument, 0.72));
    }

    _auditionRect = NSMakeRect(panelLeft, NSHeight(self.bounds) - 54.0,
        140.0, 30.0);
    fill(_auditionRect,
        S3GTrackerThemeColor(S3GTrackerThemeRole::Live, 0.12));
    stroke(_auditionRect,
        S3GTrackerThemeColor(S3GTrackerThemeRole::Live, 0.8));
    text(@"AUDITION", NSInsetRect(_auditionRect, 8.0, 8.0),
        S3GTrackerThemeColor(S3GTrackerThemeRole::Live), 8.5,
        NSFontWeightMedium, NSTextAlignmentCenter);
    _resetRect = NSMakeRect(NSMaxX(_auditionRect) + 12.0,
        NSHeight(self.bounds) - 54.0, 140.0, 30.0);
    fill(_resetRect, color(0x242424));
    stroke(_resetRect, color(0x6f6f6f));
    text(@"RESET PATCH", NSInsetRect(_resetRect, 8.0, 8.0),
        color(0xc5c5c5), 8.5, NSFontWeightMedium, NSTextAlignmentCenter);
    text(@"SPACE: AUDITION   R: RESET   ARROWS: EDIT",
        NSMakeRect(NSMaxX(_resetRect) + 12.0,
            NSMinY(_resetRect) + 9.0,
            NSMaxX(self.bounds) - kMargin - NSMaxX(_resetRect) - 12.0,
            14.0), color(0x737373), 6.7, NSFontWeightMedium,
        NSTextAlignmentRight);
}

- (void)resetCursorRects
{
    [super resetCursorRects];
    const auto kind = [self selectedKind];
    for (std::size_t index = 0u; index < activeCount(_state, kind); ++index)
        [self addCursorRect:_tabRects[index]
            cursor:NSCursor.pointingHandCursor];
    const auto count = s3g::tracker::daisyDrumParameterCount(kind);
    for (std::size_t index = 0u; index < count; ++index)
        [self addCursorRect:NSInsetRect(_barRects[index], 0.0, -9.0)
            cursor:NSCursor.resizeLeftRightCursor];
    for (NSRect rect : _presetRects)
        [self addCursorRect:rect cursor:NSCursor.pointingHandCursor];
    [self addCursorRect:_auditionRect cursor:NSCursor.pointingHandCursor];
    [self addCursorRect:_resetRect cursor:NSCursor.pointingHandCursor];
}

@end

@interface S3GTrackerDaisyDrumWindowController () <NSWindowDelegate>
@property(nonatomic, strong) S3GTrackerDaisyDrumView* drumView;
@end

@implementation S3GTrackerDaisyDrumWindowController

- (instancetype)initWithState:(TrackerViewState*)state
    callbacks:(WorkspaceCallbacks*)callbacks
{
    const NSRect frame = NSMakeRect(0.0, 0.0, 800.0, 560.0);
    const NSWindowStyleMask style = NSWindowStyleMaskTitled
        | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
        | NSWindowStyleMaskResizable;
    NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:style backing:NSBackingStoreBuffered defer:NO];
    self = [super initWithWindow:window];
    if (self) {
        self.drumView = [[S3GTrackerDaisyDrumView alloc]
            initWithState:state callbacks:callbacks];
        window.title = @"s3g Tracker — DaisySP Drum Voice";
        window.contentView = self.drumView;
        window.minSize = NSMakeSize(720.0, 540.0);
        window.backgroundColor = color(0x0b0b0b);
        window.appearance = [NSAppearance
            appearanceNamed:NSAppearanceNameDarkAqua];
        window.tabbingMode = NSWindowTabbingModeDisallowed;
        window.releasedWhenClosed = NO;
        window.delegate = self;
        window.acceptsMouseMovedEvents = YES;
        S3GTrackerRestoreWindowFrame(window,
            @"S3GTrackerDaisyDrumWindow");
    }
    return self;
}

- (void)reloadModel { [self.drumView reloadModel]; }

- (void)showWindow:(id)sender
{
    [self reloadModel];
    [super showWindow:sender];
    [self.window makeKeyAndOrderFront:sender];
    [self.window makeFirstResponder:self.drumView];
    [NSApp activateIgnoringOtherApps:YES];
}

@end
