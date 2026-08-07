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
void S3GTrackerStyleTextEditor(NSTextField* field);
void S3GTrackerRestoreWindowFrame(NSWindow* window, NSString* autosaveName);

// Tags follow the existing s3g-dsp standalone convention:
// 0 = ordinary, 1 = live/toggled, 2 = danger.
@interface S3GTrackerActionButton : NSButton
@end

@interface S3GTrackerPopupButton : NSPopUpButton
@end

@interface S3GTrackerPanelView : NSView
@end
