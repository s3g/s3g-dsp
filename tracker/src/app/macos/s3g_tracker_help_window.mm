#import "s3g_tracker_help_window.h"

#import "s3g_tracker_controls.h"

#include "s3g_gui_layout.h"
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

void appendSectionRule(NSMutableAttributedString* document,
    CGFloat spacingBefore = 5.0)
{
    NSDictionary* attributes = @{
        NSFontAttributeName: helpFont(7.5, NSFontWeightRegular),
        NSForegroundColorAttributeName: S3GTrackerColor(0x45494a),
        NSParagraphStyleAttributeName: paragraph(spacingBefore, 2.0, 0.0),
        NSKernAttributeName: @0.15,
    };
    [document appendAttributedString:[[NSAttributedString alloc]
        initWithString:
            @"────────────────────────────────────────────────────────────────────────\n"
        attributes:attributes]];
}

void appendGuideSection(NSMutableAttributedString* document,
    NSString* title, NSArray<NSArray<NSString*>*>* entries)
{
    appendSectionRule(document, 7.0);
    NSDictionary* headingAttributes = @{
        NSFontAttributeName: helpFont(9.5, NSFontWeightSemibold),
        NSForegroundColorAttributeName: S3GTrackerThemeColor(
            S3GTrackerThemeRole::Focus),
        NSParagraphStyleAttributeName: paragraph(1.0, 3.0, 0.5),
        NSKernAttributeName: @0.65,
    };
    [document appendAttributedString:[[NSAttributedString alloc]
        initWithString:[title stringByAppendingString:@"\n"]
        attributes:headingAttributes]];

    for (NSArray<NSString*>* entry in entries) {
        if (entry.count < 2u) continue;
        NSDictionary* labelAttributes = @{
            NSFontAttributeName: helpFont(8.5, NSFontWeightSemibold),
            NSForegroundColorAttributeName: S3GTrackerThemeColor(
                S3GTrackerThemeRole::Note),
            NSParagraphStyleAttributeName: paragraph(2.0, 0.0, 0.25),
            NSKernAttributeName: @0.35,
        };
        [document appendAttributedString:[[NSAttributedString alloc]
            initWithString:[entry[0u] stringByAppendingString:@"\n"]
            attributes:labelAttributes]];

        NSMutableParagraphStyle* bodyParagraph = paragraph(0.0, 2.0, 0.5);
        bodyParagraph.firstLineHeadIndent = 10.0;
        bodyParagraph.headIndent = 10.0;
        bodyParagraph.tailIndent = -4.0;
        NSDictionary* bodyAttributes = @{
            NSFontAttributeName: helpFont(8.25, NSFontWeightRegular),
            NSForegroundColorAttributeName: S3GTrackerColor(0x92999b),
            NSParagraphStyleAttributeName: bodyParagraph,
        };
        [document appendAttributedString:[[NSAttributedString alloc]
            initWithString:[entry[1u] stringByAppendingString:@"\n"]
            attributes:bodyAttributes]];
    }
}

NSAttributedString* helpDocument()
{
    NSMutableAttributedString* document = [[NSMutableAttributedString alloc]
        init];
    NSDictionary* introAttributes = @{
        NSFontAttributeName: helpFont(9.0, NSFontWeightRegular),
        NSForegroundColorAttributeName: S3GTrackerColor(0x92999b),
        NSParagraphStyleAttributeName: paragraph(0.0, 4.0, 1.0),
    };
    [document appendAttributedString:[[NSAttributedString alloc]
        initWithString:@"Lane and row addresses are one-based. Targets accept a lane number or @alias. Commands are case-insensitive and invalid input leaves the session unchanged.\n"
        attributes:introAttributes]];

    const auto& sections = s3g::tracker::CommandEngine::helpSections();
    for (const auto& section : sections) {
        appendSectionRule(document);
        const std::string_view visibleTitle = section.title;
        NSDictionary* sectionAttributes = @{
            NSFontAttributeName: helpFont(9.5, NSFontWeightSemibold),
            NSForegroundColorAttributeName: S3GTrackerThemeColor(
                S3GTrackerThemeRole::Focus),
            NSParagraphStyleAttributeName: paragraph(1.0, 3.0, 0.5),
            NSKernAttributeName: @0.65,
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
                NSFontAttributeName: helpFont(9.5, NSFontWeightMedium),
                NSForegroundColorAttributeName: S3GTrackerThemeColor(
                    S3GTrackerThemeRole::Note),
                NSParagraphStyleAttributeName: paragraph(2.0, 0.0, 0.5),
            };
            [document appendAttributedString:[[NSAttributedString alloc]
                initWithString:[stringFromView(visibleSyntax)
                    stringByAppendingString:@"\n"]
                attributes:syntaxAttributes]];

            NSDictionary* descriptionAttributes = @{
                NSFontAttributeName: helpFont(8.75, NSFontWeightRegular),
                NSForegroundColorAttributeName: S3GTrackerColor(0xb2b7b8),
                NSParagraphStyleAttributeName: paragraph(0.0, 1.0, 0.75),
            };
            [document appendAttributedString:[[NSAttributedString alloc]
                initWithString:[stringFromView(visibleDescription)
                    stringByAppendingString:@"\n"]
                attributes:descriptionAttributes]];

            NSDictionary* exampleLabelAttributes = @{
                NSFontAttributeName: helpFont(8.75, NSFontWeightMedium),
                NSForegroundColorAttributeName: S3GTrackerColor(0xb2b7b8),
                NSParagraphStyleAttributeName: paragraph(0.0, 2.0, 0.75),
            };
            NSDictionary* exampleCommandAttributes = @{
                NSFontAttributeName: helpFont(8.75, NSFontWeightMedium),
                NSForegroundColorAttributeName: S3GTrackerThemeColor(
                    S3GTrackerThemeRole::Value),
                NSParagraphStyleAttributeName: paragraph(0.0, 2.0, 0.75),
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

    appendSectionRule(document, 7.0);
    NSDictionary* actionHeadingAttributes = @{
        NSFontAttributeName: helpFont(9.5, NSFontWeightSemibold),
        NSForegroundColorAttributeName: S3GTrackerThemeColor(
            S3GTrackerThemeRole::Focus),
        NSParagraphStyleAttributeName: paragraph(1.0, 3.0, 0.5),
        NSKernAttributeName: @0.65,
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
            NSFontAttributeName: helpFont(8.75, NSFontWeightRegular),
            NSForegroundColorAttributeName: S3GTrackerColor(0xb2b7b8),
            NSParagraphStyleAttributeName: paragraph(0.0, 1.0, 0.75),
        };
        [document appendAttributedString:[[NSAttributedString alloc]
            initWithString:line attributes:actionAttributes]];
    }
    NSDictionary* midiCcAttributes = @{
        NSFontAttributeName: helpFont(8.75, NSFontWeightRegular),
        NSForegroundColorAttributeName: S3GTrackerColor(0xb2b7b8),
        NSParagraphStyleAttributeName: paragraph(0.0, 1.0, 0.75),
    };
    [document appendAttributedString:[[NSAttributedString alloc]
        initWithString:@"CC0–CC127  MIDI CONTROL CHANGE  —  MIDI value 0–127; STEP or LINEAR between visited rows\n"
        attributes:midiCcAttributes]];

    appendGuideSection(document, @"TRACKER GRID WORKFLOW", @[
        @[ @"COLUMNS", @"Compact lanes show NOTE and VOL. EXPAND DETAIL reveals SEQ1, V1, SEQ2, V2, and GATE. Double-click a length to enter a stride such as 24x2; double-click READ to set its one-based starting row." ],
        @[ @"NOTE DISPLAY", @"NOTE: NAME shows pitches such as C-4. NOTE: MIDI shows the same stored pitch as decimal value 60." ],
        @[ @"CELL SYMBOLS", @"NOTE: --- rest, RPT retrigger previous, HLD continue the active note, KIL kill, B01–B64 reusable project-wide sub-row Burst. VOL: DEF default. VOL/SEQ: PRV previous." ],
        @[ @"QUICK ENTRY", @"In NOTE, X toggles an anchored hit; R writes RPT; H writes HLD; K writes KIL. Delete clears the active cell or drag selection. [ and ] adjust VOL/V values. M toggles the selected column mute." ],
        @[ @"SEQUENCING + VALUES", @"Right-click SEQ1/SEQ2 to choose an action or CC, or double-click and type its code. VOL and sequence values use 0.000–1.000; CC also accepts 0–127. An MT value may be a plus-separated stack aligned with the NOTE voices; one MT value broadcasts. Other SEQ actions remain lane-wide. STP holds values; LIN emits bounded intermediate CC values." ],
        @[ @"SELECTION + HISTORY", @"Drag cells for a rectangle. Control-Shift-Arrows extends a keyboard rectangle. Control-A/C/X/V selects all, copies, cuts, and pastes. Control-Z and Control-Shift-Z undo and redo Tracker states; Command-Z remains REAPER's." ],
        @[ @"NAVIGATION", @"Left/Right moves between fields; Up/Down moves by JUMP; Shift-Left/Right moves lanes. Control-Option-1…0 sets JUMP 1…10. Page Up/Down, Home/End, and F9–F12 jump through the pattern. Control-=/−/0 changes zoom. < and > switches panels." ],
        @[ @"PITCH SHORTCUTS", @"Control-Option-Up/Down transposes one selected NOTE column by a semitone; add Shift for an octave." ],
        @[ @"PHRASES", @"Select 2–64 rows in one Tracker lane, then right-click and choose PHRASE > CAPTURE SELECTION; Control-Shift-P is the shortcut. Referenced Bursts retain their project-wide slot identity. Control-P copies the selected Phrase at the cursor. The Phrase page uses Tracker's own inline NOTE/VOL/SEQ/GATE entry, with VOL shown as normalized 0.000–1.000. Drag cells or Shift-click within a column to select a range; Control-Shift-Arrows extends it. Control-A/C/X/V selects all, copies, cuts, and pastes compatible Phrase cells while retaining their row/column shape. Right-click SEQ1/SEQ2 for the action and MIDI CC menu; right-click a CD value for named conditions. PREVIEW CH routes stopped-transport auditioning without changing the destination lane, while the green playhead advances at project BPM. Placement copies cells directly and creates no hidden link." ],
        @[ @"DIRECTIONS + LOOP", @"FORWARD (>), REVERSE (<), PALINDROME (<>), RANDOM (header: RND). Drag the fixed row-number gutter or use Shift-Up/Down to set the global loop region." ],
    ]);
    appendGuideSection(document, @"MIDI + LANE ROUTING", @[
        @[ @"ROUTING", @"Tracker exposes one CLAP MIDI output and one record input. CH01–CH16 sets each lane's output channel. Use another Tracker instance for more than 16 destinations. Double-click a lane name to rename it." ],
        @[ @"MIDI RECORD", @"REC LANE fixes the recording destination independently of the editing cursor. OFF disarms. STEP collects held keys into one NOTE/VOL chord and advances by View JUMP once the last key is released. LIVE Q groups notes arriving on the same row, then writes shared HLD rows and KIL release. LIVE MT writes a pitch-aligned MT value stack, so every recorded chord voice keeps its own measured onset offset and attack velocity. Live modes require REAPER playback and pattern transport." ],
        @[ @"NOTE + VOL + GATE STACKS", @"Double-click NOTE to enter up to eight plus-separated pitches, for example 60+64+67. VOL and GATE may contain one value broadcast to every pitch or per-note values in the same order. GATE accepts DEF, a row duration from 0.01 through 64, and TIE. If fewer values are entered, the final value repeats; extra values are ignored." ],
        @[ @"ALIASES", @"aliases lists bindings by lane. alias name 3 assigns or reassigns @name. autoalias rebuilds the map with the shortest available prefix of each lane name." ],
    ]);
    appendGuideSection(document, @"TRANSPORT + SONG", @[
        @[ @"TRANSPORT", @"Tempo follows REAPER. RATE selects 1/4×, 1/2×, 2/3×, 1×, 3/2×, 2×, or 4×. Space plays/pauses; Shift-Space toggles loop. SYNC ALL restarts every lane and column at row 1 without moving REAPER." ],
        @[ @"SONG ROW LENGTH", @"TICKS is the number of tracker-row advances in one pass. FULL / 1× always selects the complete longest pattern column, or the active Loop In–Out span. Fixed musical intervals extend through 256 ticks. REP adds passes while column phase continues." ],
        @[ @"SONG ROW EDIT", @"ADD inserts after the selection. DUP makes an exact copy. The ↑ and ↓ controls move the selected row. Arrangement edits remain available while REAPER runs; Tracker hands the updated arrangement to playback safely at the next Song-row boundary without creating a selected-row queue." ],
        @[ @"SONG ENERGY", @"EN % sets the intensity of each Song row. Tracker SEQ action EN plays a note or Burst only when the row energy meets its threshold; ordinary Pattern playback supplies 100%. Use different EN thresholds across a pattern to reveal progressively denser versions from one pattern." ],
        @[ @"SONG QUEUE", @"Select a target row, choose NEXT TICK, NEXT BEAT, END OF PASS, or END OF ROW, then SELECT QUEUE. END OF ROW is the default and waits through all repetitions. Queue remains available while REAPER runs with Song Transport on, whether LOOP SONG is on or off, and can relaunch after a non-looping Song ends. The pending row is yellow; pattern, warp, swing, and mutes switch together." ],
        @[ @"LOOP SONG", @"LOOP SONG can be switched on or off while REAPER is running. The current Song row keeps its position; the new loop rule takes effect when playback reaches the end of the arrangement." ],
        @[ @"SONG FILE", @"SAVE/LOAD SONG + PATTERNS uses one complete validated .s3gt project file." ],
        @[ @"WARPS", @"Compose EXP, STEP, and EUCLID serially; SAVE/RECALL named project slots. During playback the diagram traces the active curve and marks its current cycle step; Song WARP follows the saved slot selected by the sounding row." ],
    ]);
    appendGuideSection(document, @"GEOMETRY + TOOL WINDOWS", @[
        @[ @"GEOMETRY", @"ACTIVE PULSES shows authored notes; ALL STEPS is the reference; PHASE SPOKES shows live position; LANE FOCUS isolates a lane; COMPOSITE RING normalizes cycles. Pattern and active Song-row NOTE mutes retain their fixed ring positions as dark dashed M placeholders; their pulses and playheads are hidden." ],
        @[ @"TOOLS + LIVE CODE", @"Geometry, Warps, Console, and Help detach with ↗ or a double-click on the page tab. Detached Console keeps its Live Code line. Press : or backtick to focus Live Code; Escape returns to the grid." ],
        @[ @"HOST SAFETY", @"Command-key combinations are not claimed by Tracker and remain available to REAPER. ? opens Help." ],
        @[ @"READING HELP", @"Use the scroll bar to navigate. All text is selectable and copyable." ],
    ]);
    return document;
}

} // namespace

@interface S3GTrackerHelpRootView : NSView
@end

@implementation S3GTrackerHelpRootView
- (BOOL)isFlipped { return YES; }

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Canvas) setFill];
    NSRectFill(self.bounds);
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
@property(nonatomic, strong) S3GTrackerToolboxView* helpPanel;
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

    _helpPanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
    _helpPanel.translatesAutoresizingMaskIntoConstraints = NO;
    _helpPanel.toolboxIndex = 0;
    _helpPanel.toolboxTitle = @"HELP / COMMAND REFERENCE";
    _helpPanel.accessibilityElement = YES;
    _helpPanel.accessibilityRole = NSAccessibilityGroupRole;
    _helpPanel.accessibilityLabel = @"Help command reference panel";
    [root addSubview:_helpPanel];

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
    [_helpPanel addSubview:scroll];

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
    _helpTextView.textContainerInset = NSMakeSize(18.0, 14.0);
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
        [_helpPanel.leadingAnchor constraintEqualToAnchor:root.leadingAnchor
            constant:s3g::gui_layout::kTrackerPageHorizontalInset],
        [_helpPanel.trailingAnchor constraintEqualToAnchor:root.trailingAnchor
            constant:-s3g::gui_layout::kTrackerPageHorizontalInset],
        [_helpPanel.topAnchor constraintEqualToAnchor:root.topAnchor
            constant:s3g::gui_layout::kTrackerPageContentTop],
        [_helpPanel.bottomAnchor constraintEqualToAnchor:root.bottomAnchor
            constant:-s3g::gui_layout::kTrackerPageBottomInset],
        [scroll.leadingAnchor constraintEqualToAnchor:_helpPanel.leadingAnchor
            constant:8.0],
        [scroll.trailingAnchor constraintEqualToAnchor:_helpPanel.trailingAnchor
            constant:-8.0],
        [scroll.topAnchor constraintEqualToAnchor:_helpPanel.topAnchor
            constant:s3g::gui_layout::kStandardMetrics.headerHeight + 8.0],
        [scroll.bottomAnchor constraintEqualToAnchor:_helpPanel.bottomAnchor
            constant:-8.0],
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
