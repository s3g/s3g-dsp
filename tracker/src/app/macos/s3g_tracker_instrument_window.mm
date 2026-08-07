#import "s3g_tracker_instrument_window.h"

#import "s3g_tracker_controls.h"
#import "s3g_tracker_workspace.h"

#include "s3g/tracker/instrument_rack.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace {

using s3g::tracker::InstrumentRackState;
using s3g::tracker::MembraneControlKind;
using s3g::tracker::MembraneInstrumentRole;
using s3g::tracker::MembraneParameterDefinition;
using s3g::tracker::MembraneParameterGroup;
using s3g::tracker::app::TrackerViewState;
using s3g::tracker::app::WorkspaceCallbacks;

constexpr CGFloat kOuterMargin = 18.0;
constexpr CGFloat kTabTop = 48.0;
constexpr CGFloat kTabHeight = 34.0;
constexpr CGFloat kContentTop = 96.0;
constexpr CGFloat kFooterHeight = 58.0;
constexpr NSInteger kRoleControlCount = static_cast<NSInteger>(
    s3g::tracker::kMembraneRackSlotCount);
constexpr NSInteger kParameterControlStart = kRoleControlCount;
constexpr NSInteger kParameterControlCount = static_cast<NSInteger>(
    s3g::tracker::kMembraneParameterCount);
constexpr NSInteger kAuditionControl = kParameterControlStart
    + kParameterControlCount;
constexpr NSInteger kResetControl = kAuditionControl + 1;
constexpr NSInteger kPresetControlStart = kResetControl + 1;
constexpr NSInteger kAccessibleControlCount = kPresetControlStart
    + static_cast<NSInteger>(s3g::tracker::kMembranePresetCount);

std::size_t activeMembraneCount(const InstrumentRackState& rack) noexcept
{
    return s3g::tracker::activeInstrumentCount(rack,
        s3g::tracker::InstrumentKind::MembraneKick);
}

uint32_t activeMembraneNodeAt(const InstrumentRackState& rack,
    std::size_t activeIndex) noexcept
{
    std::size_t current = 0u;
    for (const auto& instrument : rack.instruments) {
        if (!instrument.active
            || instrument.kind != s3g::tracker::InstrumentKind::MembraneKick)
            continue;
        if (current++ == activeIndex) return instrument.nodeId;
    }
    return s3g::tracker::kInvalidInstrumentNode;
}

std::size_t activeMembraneIndexForNode(const InstrumentRackState& rack,
    uint32_t nodeId) noexcept
{
    std::size_t current = 0u;
    for (const auto& instrument : rack.instruments) {
        if (!instrument.active
            || instrument.kind != s3g::tracker::InstrumentKind::MembraneKick)
            continue;
        if (instrument.nodeId == nodeId) return current;
        ++current;
    }
    return activeMembraneCount(rack);
}

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

NSString* text(std::string_view value)
{
    NSString* result = [[NSString alloc] initWithBytes:value.data()
        length:value.size() encoding:NSUTF8StringEncoding];
    return result ? result : @"";
}

void drawText(NSString* value, NSRect rect, NSColor* foreground,
    CGFloat size, NSTextAlignment alignment = NSTextAlignmentLeft)
{
    NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
    style.alignment = alignment;
    style.lineBreakMode = NSLineBreakByClipping;
    [value drawInRect:rect withAttributes:@{
        NSForegroundColorAttributeName: foreground,
        NSFontAttributeName: S3GTrackerFont(size),
        NSParagraphStyleAttributeName: style,
    }];
}

NSString* groupName(MembraneParameterGroup group)
{
    switch (group) {
    case MembraneParameterGroup::Body: return @"BODY";
    case MembraneParameterGroup::Impact: return @"IMPACT";
    case MembraneParameterGroup::Strike: return @"STRIKE";
    case MembraneParameterGroup::Space: return @"SPACE";
    case MembraneParameterGroup::Response: return @"RESPONSE";
    }
    return @"PARAMETERS";
}

NSColor* groupColor(MembraneParameterGroup group)
{
    switch (group) {
    case MembraneParameterGroup::Body: return color(0xd8d8d8);
    case MembraneParameterGroup::Impact: return color(0xc0c0c0);
    case MembraneParameterGroup::Strike: return color(0xa8a8a8);
    case MembraneParameterGroup::Space: return color(0x909090);
    case MembraneParameterGroup::Response: return color(0x787878);
    }
    return color(0xb0b0b0);
}

uint8_t auditionNote(MembraneInstrumentRole role)
{
    switch (role) {
    case MembraneInstrumentRole::Kick: return 36u;
    case MembraneInstrumentRole::SnareBody: return 38u;
    case MembraneInstrumentRole::FloorTom: return 41u;
    case MembraneInstrumentRole::LowTom: return 43u;
    case MembraneInstrumentRole::HighTom: return 45u;
    }
    return 36u;
}

NSString* formattedValue(const MembraneParameterDefinition& parameter,
    float normalized)
{
    const double native = s3g::tracker::membraneNativeFromNormalized(
        parameter.parameterId, normalized);
    if (parameter.parameterId == 2u) {
        constexpr std::array<const char*, 5u> names {{
            "CIRCLE", "ELLIPSE", "SQUARE", "TRIANGLE", "IRREGULAR",
        }};
        const auto index = static_cast<std::size_t>(std::clamp(
            std::llround(native), 0ll, 4ll));
        return [NSString stringWithUTF8String:names[index]];
    }
    if (parameter.parameterId == 21u) {
        constexpr std::array<const char*, 3u> names {{
            "FIXED", "RANDOM AREA", "RANDOM RIM",
        }};
        const auto index = static_cast<std::size_t>(std::clamp(
            std::llround(native), 0ll, 2ll));
        return [NSString stringWithUTF8String:names[index]];
    }
    NSString* unit = text(parameter.unit);
    if (parameter.control == MembraneControlKind::Stepped)
        return [NSString stringWithFormat:@"%.0f %@", native, unit];
    if (std::abs(parameter.maximum - parameter.minimum) > 20.0)
        return [NSString stringWithFormat:@"%.1f %@", native, unit];
    return [NSString stringWithFormat:@"%.2f %@", native, unit];
}

NSBezierPath* membraneOutline(NSRect rect, int shape, double amount,
    double rotationDegrees)
{
    const NSPoint center = NSMakePoint(NSMidX(rect), NSMidY(rect));
    const CGFloat radius = std::min(NSWidth(rect), NSHeight(rect)) * 0.5;
    const CGFloat deformation = static_cast<CGFloat>(std::clamp(
        amount, 0.0, 1.0));
    NSBezierPath* path = [NSBezierPath bezierPath];
    if (shape == 0) {
        path = [NSBezierPath bezierPathWithOvalInRect:rect];
    } else if (shape == 1) {
        const CGFloat yRadius = radius * (1.0 - 0.34 * deformation);
        path = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            center.x - radius, center.y - yRadius,
            radius * 2.0, yRadius * 2.0)];
    } else if (shape == 2) {
        const CGFloat corner = radius * (0.25 - 0.18 * deformation);
        path = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(
            center.x - radius * 0.86, center.y - radius * 0.86,
            radius * 1.72, radius * 1.72)
            xRadius:corner yRadius:corner];
    } else if (shape == 3) {
        for (int vertex = 0; vertex < 3; ++vertex) {
            const double phase = -M_PI * 0.5
                + static_cast<double>(vertex) * 2.0 * M_PI / 3.0;
            const NSPoint point = NSMakePoint(center.x
                    + radius * static_cast<CGFloat>(std::cos(phase)),
                center.y + radius * static_cast<CGFloat>(std::sin(phase)));
            if (vertex == 0) [path moveToPoint:point];
            else [path lineToPoint:point];
        }
        [path closePath];
    } else {
        constexpr std::array<double, 10u> variation {{
            0.00, -0.19, 0.12, -0.08, 0.18,
            -0.14, 0.07, -0.21, 0.15, -0.05,
        }};
        for (std::size_t vertex = 0u; vertex < variation.size(); ++vertex) {
            const double phase = -M_PI * 0.5
                + static_cast<double>(vertex) * 2.0 * M_PI
                    / static_cast<double>(variation.size());
            const CGFloat localRadius = radius * static_cast<CGFloat>(
                0.91 + variation[vertex] * deformation);
            const NSPoint point = NSMakePoint(center.x
                    + localRadius * static_cast<CGFloat>(std::cos(phase)),
                center.y + localRadius * static_cast<CGFloat>(std::sin(phase)));
            if (vertex == 0u) [path moveToPoint:point];
            else [path lineToPoint:point];
        }
        [path closePath];
    }
    if (shape != 0 && std::abs(rotationDegrees) > 0.001) {
        NSAffineTransform* transform = [NSAffineTransform transform];
        [transform translateXBy:center.x yBy:center.y];
        [transform rotateByDegrees:static_cast<CGFloat>(rotationDegrees)];
        [transform translateXBy:-center.x yBy:-center.y];
        [path transformUsingAffineTransform:transform];
    }
    return path;
}

} // namespace

@class S3GTrackerMembraneRackView;

@interface S3GTrackerRackAccessibilityElement : NSAccessibilityElement
@property(nonatomic, weak) S3GTrackerMembraneRackView* rackOwner;
@property(nonatomic) NSInteger controlIndex;
- (instancetype)initWithOwner:(S3GTrackerMembraneRackView*)owner
    controlIndex:(NSInteger)controlIndex;
@end

@interface S3GTrackerMembraneRackView : NSView {
@private
    TrackerViewState* _state;
    WorkspaceCallbacks* _callbacks;
    std::array<NSRect, s3g::tracker::kMembraneRackSlotCount> _tabRects;
    std::array<NSRect, s3g::tracker::kMembraneParameterCount>
        _parameterRects;
    std::array<NSRect, s3g::tracker::kMembraneParameterCount>
        _parameterBarRects;
    NSRect _membraneRect;
    NSRect _auditionRect;
    NSRect _resetRect;
    std::array<NSRect, s3g::tracker::kMembranePresetCount> _presetRects;
    NSInteger _activeParameter;
    NSInteger _focusedControl;
    BOOL _draggingStrike;
    NSArray<S3GTrackerRackAccessibilityElement*>* _accessibilityControls;
}

- (instancetype)initWithState:(TrackerViewState*)state
    callbacks:(WorkspaceCallbacks*)callbacks;
- (void)reloadModel;
- (NSRect)accessibilityFrameForControl:(NSInteger)control;
- (NSString*)accessibilityRoleForControl:(NSInteger)control;
- (NSString*)accessibilityLabelForControl:(NSInteger)control;
- (NSString*)accessibilityHelpForControl:(NSInteger)control;
- (NSString*)accessibilityIdentifierForControl:(NSInteger)control;
- (id)accessibilityValueForControl:(NSInteger)control;
- (NSString*)accessibilityValueDescriptionForControl:(NSInteger)control;
- (NSNumber*)accessibilityMinimumForControl:(NSInteger)control;
- (NSNumber*)accessibilityMaximumForControl:(NSInteger)control;
- (BOOL)isAccessibilityControlFocused:(NSInteger)control;
- (void)focusAccessibilityControl:(NSInteger)control notify:(BOOL)notify;
- (void)activateAccessibilityControl:(NSInteger)control;
- (void)adjustAccessibilityControl:(NSInteger)control direction:(NSInteger)direction
    fine:(BOOL)fine;
- (void)setAccessibilityValue:(id)value forControl:(NSInteger)control;
- (void)applyPreset:(std::size_t)presetIndex;

@end

@implementation S3GTrackerRackAccessibilityElement

- (instancetype)initWithOwner:(S3GTrackerMembraneRackView*)owner
    controlIndex:(NSInteger)controlIndex
{
    self = [super init];
    if (self) {
        self.rackOwner = owner;
        self.controlIndex = controlIndex;
    }
    return self;
}

- (BOOL)isAccessibilityElement { return YES; }
- (id)accessibilityParent { return self.rackOwner; }
- (NSString*)accessibilityRole
{
    return [self.rackOwner accessibilityRoleForControl:self.controlIndex];
}
- (NSString*)accessibilityLabel
{
    return [self.rackOwner accessibilityLabelForControl:self.controlIndex];
}
- (NSString*)accessibilityHelp
{
    return [self.rackOwner accessibilityHelpForControl:self.controlIndex];
}
- (NSString*)accessibilityIdentifier
{
    return [self.rackOwner accessibilityIdentifierForControl:
        self.controlIndex];
}
- (id)accessibilityValue
{
    return [self.rackOwner accessibilityValueForControl:self.controlIndex];
}
- (NSString*)accessibilityValueDescription
{
    return [self.rackOwner accessibilityValueDescriptionForControl:
        self.controlIndex];
}
- (NSNumber*)accessibilityMinValue
{
    return [self.rackOwner accessibilityMinimumForControl:self.controlIndex];
}
- (NSNumber*)accessibilityMaxValue
{
    return [self.rackOwner accessibilityMaximumForControl:self.controlIndex];
}
- (NSRect)accessibilityFrameInParentSpace
{
    return [self.rackOwner accessibilityFrameForControl:self.controlIndex];
}
- (NSRect)accessibilityFrame
{
    const NSRect local = [self.rackOwner
        accessibilityFrameForControl:self.controlIndex];
    const NSRect windowRect = [self.rackOwner convertRect:local toView:nil];
    return [self.rackOwner.window convertRectToScreen:windowRect];
}
- (BOOL)accessibilityEnabled { return YES; }
- (NSArray*)accessibilityActionNames
{
    if (self.controlIndex >= kParameterControlStart
        && self.controlIndex < kAuditionControl) {
        return @[ NSAccessibilityIncrementAction,
            NSAccessibilityDecrementAction ];
    }
    return @[ NSAccessibilityPressAction ];
}
- (BOOL)accessibilityFocused
{
    return [self.rackOwner isAccessibilityControlFocused:self.controlIndex];
}
- (void)setAccessibilityFocused:(BOOL)focused
{
    if (focused)
        [self.rackOwner focusAccessibilityControl:self.controlIndex notify:NO];
}
- (BOOL)accessibilityPerformPress
{
    [self.rackOwner activateAccessibilityControl:self.controlIndex];
    return YES;
}
- (BOOL)accessibilityPerformIncrement
{
    [self.rackOwner adjustAccessibilityControl:self.controlIndex
        direction:1 fine:NO];
    return YES;
}
- (BOOL)accessibilityPerformDecrement
{
    [self.rackOwner adjustAccessibilityControl:self.controlIndex
        direction:-1 fine:NO];
    return YES;
}
- (void)setAccessibilityValue:(id)value
{
    [self.rackOwner setAccessibilityValue:value forControl:self.controlIndex];
}

@end

@implementation S3GTrackerMembraneRackView

- (instancetype)initWithState:(TrackerViewState*)state
    callbacks:(WorkspaceCallbacks*)callbacks
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, 1120.0, 720.0)];
    if (self) {
        _state = state;
        _callbacks = callbacks;
        _activeParameter = -1;
        _focusedControl = 0;
        NSMutableArray<S3GTrackerRackAccessibilityElement*>* controls =
            [NSMutableArray arrayWithCapacity:kAccessibleControlCount];
        for (NSInteger control = 0; control < kAccessibleControlCount;
            ++control) {
            [controls addObject:[[S3GTrackerRackAccessibilityElement alloc]
                initWithOwner:self controlIndex:control]];
        }
        _accessibilityControls = [controls copy];
        self.wantsLayer = YES;
        self.layer.backgroundColor = color(0x0c0c0c).CGColor;
        self.accessibilityRole = NSAccessibilityGroupRole;
        self.accessibilityLabel = @"Membrane instrument editor";
        self.accessibilityHelp = @"Edit the selected Membrane Kick instance. If more instances are added from the instrument toolbox, their indexed tabs appear here. Double-click a parameter to restore its default.";
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isAccessibilityElement { return YES; }
- (NSArray*)accessibilityChildren { return _accessibilityControls; }

- (BOOL)becomeFirstResponder
{
    const BOOL result = [super becomeFirstResponder];
    [self setNeedsDisplay:YES];
    return result;
}

- (BOOL)resignFirstResponder
{
    const BOOL result = [super resignFirstResponder];
    [self setNeedsDisplay:YES];
    return result;
}

- (std::size_t)selectedSlotIndex
{
    if (!_state) return 0u;
    const auto& rack = _state->instrumentRack;
    for (std::size_t index = 0u; index < rack.slots.size(); ++index) {
        if (rack.slots[index].nodeId == rack.selectedNode) return index;
    }
    return 0u;
}

- (std::size_t)selectedInstanceIndex
{
    if (!_state) return 0u;
    const auto index = activeMembraneIndexForNode(
        _state->instrumentRack, _state->instrumentRack.selectedNode);
    return index < activeMembraneCount(_state->instrumentRack) ? index : 0u;
}

- (NSRect)accessibilityFrameForControl:(NSInteger)control
{
    if (control >= 0 && control < kRoleControlCount)
        return _tabRects[static_cast<std::size_t>(control)];
    if (control >= kParameterControlStart && control < kAuditionControl) {
        return _parameterRects[static_cast<std::size_t>(
            control - kParameterControlStart)];
    }
    if (control == kAuditionControl) return _auditionRect;
    if (control == kResetControl) return _resetRect;
    if (control >= kPresetControlStart
        && control < kAccessibleControlCount) {
        return _presetRects[static_cast<std::size_t>(
            control - kPresetControlStart)];
    }
    return NSZeroRect;
}

- (NSString*)accessibilityRoleForControl:(NSInteger)control
{
    if (control >= 0 && control < kRoleControlCount)
        return NSAccessibilityRadioButtonRole;
    if (control >= kParameterControlStart && control < kAuditionControl)
        return NSAccessibilitySliderRole;
    return NSAccessibilityButtonRole;
}

- (NSString*)accessibilityLabelForControl:(NSInteger)control
{
    if (control >= 0 && control < kRoleControlCount && _state) {
        const auto node = activeMembraneNodeAt(_state->instrumentRack,
            static_cast<std::size_t>(control));
        const auto index = s3g::tracker::rackIndexForNode(
            _state->instrumentRack, node);
        return index < _state->instrumentRack.instruments.size()
            ? [NSString stringWithFormat:@"Instrument %02lu, Membrane Kick",
                static_cast<unsigned long>(index)]
            : @"Unused membrane instance";
    }
    if (control >= kParameterControlStart && control < kAuditionControl) {
        const auto* parameter = s3g::tracker::membraneParameter(
            static_cast<std::size_t>(control - kParameterControlStart));
        return parameter ? text(parameter->displayName) : @"Parameter";
    }
    if (control == kAuditionControl) return @"Audition instrument";
    if (control == kResetControl) return @"Reset kick patch";
    if (control >= kPresetControlStart
        && control < kAccessibleControlCount) {
        const auto* preset = s3g::tracker::membranePreset(
            static_cast<std::size_t>(control - kPresetControlStart));
        return preset ? [NSString stringWithFormat:@"Apply %@ preset",
            text(preset->name)] : @"Apply preset";
    }
    return @"Membrane control";
}

- (NSString*)accessibilityHelpForControl:(NSInteger)control
{
    if (control >= 0 && control < kRoleControlCount)
        return @"Selects this membrane voice for editing.";
    if (control >= kParameterControlStart && control < kAuditionControl)
        return @"Use increment or decrement actions, or the arrow keys, to adjust this base-patch parameter.";
    if (control == kAuditionControl)
        return @"Plays the selected membrane voice at a fixed audition velocity.";
    if (control == kResetControl)
        return @"Restores every parameter in the selected kick instance to its default.";
    if (control >= kPresetControlStart && control < kAccessibleControlCount)
        return @"Applies this complete membrane kick preset to the selected instance.";
    return @"";
}

- (NSString*)accessibilityIdentifierForControl:(NSInteger)control
{
    if (control >= 0 && control < kRoleControlCount)
        return [NSString stringWithFormat:@"membrane.instance.%ld",
            static_cast<long>(control)];
    if (control >= kParameterControlStart && control < kAuditionControl) {
        const auto* parameter = s3g::tracker::membraneParameter(
            static_cast<std::size_t>(control - kParameterControlStart));
        return parameter
            ? [NSString stringWithFormat:@"membrane.parameter.%@",
                text(parameter->stableKey)]
            : @"membrane.parameter";
    }
    if (control == kAuditionControl) return @"membrane.audition";
    if (control == kResetControl) return @"membrane.reset";
    if (control >= kPresetControlStart && control < kAccessibleControlCount)
        return [NSString stringWithFormat:@"membrane.preset.%ld",
            static_cast<long>(control - kPresetControlStart)];
    return @"membrane.control";
}

- (id)accessibilityValueForControl:(NSInteger)control
{
    if (!_state) return nil;
    if (control >= 0 && control < kRoleControlCount) {
        return @([self selectedInstanceIndex]
            == static_cast<std::size_t>(control));
    }
    if (control >= kParameterControlStart && control < kAuditionControl) {
        const auto index = static_cast<std::size_t>(
            control - kParameterControlStart);
        const auto* parameter = s3g::tracker::membraneParameter(index);
        if (!parameter) return nil;
        const auto slot = [self selectedSlotIndex];
        return @(s3g::tracker::membraneNativeFromNormalized(
            parameter->parameterId,
            _state->instrumentRack.slots[slot].basePatch.normalized[index]));
    }
    return nil;
}

- (NSString*)accessibilityValueDescriptionForControl:(NSInteger)control
{
    if (!_state) return @"";
    if (control >= 0 && control < kRoleControlCount)
        return [self selectedSlotIndex] == static_cast<std::size_t>(control)
            ? @"selected" : @"not selected";
    if (control >= kParameterControlStart && control < kAuditionControl) {
        const auto index = static_cast<std::size_t>(
            control - kParameterControlStart);
        const auto* parameter = s3g::tracker::membraneParameter(index);
        if (!parameter) return @"";
        const auto slot = [self selectedSlotIndex];
        return formattedValue(*parameter,
            _state->instrumentRack.slots[slot].basePatch.normalized[index]);
    }
    return @"";
}

- (NSNumber*)accessibilityMinimumForControl:(NSInteger)control
{
    if (control < kParameterControlStart || control >= kAuditionControl)
        return nil;
    const auto* parameter = s3g::tracker::membraneParameter(
        static_cast<std::size_t>(control - kParameterControlStart));
    return parameter ? @(parameter->minimum) : nil;
}

- (NSNumber*)accessibilityMaximumForControl:(NSInteger)control
{
    if (control < kParameterControlStart || control >= kAuditionControl)
        return nil;
    const auto* parameter = s3g::tracker::membraneParameter(
        static_cast<std::size_t>(control - kParameterControlStart));
    return parameter ? @(parameter->maximum) : nil;
}

- (BOOL)isAccessibilityControlFocused:(NSInteger)control
{
    return _focusedControl == control;
}

- (void)focusAccessibilityControl:(NSInteger)control notify:(BOOL)notify
{
    if (control < 0 || control >= kAccessibleControlCount) return;
    _focusedControl = control;
    [self.window makeFirstResponder:self];
    [self setNeedsDisplay:YES];
    if (notify && control < static_cast<NSInteger>(
            _accessibilityControls.count)) {
        NSAccessibilityPostNotification(_accessibilityControls[
            static_cast<NSUInteger>(control)],
            NSAccessibilityFocusedUIElementChangedNotification);
    }
}

- (void)activateAccessibilityControl:(NSInteger)control
{
    [self focusAccessibilityControl:control notify:NO];
    if (control >= 0 && control < kRoleControlCount) {
        [self selectSlot:static_cast<std::size_t>(control)];
    } else if (control == kAuditionControl) {
        [self audition];
    } else if (control == kResetControl) {
        [self resetPatch];
    } else if (control >= kPresetControlStart
        && control < kAccessibleControlCount) {
        [self applyPreset:static_cast<std::size_t>(
            control - kPresetControlStart)];
    }
}

- (void)adjustAccessibilityControl:(NSInteger)control
    direction:(NSInteger)direction fine:(BOOL)fine
{
    if (direction == 0) return;
    if (control >= 0 && control < kRoleControlCount) {
        const NSInteger next = std::clamp(control + direction,
            0l, kRoleControlCount - 1);
        [self focusAccessibilityControl:next notify:YES];
        [self selectSlot:static_cast<std::size_t>(next)];
        return;
    }
    if (control < kParameterControlStart || control >= kAuditionControl
        || !_state) return;
    const auto index = static_cast<std::size_t>(
        control - kParameterControlStart);
    const auto* parameter = s3g::tracker::membraneParameter(index);
    if (!parameter) return;
    const auto slot = [self selectedSlotIndex];
    const float current = _state->instrumentRack.slots[slot]
        .basePatch.normalized[index];
    const float step = parameter->control == MembraneControlKind::Stepped
            && parameter->stepCount > 1u
        ? 1.0f / static_cast<float>(parameter->stepCount - 1u)
        : fine ? 0.001f : 0.01f;
    [self setParameterAtIndex:index normalized:std::clamp(
        current + static_cast<float>(direction) * step, 0.0f, 1.0f)];
}

- (void)setAccessibilityValue:(id)value forControl:(NSInteger)control
{
    if (![value isKindOfClass:[NSNumber class]]) return;
    if (control >= 0 && control < kRoleControlCount) {
        if ([(NSNumber*)value boolValue])
            [self activateAccessibilityControl:control];
        return;
    }
    if (control < kParameterControlStart || control >= kAuditionControl)
        return;
    const auto index = static_cast<std::size_t>(
        control - kParameterControlStart);
    const auto* parameter = s3g::tracker::membraneParameter(index);
    if (!parameter) return;
    [self setParameterAtIndex:index normalized:
        s3g::tracker::membraneNormalizedFromNative(parameter->parameterId,
            [(NSNumber*)value doubleValue])];
}

- (void)reloadModel
{
    [self setNeedsDisplay:YES];
}

- (void)drawPanel:(NSRect)rect group:(MembraneParameterGroup)group
    patch:(const s3g::tracker::MembranePatch&)patch
{
    fill(rect, color(0x111315));
    stroke(rect, color(0x34383a));
    NSColor* accent = groupColor(group);
    fill(NSMakeRect(NSMinX(rect), NSMinY(rect), 3.0, NSHeight(rect)), accent);
    drawText(groupName(group), NSMakeRect(NSMinX(rect) + 10.0,
        NSMinY(rect) + 7.0, NSWidth(rect) - 20.0, 14.0), accent, 9.5);

    std::array<std::size_t, s3g::tracker::kMembraneParameterCount> indices {};
    std::size_t count = 0u;
    for (std::size_t index = 0u;
        index < s3g::tracker::membraneParameterCount(); ++index) {
        const auto* parameter = s3g::tracker::membraneParameter(index);
        if (parameter && parameter->group == group) indices[count++] = index;
    }
    if (count == 0u) return;

    const CGFloat rowTop = NSMinY(rect) + 25.0;
    const CGFloat rowHeight = (NSHeight(rect) - 29.0)
        / static_cast<CGFloat>(count);
    for (std::size_t ordinal = 0u; ordinal < count; ++ordinal) {
        const std::size_t index = indices[ordinal];
        const auto* parameter = s3g::tracker::membraneParameter(index);
        if (!parameter) continue;
        const CGFloat y = rowTop + static_cast<CGFloat>(ordinal) * rowHeight;
        const NSRect row = NSMakeRect(NSMinX(rect) + 9.0, y,
            NSWidth(rect) - 18.0, rowHeight);
        _parameterRects[index] = row;
        if (ordinal > 0u)
            fill(NSMakeRect(NSMinX(row), y, NSWidth(row), 1.0),
                color(0x242729));

        drawText(text(parameter->displayName), NSMakeRect(NSMinX(row),
            y + 4.0, NSWidth(row) * 0.58, 13.0), color(0xb9bec1), 8.5);
        const float normalized = patch.normalized[index];
        drawText(formattedValue(*parameter, normalized), NSMakeRect(
            NSMinX(row) + NSWidth(row) * 0.46, y + 4.0,
            NSWidth(row) * 0.54, 13.0), color(0xe1e4e5), 8.5,
            NSTextAlignmentRight);

        const NSRect bar = NSMakeRect(NSMinX(row),
            NSMaxY(row) - 11.0, NSWidth(row), 6.0);
        _parameterBarRects[index] = bar;
        fill(bar, color(0x25292b));
        if (parameter->control == MembraneControlKind::Stepped
            && parameter->stepCount > 1u) {
            const CGFloat segmentWidth = NSWidth(bar)
                / static_cast<CGFloat>(parameter->stepCount);
            const auto active = static_cast<std::size_t>(std::clamp(
                std::llround(s3g::tracker::membraneNativeFromNormalized(
                    parameter->parameterId, normalized)
                    - parameter->minimum),
                0ll, static_cast<long long>(parameter->stepCount - 1u)));
            fill(NSMakeRect(NSMinX(bar) + segmentWidth * active,
                NSMinY(bar), segmentWidth, NSHeight(bar)), accent);
            for (std::size_t segment = 1u;
                segment < parameter->stepCount; ++segment) {
                fill(NSMakeRect(NSMinX(bar) + segmentWidth * segment,
                    NSMinY(bar), 1.0, NSHeight(bar)), color(0x0c0d0e));
            }
        } else {
            fill(NSMakeRect(NSMinX(bar), NSMinY(bar),
                NSWidth(bar) * std::clamp<CGFloat>(normalized, 0.0, 1.0),
                NSHeight(bar)), accent);
            const CGFloat x = NSMinX(bar) + NSWidth(bar)
                * std::clamp<CGFloat>(normalized, 0.0, 1.0);
            fill(NSMakeRect(x - 1.0, NSMinY(bar) - 2.0, 2.0,
                NSHeight(bar) + 4.0), color(0xf0f2f2));
        }
    }
}

- (void)drawGeometry:(NSRect)panel
    slot:(const s3g::tracker::MembraneInstrumentSlot&)slot
{
    fill(panel, color(0x101214));
    stroke(panel, color(0x34383a));
    drawText(@"MEMBRANE / STRIKE GEOMETRY", NSMakeRect(NSMinX(panel) + 12.0,
        NSMinY(panel) + 10.0, NSWidth(panel) - 24.0, 16.0),
        color(0xb8b8b8), 9.5);

    const CGFloat diameter = std::min(NSWidth(panel) - 62.0,
        NSHeight(panel) - 122.0);
    _membraneRect = NSMakeRect(NSMidX(panel) - diameter * 0.5,
        NSMinY(panel) + 47.0, diameter, diameter);
    const NSPoint center = NSMakePoint(NSMidX(_membraneRect),
        NSMidY(_membraneRect));
    const CGFloat radius = diameter * 0.5;

    const int shape = static_cast<int>(std::lround(
        s3g::tracker::membraneNativeFromNormalized(2u,
            s3g::tracker::membraneBaseParameter(_state->instrumentRack,
                slot.nodeId, 2u))));
    const double shapeAmount = s3g::tracker::membraneNativeFromNormalized(
        16u, s3g::tracker::membraneBaseParameter(_state->instrumentRack,
            slot.nodeId, 16u));
    const double rotation = s3g::tracker::membraneNativeFromNormalized(
        15u, s3g::tracker::membraneBaseParameter(_state->instrumentRack,
            slot.nodeId, 15u));
    NSBezierPath* disc = membraneOutline(_membraneRect, shape,
        shapeAmount, rotation);
    [color(0x151c1d) setFill];
    [disc fill];
    [NSGraphicsContext saveGraphicsState];
    [disc addClip];
    for (NSInteger ring = 1; ring <= 4; ++ring) {
        const CGFloat fraction = static_cast<CGFloat>(ring) / 5.0;
        const CGFloat r = radius * fraction;
        NSBezierPath* path = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(center.x - r, center.y - r, r * 2.0, r * 2.0)];
        [color(0x3b3b3b, 0.7) setStroke];
        path.lineWidth = 0.7;
        [path stroke];
    }
    fill(NSMakeRect(NSMinX(_membraneRect), center.y, diameter, 1.0),
        color(0x363636));
    fill(NSMakeRect(center.x, NSMinY(_membraneRect), 1.0, diameter),
        color(0x363636));
    [NSGraphicsContext restoreGraphicsState];
    [color(0x707070) setStroke];
    disc.lineWidth = 1.5;
    [disc stroke];

    const float strikeX = s3g::tracker::membraneBaseParameter(
        _state->instrumentRack, slot.nodeId, 11u);
    const float strikeY = s3g::tracker::membraneBaseParameter(
        _state->instrumentRack, slot.nodeId, 12u);
    const double nativeX = s3g::tracker::membraneNativeFromNormalized(11u,
        strikeX);
    const double nativeY = s3g::tracker::membraneNativeFromNormalized(12u,
        strikeY);
    const NSPoint strikePoint = NSMakePoint(center.x
            + radius * static_cast<CGFloat>(nativeX),
        center.y - radius * static_cast<CGFloat>(nativeY));
    NSBezierPath* halo = [NSBezierPath bezierPathWithOvalInRect:
        NSMakeRect(strikePoint.x - 10.0, strikePoint.y - 10.0, 20.0, 20.0)];
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Value, 0.22) setFill];
    [halo fill];
    NSBezierPath* point = [NSBezierPath bezierPathWithOvalInRect:
        NSMakeRect(strikePoint.x - 4.0, strikePoint.y - 4.0, 8.0, 8.0)];
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Value) setFill];
    [point fill];

    drawText(@"DRAG THE STRIKE POINT  /  DOUBLE-CLICK CONTROLS FOR DEFAULT",
        NSMakeRect(NSMinX(panel) + 12.0, NSMaxY(_membraneRect) + 14.0,
            NSWidth(panel) - 24.0, 14.0), color(0x737a80), 7.5,
        NSTextAlignmentCenter);

    drawText(@"KICK BODY  /  SAMPLE-ACCURATE EXCITATION",
        NSMakeRect(NSMinX(panel) + 12.0, NSMaxY(_membraneRect) + 34.0,
            NSWidth(panel) - 24.0, 15.0), color(0x808080), 8.0,
        NSTextAlignmentCenter);
}

- (void)drawActionButton:(NSRect)rect title:(NSString*)title accent:(NSColor*)accent
{
    fill(rect, color(0x242729));
    stroke(rect, accent);
    drawText(title, NSInsetRect(rect, 8.0, 7.0), color(0xd9dcdd), 9.0,
        NSTextAlignmentCenter);
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    fill(self.bounds, color(0x0c0c0c));
    _parameterRects.fill(NSZeroRect);
    _parameterBarRects.fill(NSZeroRect);
    _presetRects.fill(NSZeroRect);
    if (!_state) return;

    drawText(@"MEMBRANE KICK", NSMakeRect(kOuterMargin, 14.0,
        420.0, 20.0), color(0xa8aaab), 15.0);
    drawText(@"BASE PATCHES  /  FX LANES AUTOMATE WITHOUT OVERWRITING",
        NSMakeRect(NSWidth(self.bounds) - 500.0, 17.0, 482.0, 16.0),
        color(0x737a80), 8.0, NSTextAlignmentRight);

    _tabRects.fill(NSZeroRect);
    const auto instanceCount = std::max<std::size_t>(1u,
        activeMembraneCount(_state->instrumentRack));
    const CGFloat tabWidth = (NSWidth(self.bounds) - kOuterMargin * 2.0)
        / static_cast<CGFloat>(instanceCount);
    const std::size_t selectedInstance = [self selectedInstanceIndex];
    for (std::size_t index = 0u; index < instanceCount; ++index) {
        NSRect tab = NSMakeRect(kOuterMargin + tabWidth * index, kTabTop,
            tabWidth, kTabHeight);
        tab = NSInsetRect(tab, index == 0u ? 0.0 : 2.0, 0.0);
        _tabRects[index] = tab;
        const bool active = index == selectedInstance;
        fill(tab, active
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::Selection)
                : S3GTrackerThemeColor(S3GTrackerThemeRole::Panel));
        stroke(tab, active
                ? S3GTrackerThemeColor(S3GTrackerThemeRole::Focus)
                : S3GTrackerThemeColor(S3GTrackerThemeRole::Grid));
        const auto node = activeMembraneNodeAt(_state->instrumentRack, index);
        const auto rackIndex = s3g::tracker::rackIndexForNode(
            _state->instrumentRack, node);
        NSString* label = [NSString stringWithFormat:@"%02lu  MEMBRANE KICK",
            static_cast<unsigned long>(rackIndex)];
        drawText(label, NSInsetRect(tab, 7.0, 10.0),
            active ? S3GTrackerThemeColor(S3GTrackerThemeRole::TextPrimary)
                   : S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted), 9.0,
            NSTextAlignmentCenter);
    }

    const CGFloat contentBottom = NSHeight(self.bounds) - kFooterHeight;
    const CGFloat contentHeight = std::max<CGFloat>(360.0,
        contentBottom - kContentTop);
    const CGFloat geometryWidth = std::clamp(NSWidth(self.bounds) * 0.32,
        310.0, 370.0);
    const NSRect geometryPanel = NSMakeRect(kOuterMargin, kContentTop,
        geometryWidth, contentHeight);
    const auto& slot = _state->instrumentRack.slots[[self selectedSlotIndex]];
    [self drawGeometry:geometryPanel slot:slot];

    const CGFloat parameterLeft = NSMaxX(geometryPanel) + 12.0;
    const CGFloat parameterWidth = NSWidth(self.bounds) - parameterLeft
        - kOuterMargin;
    const CGFloat gap = 10.0;
    const CGFloat columnWidth = (parameterWidth - gap) * 0.5;
    const CGFloat firstHeight = std::floor((contentHeight - gap) * 0.55);
    const NSRect body = NSMakeRect(parameterLeft, kContentTop,
        columnWidth, firstHeight);
    const NSRect impact = NSMakeRect(parameterLeft, NSMaxY(body) + gap,
        columnWidth, contentHeight - firstHeight - gap);

    const CGFloat secondX = parameterLeft + columnWidth + gap;
    const CGFloat strikeHeight = std::floor((contentHeight - gap * 2.0)
        * 0.27);
    const CGFloat spaceHeight = std::floor((contentHeight - gap * 2.0)
        * 0.40);
    const NSRect strikePanel = NSMakeRect(secondX, kContentTop,
        columnWidth, strikeHeight);
    const NSRect space = NSMakeRect(secondX, NSMaxY(strikePanel) + gap,
        columnWidth, spaceHeight);
    const NSRect response = NSMakeRect(secondX, NSMaxY(space) + gap,
        columnWidth, contentHeight - strikeHeight - spaceHeight - gap * 2.0);

    const auto& patch = slot.basePatch;
    [self drawPanel:body group:MembraneParameterGroup::Body patch:patch];
    [self drawPanel:impact group:MembraneParameterGroup::Impact patch:patch];
    [self drawPanel:strikePanel group:MembraneParameterGroup::Strike patch:patch];
    [self drawPanel:space group:MembraneParameterGroup::Space patch:patch];
    [self drawPanel:response group:MembraneParameterGroup::Response patch:patch];

    _auditionRect = NSMakeRect(kOuterMargin, contentBottom + 12.0,
        174.0, 32.0);
    _resetRect = NSMakeRect(NSMaxX(_auditionRect) + 9.0,
        contentBottom + 12.0, geometryWidth - 183.0, 32.0);
    [self drawActionButton:_auditionRect title:@"● AUDITION"
        accent:S3GTrackerThemeColor(S3GTrackerThemeRole::Live)];
    [self drawActionButton:_resetRect title:@"RESET KICK PATCH"
        accent:color(0x888888)];
    const CGFloat presetGap = 5.0;
    const CGFloat presetWidth = (parameterWidth - presetGap * 4.0) / 5.0;
    for (std::size_t index = 0u; index < _presetRects.size(); ++index) {
        _presetRects[index] = NSMakeRect(parameterLeft
                + static_cast<CGFloat>(index) * (presetWidth + presetGap),
            contentBottom + 12.0, presetWidth, 32.0);
        const auto* preset = s3g::tracker::membranePreset(index);
        [self drawActionButton:_presetRects[index]
            title:preset ? text(preset->name) : @"PRESET"
            accent:color(0x666666)];
    }
    if (self.window.firstResponder == self) {
        const NSRect focusRect = [self accessibilityFrameForControl:
            _focusedControl];
        if (!NSIsEmptyRect(focusRect)) {
            stroke(NSInsetRect(focusRect, -2.0, -2.0),
                S3GTrackerThemeColor(S3GTrackerThemeRole::Focus), 1.5);
            fill(NSMakeRect(NSMinX(focusRect) - 2.0,
                NSMinY(focusRect) - 2.0,
                std::min<CGFloat>(24.0, NSWidth(focusRect) + 4.0), 2.0),
                S3GTrackerThemeColor(S3GTrackerThemeRole::Focus));
        }
    }
    [self.window invalidateCursorRectsForView:self];
}

- (void)selectSlot:(std::size_t)index
{
    if (!_state || index >= activeMembraneCount(_state->instrumentRack))
        return;
    const auto node = activeMembraneNodeAt(_state->instrumentRack, index);
    if (node == s3g::tracker::kInvalidInstrumentNode) return;
    _state->instrumentRack.selectedNode = node;
    [self setNeedsDisplay:YES];
    for (NSInteger control = 0; control < kAuditionControl; ++control) {
        NSAccessibilityPostNotification(_accessibilityControls[
            static_cast<NSUInteger>(control)],
            NSAccessibilityValueChangedNotification);
    }
}

- (void)setParameterAtIndex:(std::size_t)index normalized:(float)normalized
{
    if (!_state || index >= s3g::tracker::membraneParameterCount()) return;
    const auto* parameter = s3g::tracker::membraneParameter(index);
    if (!parameter) return;
    if (parameter->control == MembraneControlKind::Stepped) {
        const double native = s3g::tracker::membraneNativeFromNormalized(
            parameter->parameterId, normalized);
        normalized = s3g::tracker::membraneNormalizedFromNative(
            parameter->parameterId, native);
    }
    const uint32_t node = _state->instrumentRack.selectedNode;
    if (!s3g::tracker::setMembraneBaseParameter(_state->instrumentRack,
            node, parameter->parameterId, normalized)) return;
    if (_callbacks && _callbacks->instrumentParameterChanged) {
        _callbacks->instrumentParameterChanged(node,
            parameter->parameterId, normalized);
    }
    const NSInteger control = kParameterControlStart
        + static_cast<NSInteger>(index);
    NSAccessibilityPostNotification(_accessibilityControls[
        static_cast<NSUInteger>(control)],
        NSAccessibilityValueChangedNotification);
    [self setNeedsDisplay:YES];
}

- (void)setParameterFromPoint:(NSPoint)point index:(std::size_t)index
{
    if (index >= _parameterBarRects.size()) return;
    const NSRect bar = _parameterBarRects[index];
    if (NSWidth(bar) <= 0.0) return;
    const float normalized = static_cast<float>(std::clamp(
        (point.x - NSMinX(bar)) / NSWidth(bar), 0.0, 1.0));
    [self setParameterAtIndex:index normalized:normalized];
}

- (void)setStrikeFromPoint:(NSPoint)point
{
    if (!_state || NSWidth(_membraneRect) <= 0.0) return;
    const CGFloat radius = NSWidth(_membraneRect) * 0.5;
    const NSPoint center = NSMakePoint(NSMidX(_membraneRect),
        NSMidY(_membraneRect));
    double x = std::clamp((point.x - center.x) / radius, -1.0, 1.0);
    double y = std::clamp((center.y - point.y) / radius, -1.0, 1.0);
    const double distance = std::sqrt(x * x + y * y);
    if (distance > 1.0) {
        x /= distance;
        y /= distance;
    }
    const float normalizedX = s3g::tracker::membraneNormalizedFromNative(
        11u, x);
    const float normalizedY = s3g::tracker::membraneNormalizedFromNative(
        12u, y);
    const uint32_t node = _state->instrumentRack.selectedNode;
    s3g::tracker::setMembraneBaseParameter(_state->instrumentRack,
        node, 11u, normalizedX);
    s3g::tracker::setMembraneBaseParameter(_state->instrumentRack,
        node, 12u, normalizedY);
    if (_callbacks && _callbacks->instrumentParameterChanged) {
        _callbacks->instrumentParameterChanged(node, 11u, normalizedX);
        _callbacks->instrumentParameterChanged(node, 12u, normalizedY);
    }
    for (const uint32_t parameterId : { 11u, 12u }) {
        const auto index = s3g::tracker::membraneParameterIndex(parameterId);
        if (index < s3g::tracker::kMembraneParameterCount) {
            NSAccessibilityPostNotification(_accessibilityControls[
                static_cast<NSUInteger>(kParameterControlStart
                    + static_cast<NSInteger>(index))],
                NSAccessibilityValueChangedNotification);
        }
    }
    [self setNeedsDisplay:YES];
}

- (void)audition
{
    if (!_state) return;
    const auto index = [self selectedSlotIndex];
    const auto& slot = _state->instrumentRack.slots[index];
    if (_callbacks && _callbacks->auditionInstrument)
        _callbacks->auditionInstrument(slot.nodeId,
            auditionNote(slot.role), 0.92f);
}

- (void)resetPatch
{
    if (!_state) return;
    const auto index = [self selectedSlotIndex];
    const auto defaults = s3g::tracker::makeDefaultInstrumentRack();
    auto& slot = _state->instrumentRack.slots[index];
    slot.basePatch = defaults.slots[index].basePatch;
    if (_callbacks && _callbacks->resetInstrumentPatch)
        _callbacks->resetInstrumentPatch(slot.nodeId);
    for (NSInteger control = kParameterControlStart;
        control < kAuditionControl; ++control) {
        NSAccessibilityPostNotification(_accessibilityControls[
            static_cast<NSUInteger>(control)],
            NSAccessibilityValueChangedNotification);
    }
    [self setNeedsDisplay:YES];
}

- (void)applyPreset:(std::size_t)presetIndex
{
    if (!_state) return;
    const uint32_t node = _state->instrumentRack.selectedNode;
    if (!s3g::tracker::applyMembranePreset(_state->instrumentRack,
            node, presetIndex)) return;
    if (_callbacks && _callbacks->resetInstrumentPatch)
        _callbacks->resetInstrumentPatch(node);
    for (NSInteger control = kParameterControlStart;
         control < kAuditionControl; ++control) {
        NSAccessibilityPostNotification(_accessibilityControls[
            static_cast<NSUInteger>(control)],
            NSAccessibilityValueChangedNotification);
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    [self.window makeFirstResponder:self];
    const NSPoint point = [self convertPoint:event.locationInWindow
        fromView:nil];
    for (std::size_t index = 0u;
         index < activeMembraneCount(_state->instrumentRack); ++index) {
        if (NSPointInRect(point, _tabRects[index])) {
            [self focusAccessibilityControl:static_cast<NSInteger>(index)
                notify:YES];
            [self selectSlot:index];
            return;
        }
    }
    if (NSPointInRect(point, _auditionRect)) {
        [self focusAccessibilityControl:kAuditionControl notify:YES];
        [self audition];
        return;
    }
    if (NSPointInRect(point, _resetRect)) {
        [self focusAccessibilityControl:kResetControl notify:YES];
        [self resetPatch];
        return;
    }
    for (std::size_t index = 0u; index < _presetRects.size(); ++index) {
        if (!NSPointInRect(point, _presetRects[index])) continue;
        [self focusAccessibilityControl:kPresetControlStart
            + static_cast<NSInteger>(index) notify:YES];
        [self applyPreset:index];
        return;
    }
    if (NSPointInRect(point, _membraneRect)) {
        const auto strikeX = s3g::tracker::membraneParameterIndex(11u);
        if (strikeX < s3g::tracker::kMembraneParameterCount) {
            [self focusAccessibilityControl:kParameterControlStart
                + static_cast<NSInteger>(strikeX) notify:YES];
        }
        _draggingStrike = YES;
        [self setStrikeFromPoint:point];
        return;
    }
    for (std::size_t index = 0u; index < _parameterRects.size(); ++index) {
        if (!NSPointInRect(point, _parameterRects[index])) continue;
        _activeParameter = static_cast<NSInteger>(index);
        [self focusAccessibilityControl:kParameterControlStart
            + static_cast<NSInteger>(index) notify:YES];
        const auto* parameter = s3g::tracker::membraneParameter(index);
        if (event.clickCount >= 2 && parameter) {
            const auto defaults = s3g::tracker::makeDefaultInstrumentRack();
            const auto slot = [self selectedSlotIndex];
            [self setParameterAtIndex:index
                normalized:defaults.slots[slot].basePatch.normalized[index]];
        } else {
            [self setParameterFromPoint:point index:index];
        }
        return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:event.locationInWindow
        fromView:nil];
    if (_draggingStrike) {
        [self setStrikeFromPoint:point];
    } else if (_activeParameter >= 0) {
        [self setParameterFromPoint:point
            index:static_cast<std::size_t>(_activeParameter)];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _draggingStrike = NO;
    _activeParameter = -1;
}

- (void)keyDown:(NSEvent*)event
{
    NSString* characters = event.charactersIgnoringModifiers.lowercaseString;
    if ([characters isEqualToString:@" "]) {
        [self audition];
        return;
    }
    if ([characters isEqualToString:@"r"]) {
        [self resetPatch];
        return;
    }
    if (characters.length == 1u) {
        const unichar key = [characters characterAtIndex:0u];
        const auto instanceCount = _state
            ? activeMembraneCount(_state->instrumentRack) : 0u;
        if (key >= '1' && key < '1' + instanceCount) {
            const NSInteger control = static_cast<NSInteger>(key - '1');
            [self focusAccessibilityControl:control notify:YES];
            [self selectSlot:static_cast<std::size_t>(control)];
            return;
        }
    }
    if (event.keyCode == 48u) {
        const NSInteger direction = (event.modifierFlags
            & NSEventModifierFlagShift) != 0u ? -1 : 1;
        NSInteger next = (_focusedControl + direction)
            % kAccessibleControlCount;
        if (next < 0) next += kAccessibleControlCount;
        [self focusAccessibilityControl:next notify:YES];
        return;
    }
    if (event.keyCode == 36u || event.keyCode == 76u) {
        [self activateAccessibilityControl:_focusedControl];
        return;
    }
    NSInteger adjustment = 0;
    if (event.keyCode == 123u || event.keyCode == 125u) adjustment = -1;
    if (event.keyCode == 124u || event.keyCode == 126u) adjustment = 1;
    if (adjustment != 0) {
        [self adjustAccessibilityControl:_focusedControl
            direction:adjustment fine:(event.modifierFlags
                & NSEventModifierFlagShift) != 0u];
        return;
    }
    [super keyDown:event];
}

- (void)resetCursorRects
{
    [super resetCursorRects];
    for (NSRect rect : _tabRects) [self addCursorRect:rect
        cursor:NSCursor.pointingHandCursor];
    [self addCursorRect:_auditionRect cursor:NSCursor.pointingHandCursor];
    [self addCursorRect:_resetRect cursor:NSCursor.pointingHandCursor];
    for (NSRect rect : _presetRects)
        [self addCursorRect:rect cursor:NSCursor.pointingHandCursor];
    [self addCursorRect:_membraneRect cursor:NSCursor.crosshairCursor];
    for (NSRect rect : _parameterRects)
        [self addCursorRect:rect cursor:NSCursor.resizeLeftRightCursor];
}

@end

@interface S3GTrackerInstrumentWindowController () <NSWindowDelegate>
@property(nonatomic, strong) S3GTrackerMembraneRackView* rackView;
@end

@implementation S3GTrackerInstrumentWindowController

- (instancetype)initWithState:(TrackerViewState*)state
    callbacks:(WorkspaceCallbacks*)callbacks
{
    const NSRect frame = NSMakeRect(0.0, 0.0, 1120.0, 720.0);
    const NSWindowStyleMask style = NSWindowStyleMaskTitled
        | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
        | NSWindowStyleMaskResizable;
    NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:style backing:NSBackingStoreBuffered defer:NO];
    self = [super initWithWindow:window];
    if (self) {
        self.rackView = [[S3GTrackerMembraneRackView alloc]
            initWithState:state callbacks:callbacks];
        window.title = @"s3g Tracker — Membrane Kick";
        window.contentView = self.rackView;
        window.minSize = NSMakeSize(940.0, 630.0);
        window.backgroundColor = color(0x0c0c0c);
        window.appearance = [NSAppearance
            appearanceNamed:NSAppearanceNameDarkAqua];
        window.tabbingMode = NSWindowTabbingModeDisallowed;
        window.releasedWhenClosed = NO;
        window.delegate = self;
        window.acceptsMouseMovedEvents = YES;
        S3GTrackerRestoreWindowFrame(window,
            @"S3GTrackerInstrumentRackWindow");
    }
    return self;
}

- (void)reloadModel
{
    [self.rackView reloadModel];
}

- (void)showWindow:(id)sender
{
    [self reloadModel];
    [super showWindow:sender];
    [self.window makeKeyAndOrderFront:sender];
    [self.window makeFirstResponder:self.rackView];
    [NSApp activateIgnoringOtherApps:YES];
}

@end
