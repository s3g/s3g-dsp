#import "s3g_tracker_help_window.h"

#import "s3g_tracker_controls.h"

#include "s3g/tracker/command.h"
#include "s3g/tracker/fx_catalog.h"

#include <string>
#include <string_view>

namespace {

NSString* stringFromView(std::string_view text)
{
    NSString* result = [[NSString alloc] initWithBytes:text.data()
        length:text.size() encoding:NSUTF8StringEncoding];
    return result ? result : @"";
}

NSFont* helpFont(CGFloat size, NSFontWeight weight)
{
    return S3GTrackerFont(size, weight);
}

NSMutableParagraphStyle* paragraph(CGFloat before, CGFloat after,
    CGFloat lineHeight)
{
    NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
    style.paragraphSpacingBefore = before;
    style.paragraphSpacing = after;
    style.lineSpacing = lineHeight;
    style.lineBreakMode = NSLineBreakByWordWrapping;
    return style;
}

NSAttributedString* helpDocument()
{
    NSMutableAttributedString* document = [[NSMutableAttributedString alloc]
        init];
    NSDictionary* introAttributes = @{
        NSFontAttributeName: helpFont(10.5, NSFontWeightRegular),
        NSForegroundColorAttributeName: S3GTrackerColor(0x92999b),
        NSParagraphStyleAttributeName: paragraph(0.0, 15.0, 3.0),
    };
    [document appendAttributedString:[[NSAttributedString alloc]
        initWithString:@"Lane and row addresses are one-based. Targets accept a lane number or @alias. Commands are case-insensitive and invalid input leaves the session unchanged.\n"
        attributes:introAttributes]];

    const auto& sections = s3g::tracker::CommandEngine::helpSections();
    for (const auto& section : sections) {
        const std::string_view visibleTitle = section.title;
        NSDictionary* sectionAttributes = @{
            NSFontAttributeName: helpFont(11.0, NSFontWeightSemibold),
            NSForegroundColorAttributeName: S3GTrackerThemeColor(
                S3GTrackerThemeRole::Focus),
            NSParagraphStyleAttributeName: paragraph(14.0, 7.0, 1.0),
            NSKernAttributeName: @0.8,
        };
        [document appendAttributedString:[[NSAttributedString alloc]
            initWithString:[stringFromView(visibleTitle)
                stringByAppendingString:@"\n"]
            attributes:sectionAttributes]];

        for (const auto& entry : section.entries) {
            std::string_view visibleSyntax = entry.syntax;
            std::string_view visibleDescription = entry.description;
            if (entry.syntax == "actions") {
                visibleDescription = "List the sequencing action keys accepted by SEQ1 and SEQ2.";
            } else if (entry.syntax == "demo") {
                visibleDescription = "Load the General MIDI tracker demonstration.";
            }
            NSDictionary* syntaxAttributes = @{
                NSFontAttributeName: helpFont(11.0, NSFontWeightMedium),
                NSForegroundColorAttributeName: S3GTrackerThemeColor(
                    S3GTrackerThemeRole::Note),
                NSParagraphStyleAttributeName: paragraph(5.0, 2.0, 1.5),
            };
            [document appendAttributedString:[[NSAttributedString alloc]
                initWithString:[stringFromView(visibleSyntax)
                    stringByAppendingString:@"\n"]
                attributes:syntaxAttributes]];

            NSDictionary* descriptionAttributes = @{
                NSFontAttributeName: helpFont(10.0, NSFontWeightRegular),
                NSForegroundColorAttributeName: S3GTrackerColor(0xb2b7b8),
                NSParagraphStyleAttributeName: paragraph(0.0, 4.0, 2.0),
            };
            [document appendAttributedString:[[NSAttributedString alloc]
                initWithString:[stringFromView(visibleDescription)
                    stringByAppendingString:@"\n"]
                attributes:descriptionAttributes]];
        }
    }

    NSDictionary* actionHeadingAttributes = @{
        NSFontAttributeName: helpFont(11.0, NSFontWeightSemibold),
        NSForegroundColorAttributeName: S3GTrackerThemeColor(
            S3GTrackerThemeRole::Focus),
        NSParagraphStyleAttributeName: paragraph(14.0, 7.0, 1.0),
        NSKernAttributeName: @0.8,
    };
    [document appendAttributedString:[[NSAttributedString alloc]
        initWithString:@"SEQUENCING ACTIONS\n"
        attributes:actionHeadingAttributes]];
    for (std::size_t index = 0u;
         index < s3g::tracker::sequencerActionCount(); ++index) {
        const auto* action = s3g::tracker::sequencerAction(index);
        if (!action) continue;
        NSString* line = [NSString stringWithFormat:@"%@  %@  —  %@\n",
            stringFromView(action->mnemonic),
            stringFromView(action->displayName).uppercaseString,
            stringFromView(action->valueMeaning)];
        NSDictionary* actionAttributes = @{
            NSFontAttributeName: helpFont(10.0, NSFontWeightRegular),
            NSForegroundColorAttributeName: S3GTrackerColor(0xb2b7b8),
            NSParagraphStyleAttributeName: paragraph(1.0, 3.0, 2.0),
        };
        [document appendAttributedString:[[NSAttributedString alloc]
            initWithString:line attributes:actionAttributes]];
    }

    NSDictionary* footerAttributes = @{
        NSFontAttributeName: helpFont(9.5, NSFontWeightRegular),
        NSForegroundColorAttributeName: S3GTrackerColor(0x777e80),
        NSParagraphStyleAttributeName: paragraph(17.0, 8.0, 2.0),
    };
    [document appendAttributedString:[[NSAttributedString alloc]
        initWithString:@"COLUMNS  NOTE · VOL · SEQ1 · V1 · SEQ2 · V2 are visible together · Double-click a column header to enter its independent length\nSEQUENCING  Right-click a SEQ1/SEQ2 cell to choose an action, or double-click and type its code\nROUTING  Click B01–B08 or CH01–CH16 in any lane header to set that track's MIDI bus and channel · Double-click the lane name to rename it\nVALUES  VOL and ordinary sequence values use normalized 0.000–1.000 · WRP uses library index 01–64\nWARPS  Compose EXP/STEP/EUCLID serially · SAVE/RECALL named project slots · WRP switches a slot at a row boundary\nDIRECTIONS  FORWARD (>) · REVERSE (<) · PALINDROME (<>) · RANDOM (?)\n\nTRANSPORT  Tempo follows REAPER · RATE selects 1/4×, 1/2×, 2/3×, 1×, 3/2×, 2×, or 4× · Space play/pause · Shift-Space loop\nTRACKER  Type a MIDI number or note name, then Return · Drag cells for a rectangle\nTRACKER MODIFIER  Control-A/C/X/V select all, copy, cut, paste · Control-=/−/0 zoom\nHOST SAFETY  Command-key combinations are not claimed by the tracker and remain available to REAPER\nNAVIGATE  Left/Right fields · Up/Down rows · Shift-Left/Right lanes · Page Up/Down · Home/End · F9–F12 jump to 0/25/50/75%\nLOOP REGION  Drag the row-number gutter or use Shift-Up/Down; the region applies to every column\nTOOLS  Geometry, Warps, and Console can be detached with ↗ or by double-clicking their page tab\n\nUse the scroll bar to navigate. Text is selectable and copyable.\n"
        attributes:footerAttributes]];
    return document;
}

} // namespace

@interface S3GTrackerHelpRootView : NSView
@end

@implementation S3GTrackerHelpRootView
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    [S3GTrackerColor(0x0c0c0c) setFill];
    NSRectFill(self.bounds);
    [S3GTrackerColor(0x131313) setFill];
    NSRectFill(NSMakeRect(0.0, NSMaxY(self.bounds) - 74.0,
        NSWidth(self.bounds), 74.0));
    [S3GTrackerColor(0x3d4142) setFill];
    NSRectFill(NSMakeRect(0.0, NSMaxY(self.bounds) - 75.0,
        NSWidth(self.bounds), 1.0));
}
@end

@interface S3GTrackerHelpTextView : NSTextView
@end

@implementation S3GTrackerHelpTextView
- (void)keyDown:(NSEvent*)event
{
    if (event.keyCode == 53u) {
        [self.window close];
        return;
    }
    [super keyDown:event];
}
@end

@interface S3GTrackerConsoleHelpWindowController () <NSWindowDelegate>
@property(nonatomic, strong) S3GTrackerHelpTextView* helpTextView;
@end

@implementation S3GTrackerConsoleHelpWindowController

+ (instancetype)sharedController
{
    static S3GTrackerConsoleHelpWindowController* controller = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        controller = [[S3GTrackerConsoleHelpWindowController alloc] init];
    });
    return controller;
}

- (instancetype)init
{
    const NSWindowStyleMask style = NSWindowStyleMaskTitled
        | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
        | NSWindowStyleMaskResizable;
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0.0, 0.0, 760.0, 720.0)
                  styleMask:style
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self = [super initWithWindow:window];
    if (!self) return nil;

    window.title = @"s3g Tracker — Console Help";
    window.minSize = NSMakeSize(570.0, 430.0);
    window.releasedWhenClosed = NO;
    window.delegate = self;
    window.tabbingMode = NSWindowTabbingModeDisallowed;
    window.backgroundColor = S3GTrackerColor(0x0c0c0c);
    window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    S3GTrackerRestoreWindowFrame(window, @"S3GTrackerConsoleHelpWindow");

    S3GTrackerHelpRootView* root = [[S3GTrackerHelpRootView alloc]
        initWithFrame:window.contentView.bounds];
    window.contentView = root;

    NSTextField* title = [NSTextField labelWithString:@"CONSOLE HELP"];
    title.translatesAutoresizingMaskIntoConstraints = NO;
    title.font = helpFont(18.0, NSFontWeightMedium);
    title.textColor = S3GTrackerColor(0xa8a8a8);
    title.accessibilityRoleDescription = @"Console help heading";
    [root addSubview:title];

    NSTextField* subtitle = [NSTextField
        labelWithString:@"COMPLETE NATIVE COMMAND LANGUAGE  /  EMBEDDED HELP PAGE"];
    subtitle.translatesAutoresizingMaskIntoConstraints = NO;
    subtitle.font = helpFont(9.0, NSFontWeightMedium);
    subtitle.textColor = S3GTrackerColor(0x737a7c);
    [root addSubview:subtitle];

    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.hasVerticalScroller = YES;
    scroll.hasHorizontalScroller = NO;
    scroll.autohidesScrollers = YES;
    scroll.scrollerStyle = NSScrollerStyleOverlay;
    scroll.scrollerKnobStyle = NSScrollerKnobStyleLight;
    scroll.borderType = NSNoBorder;
    scroll.drawsBackground = YES;
    scroll.backgroundColor = S3GTrackerColor(0x101214);
    scroll.accessibilityLabel = @"Console command reference";
    [root addSubview:scroll];

    NSSize contentSize = scroll.contentSize;
    _helpTextView = [[S3GTrackerHelpTextView alloc]
        initWithFrame:NSMakeRect(0.0, 0.0, contentSize.width,
            contentSize.height)];
    _helpTextView.minSize = NSMakeSize(0.0, contentSize.height);
    _helpTextView.maxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
    _helpTextView.verticallyResizable = YES;
    _helpTextView.horizontallyResizable = NO;
    _helpTextView.autoresizingMask = NSViewWidthSizable;
    _helpTextView.textContainer.containerSize = NSMakeSize(
        contentSize.width, CGFLOAT_MAX);
    _helpTextView.textContainer.widthTracksTextView = YES;
    _helpTextView.textContainerInset = NSMakeSize(25.0, 22.0);
    _helpTextView.drawsBackground = YES;
    _helpTextView.backgroundColor = S3GTrackerColor(0x101214);
    _helpTextView.editable = NO;
    _helpTextView.selectable = YES;
    _helpTextView.richText = YES;
    _helpTextView.importsGraphics = NO;
    _helpTextView.usesFindBar = YES;
    _helpTextView.automaticLinkDetectionEnabled = NO;
    _helpTextView.automaticDataDetectionEnabled = NO;
    _helpTextView.insertionPointColor = S3GTrackerColor(0xd0d0d0);
    _helpTextView.selectedTextAttributes = @{
        NSBackgroundColorAttributeName: S3GTrackerColor(0x4a4a4a),
        NSForegroundColorAttributeName: S3GTrackerColor(0xf2f2f2),
    };
    _helpTextView.accessibilityLabel = @"All console commands, grouped by function";
    [_helpTextView.textStorage setAttributedString:helpDocument()];
    scroll.documentView = _helpTextView;

    [NSLayoutConstraint activateConstraints:@[
        [title.leadingAnchor constraintEqualToAnchor:root.leadingAnchor
            constant:24.0],
        [title.topAnchor constraintEqualToAnchor:root.topAnchor constant:13.0],
        [subtitle.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
        [subtitle.topAnchor constraintEqualToAnchor:title.bottomAnchor
            constant:3.0],
        [scroll.leadingAnchor constraintEqualToAnchor:root.leadingAnchor
            constant:14.0],
        [scroll.trailingAnchor constraintEqualToAnchor:root.trailingAnchor
            constant:-14.0],
        [scroll.topAnchor constraintEqualToAnchor:root.topAnchor constant:86.0],
        [scroll.bottomAnchor constraintEqualToAnchor:root.bottomAnchor
            constant:-14.0],
    ]];
    return self;
}

- (void)showWindow:(id)sender
{
    [super showWindow:sender];
    if (self.window.miniaturized) [self.window deminiaturize:sender];
    [self.window makeKeyAndOrderFront:sender];
    [self.window makeFirstResponder:self.helpTextView];
    [NSApp activateIgnoringOtherApps:YES];
}

@end
