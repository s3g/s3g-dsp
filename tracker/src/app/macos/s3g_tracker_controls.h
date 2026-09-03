#pragma once

#import <Cocoa/Cocoa.h>

#include <cstdint>

// Night Tracker is a semantic palette, not a bag of per-view literals.  The
// roles keep navigation, playback, data columns, and warnings recognizable in
// every editor while neutral tones remain quiet.
enum class S3GTrackerThemeRole : std::uint8_t {
    Canvas,
    Workspace,
    Panel,
    Raised,
    Control,
    ControlHover,
    Selection,
    GridPlayback,
    GridPlaybackAccent,
    GridSelection,
    GridCursor,
    Grid,
    Border,
    BorderStrong,
    TextPrimary,
    TextSecondary,
    TextMuted,
    TextFaint,
    Focus,
    Note,
    Instrument,
    Value,
    Live,
    Success,
    Warning,
    Danger,
};

std::uint32_t S3GTrackerThemeRGB(S3GTrackerThemeRole role);
NSColor* S3GTrackerThemeColor(S3GTrackerThemeRole role,
    CGFloat alpha = 1.0);

// Near-neutral inputs are interpreted as relative tonal ranks and translated
// through the active Night Tracker ramp. Chromatic literals remain literal.
// This keeps older drawing code coherent while new code uses semantic roles.
NSColor* S3GTrackerColor(std::uint32_t rgb, CGFloat alpha = 1.0);
NSFont* S3GTrackerFont(CGFloat size = 10.0,
    NSFontWeight weight = NSFontWeightRegular);

void S3GTrackerStyleTextField(NSTextField* field,
    NSTextAlignment alignment = NSTextAlignmentRight);
void S3GTrackerStyleSuiteTextField(NSTextField* field,
    NSTextAlignment alignment = NSTextAlignmentRight);
void S3GTrackerStyleTextEditor(NSTextField* field);
void S3GTrackerRestoreWindowFrame(NSWindow* window, NSString* autosaveName);

// One visual contract for compact action buttons, including controls drawn
// directly into the Geometry/Burst canvas. The AppKit button subclass below
// delegates to this renderer so neither implementation can drift.
void S3GTrackerDrawSuiteActionButton(NSRect bounds, NSString* title,
    BOOL enabled, BOOL pressed, BOOL hovered, BOOL live,
    BOOL positive, BOOL binaryOff, BOOL danger, BOOL neutralTitle);

// Tags follow the existing s3g-dsp standalone convention:
// 0 = ordinary, 1 = live/toggled, 2 = danger.
@interface S3GTrackerActionButton : NSButton
@property(nonatomic) BOOL s3gUsesSuiteStyle;
// Keep semantic state in the button fill and border while drawing every
// enabled title at the same suite-gray level. Compact action banks use this
// when colored titles would make neighboring labels appear mismatched.
@property(nonatomic) BOOL s3gUsesNeutralTitle;
@end

@interface S3GTrackerPopupButton : NSPopUpButton
// Song and Warps opt into the suite-wide in-canvas dropdown contract. The
// Tracker grid leaves this disabled so its established native tracker menu
// behavior remains unchanged.
@property(nonatomic) BOOL s3gUsesCanvasMenu;
@end

// Labels beside suite controls use the same regular Menlo face and vertical
// centering as values inside the custom canvas menus. Tracker grid text keeps
// its separate IBM Plex treatment.
@interface S3GTrackerSuiteLabel : NSTextField
@end

// Background containers accept first responder so clicking unused space ends
// native text editing and clears the selection highlight. This is especially
// important in an embedded plug-in, where the host window otherwise keeps the
// AppKit field editor active indefinitely.
@interface S3GTrackerFocusReleaseView : NSView
@end

@interface S3GTrackerFocusReleaseStackView : NSStackView
@end

// Tracker-style numeric entry: drag vertically for continuous adjustment,
// or click/double-click to retain ordinary direct text entry. The explicit
// range and increment keep the interaction independent of locale-sensitive
// NSNumberFormatter internals.
@interface S3GTrackerDragNumberField : NSTextField
@property(nonatomic) double s3gMinimumValue;
@property(nonatomic) double s3gMaximumValue;
@property(nonatomic) double s3gDragIncrement;
@property(nonatomic) NSUInteger s3gFractionDigits;
- (double)s3gValueFromStart:(double)start
    verticalDelta:(CGFloat)verticalDelta
    modifierFlags:(NSEventModifierFlags)modifierFlags;
@end

// Full-width suite processor slider with an integrated value readout. It
// inherits vertical number dragging and adds direct left/right track dragging.
@interface S3GTrackerProcessorSliderField : S3GTrackerDragNumberField
@end

// One constructor-level contract for native Tracker pages that host the
// suite's processor slider drawing in an AppKit control.
void S3GTrackerConfigureProcessorSlider(
    S3GTrackerProcessorSliderField* slider,
    double minimum, double maximum, NSUInteger fractionDigits,
    id target, SEL action);

// Shared compact Swing control used by both the Tracker transport and Song
// rows. It follows the suite's thin-track/value treatment, publishes once on
// mouse-up, supports wheel adjustment, and can optionally represent an
// inherited (non-override) value.
@interface S3GTrackerSwingSlider : NSControl
@property(nonatomic, copy) NSString* s3gLabel;
@property(nonatomic) double s3gSwingValue;
@property(nonatomic) BOOL s3gHasOverride;
- (void)setSwingValue:(double)value hasOverride:(BOOL)hasOverride;
- (void)resetToBase;
- (BOOL)adjustByScrollDelta:(CGFloat)delta
    modifierFlags:(NSEventModifierFlags)modifierFlags;
@end

@interface S3GTrackerPanelView : S3GTrackerFocusReleaseView
@end

// Standard Tracker toolbox container. Its geometry and drawing mirror the
// shared s3g-dsp 21 px static header contract; callers position it with the
// family layouts in s3g_gui_layout.h.
@interface S3GTrackerToolboxView : S3GTrackerFocusReleaseView
@property(nonatomic, copy) NSString* toolboxTitle;
@property(nonatomic) NSInteger toolboxIndex;
@end
