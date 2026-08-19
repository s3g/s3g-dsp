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

            NSDictionary* exampleLabelAttributes = @{
                NSFontAttributeName: helpFont(10.5, NSFontWeightMedium),
                NSForegroundColorAttributeName: S3GTrackerColor(0xb2b7b8),
                NSParagraphStyleAttributeName: paragraph(0.0, 6.0, 2.5),
            };
            NSDictionary* exampleCommandAttributes = @{
                NSFontAttributeName: helpFont(10.5, NSFontWeightMedium),
                NSForegroundColorAttributeName: S3GTrackerThemeColor(
                    S3GTrackerThemeRole::Live),
                NSParagraphStyleAttributeName: paragraph(0.0, 6.0, 2.5),
            };
            [document appendAttributedString:[[NSAttributedString alloc]
                initWithString:@"EXAMPLE  "
                attributes:exampleLabelAttributes]];
            [document appendAttributedString:[[NSAttributedString alloc]
                initWithString:[stringFromView(entry.example)
                    stringByAppendingString:@"\n"]
                attributes:exampleCommandAttributes]];
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
    NSDictionary* midiCcAttributes = @{
        NSFontAttributeName: helpFont(10.0, NSFontWeightRegular),
        NSForegroundColorAttributeName: S3GTrackerColor(0xb2b7b8),
        NSParagraphStyleAttributeName: paragraph(1.0, 3.0, 2.0),
    };
    [document appendAttributedString:[[NSAttributedString alloc]
        initWithString:@"CC0–CC127  MIDI CONTROL CHANGE  —  MIDI value 0–127; STEP or LINEAR between visited rows\n"
        attributes:midiCcAttributes]];

    NSDictionary* footerAttributes = @{
        NSFontAttributeName: helpFont(9.5, NSFontWeightRegular),
        NSForegroundColorAttributeName: S3GTrackerColor(0x777e80),
        NSParagraphStyleAttributeName: paragraph(17.0, 8.0, 2.0),
    };
    [document appendAttributedString:[[NSAttributedString alloc]
        initWithString:
            @"COLUMNS  Compact lanes show NOTE · VOL · EXPAND SEQ reveals SEQ1 · V1 · SEQ2 · V2 · Double-click a column length to edit it\n"
            @"NOTE VIEW  NOTE: NAME shows pitches such as C-4 · NOTE: MIDI shows the same stored pitch as decimal value 60\n"
            @"CELL TEXT  NOTE --- rest · RPT retrigger previous · HLD continue active note · KIL kill active note · VOL DEF default · VOL/SEQ PRV previous\n"
            @"SEQUENCING  Right-click a SEQ1/SEQ2 cell to choose an action or CC number, or double-click and type its code · Click V1 STP/LIN or V2 STP/LIN to switch interpolation\n"
            @"ROUTING  Tracker exposes one CLAP MIDI output plus one record input · Click CH01–CH16 in a lane header to set that track's output channel · Use another Tracker instance for more than 16 destinations · Double-click the lane name to rename it\n"
            @"MIDI RECORD  OFF disarms · STEP, LIVE Q, and LIVE MT immediately monitor incoming note-on/off on the selected lane's MIDI channel · STEP writes note/velocity at the cursor, clears MT there, and advances one row · LIVE Q records the onset at the nearest playback row, then writes HLD rows and a KIL release from the physical note-off · LIVE MT also writes measured onset and release offsets into available SEQ pairs · Live recording preserves lane lengths and moves the cursor highlight to each written boundary without advancing · Live modes require REAPER playback and pattern transport\n"
            @"ALIASES  aliases groups bindings by lane · alias name 3 assigns or reassigns @name · autoalias replaces the map with the shortest available prefix of every lane name (k, then ki, then kit as needed)\n"
            @"HISTORY  UNDO/REDO buttons or undo/redo commands restore persistent Tracker states · Control-Z undo · Control-Shift-Z redo · Command-Z remains REAPER's\n"
            @"VALUES  VOL and sequence values use normalized 0.000–1.000 · CC values also accept MIDI integers 0–127 (use 1.0 for normalized maximum) · STEP holds between rows · LINEAR emits bounded intermediate CC values\n"
            @"WARPS  Compose EXP/STEP/EUCLID serially · SAVE/RECALL named project slots · Song WARP selects OFF or a saved slot per row\n"
            @"DIRECTIONS  FORWARD (>) · REVERSE (<) · PALINDROME (<>) · RANDOM (write random; header displays RND) · ? opens Help\n"
            @"\nTRANSPORT  Tempo follows REAPER · RATE selects 1/4×, 1/2×, 2/3×, 1×, 3/2×, 2×, or 4× · Space play/pause · Shift-Space loop · SYNC ALL forces every lane and column to row 1 without moving REAPER and ignores phase for that launch\n"
            @"SONG QUEUE  Select a target row · Choose NEXT TICK/BEAT/CYCLE/SONG ROW · QUEUE SELECTED · The pending row and boundary appear in yellow · Pattern, warp, swing, and mutes switch together on launch\n"
            @"SONG FILE  SAVE/LOAD SONG + PATTERNS uses a complete validated .s3gt project file\n"
            @"GEOMETRY VIEW  ACTIVE PULSES original · ALL STEPS reference · PHASE SPOKES live position · LANE FOCUS selected lane · COMPOSITE RING normalized cycle · Pattern NOTE mutes and active Song-row lane mutes are omitted\n"
            @"TRACKER  Type a MIDI number or note name, then Return · Drag cells for a rectangle\n"
            @"QUICK ENTRY  NOTE X toggles an anchored hit · R writes RPT · H writes HLD · K writes KIL · Delete clears the active cell or every cell in a drag selection · [ and ] adjust VOL/V values · M toggles the selected column mute\n"
            @"TRACKER MODIFIER  Control-A/C/X/V select all, copy, cut, paste · Control-Z/Shift-Z undo/redo · Control-=/−/0 zoom\n"
            @"HOST SAFETY  Command-key combinations are not claimed by the tracker and remain available to REAPER\n"
            @"NAVIGATE  Left/Right fields · Up/Down rows · Shift-Left/Right lanes · Page Up/Down · Home/End · F9–F12 jump to 0/25/50/75%\n"
            @"LOOP REGION  Drag the row-number gutter or use Shift-Up/Down; the region applies to every column\n"
            @"TOOLS  Geometry, Warps, Console, and Help can be detached with ↗ or by double-clicking their page tab · Detached Console retains its own Live Code line\n"
            @"\nUse the scroll bar to navigate. Text is selectable and copyable.\n"
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
