#import "s3g_song_window.h"
#import "s3g_tracker_controls.h"

#include <algorithm>

namespace {

NSColor* s3gSongColor(unsigned rgb, CGFloat alpha = 1.0)
{
    return S3GTrackerColor(rgb, alpha);
}

NSFont* s3gSongFont(CGFloat size, NSFontWeight weight = NSFontWeightRegular)
{
    return S3GTrackerFont(size, weight);
}

NSString* const S3GSongColumnRow = @"row";
NSString* const S3GSongColumnPattern = @"pattern";
NSString* const S3GSongColumnRepeats = @"repeats";
NSString* const S3GSongColumnBPM = @"bpm";
NSString* const S3GSongColumnTicks = @"ticks";
NSString* const S3GSongColumnSwing = @"swing";
NSString* const S3GSongColumnMutes = @"mutes";
NSString* const S3GSongColumnDelete = @"delete";

NSInteger s3gClampInteger(NSInteger value, NSInteger low, NSInteger high)
{
    return std::min(high, std::max(low, value));
}

double s3gClampDouble(double value, double low, double high)
{
    return std::min(high, std::max(low, value));
}

bool s3gScanInteger(NSString* text, NSInteger& value)
{
    NSString* trimmed = [text stringByTrimmingCharactersInSet:
        [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSScanner* scanner = [NSScanner scannerWithString:trimmed];
    scanner.charactersToBeSkipped = [NSCharacterSet whitespaceCharacterSet];
    NSInteger candidate = 0;
    if (![scanner scanInteger:&candidate]) return false;
    if (!scanner.isAtEnd) return false;
    value = candidate;
    return true;
}

bool s3gScanDouble(NSString* text, double& value)
{
    NSString* trimmed = [text stringByTrimmingCharactersInSet:
        [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSScanner* scanner = [NSScanner scannerWithString:trimmed];
    scanner.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
    scanner.charactersToBeSkipped = [NSCharacterSet whitespaceCharacterSet];
    double candidate = 0.0;
    if (![scanner scanDouble:&candidate]) return false;
    if (!scanner.isAtEnd) return false;
    value = candidate;
    return true;
}

bool s3gUsesProjectValue(NSString* text)
{
    NSString* trimmed = [[text stringByTrimmingCharactersInSet:
        [NSCharacterSet whitespaceAndNewlineCharacterSet]] lowercaseString];
    return trimmed.length == 0u || [trimmed isEqualToString:@"-"]
        || [trimmed isEqualToString:@"—"]
        || [trimmed isEqualToString:@"base"];
}

} // namespace

@interface S3GTrackerSongRow : NSObject
@property(nonatomic, copy) NSString* pattern;
@property(nonatomic) NSInteger repeats;
@property(nonatomic) double bpm;
@property(nonatomic) BOOL hasBPMOverride;
@property(nonatomic) NSInteger ticks;
@property(nonatomic) double swing;
@property(nonatomic) BOOL hasSwingOverride;
@property(nonatomic, strong) NSMutableIndexSet* mutedLanes;
@end

@implementation S3GTrackerSongRow
@end

@interface S3GTrackerSongRootView : NSView
@end

@implementation S3GTrackerSongRootView

- (BOOL)isFlipped { return YES; }

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    [s3gSongColor(0x0c0c0c) setFill];
    NSRectFill(self.bounds);
    [s3gSongColor(0x141414) setFill];
    NSRectFill(NSMakeRect(0.0, 0.0, NSWidth(self.bounds), 76.0));
    [s3gSongColor(0x252525) setFill];
    NSRectFill(NSMakeRect(0.0, 75.0, NSWidth(self.bounds), 1.0));
}

@end

@interface S3GTrackerSongHeaderCell : NSTableHeaderCell
@end

@implementation S3GTrackerSongHeaderCell

- (void)drawWithFrame:(NSRect)cellFrame inView:(NSView*)controlView
{
    (void)controlView;
    [s3gSongColor(0x181818) setFill];
    NSRectFill(cellFrame);
    [s3gSongColor(0x3a3a3a) setStroke];
    NSBezierPath* divider = [NSBezierPath bezierPath];
    [divider moveToPoint:NSMakePoint(NSMaxX(cellFrame) - 0.5, NSMinY(cellFrame))];
    [divider lineToPoint:NSMakePoint(NSMaxX(cellFrame) - 0.5, NSMaxY(cellFrame))];
    [divider stroke];

    NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc] init];
    paragraph.alignment = NSTextAlignmentCenter;
    paragraph.lineBreakMode = NSLineBreakByClipping;
    [self.stringValue drawInRect:NSInsetRect(cellFrame, 4.0, 6.0)
        withAttributes:@{
            NSForegroundColorAttributeName: s3gSongColor(0x8e9697),
            NSFontAttributeName: s3gSongFont(10.0, NSFontWeightSemibold),
            NSParagraphStyleAttributeName: paragraph,
        }];
}

@end

@interface S3GTrackerSongRowView : NSTableRowView
@property(nonatomic) NSInteger songRow;
@property(nonatomic) BOOL playbackActive;
@property(nonatomic) BOOL playbackPending;
@end

@implementation S3GTrackerSongRowView

- (void)drawBackgroundInRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    NSColor* color = (self.songRow % 2 == 0)
        ? s3gSongColor(0x111111) : s3gSongColor(0x151515);
    [color setFill];
    NSRectFill(self.bounds);
    if (self.playbackActive) {
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Success, 0.10) setFill];
        NSRectFill(self.bounds);
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Success) setFill];
        NSRectFill(NSMakeRect(NSMinX(self.bounds), NSMinY(self.bounds),
            3.0, NSHeight(self.bounds)));
    }
    if (self.playbackPending) {
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Warning) setFill];
        NSRectFill(NSMakeRect(NSMinX(self.bounds), NSMaxY(self.bounds) - 2.0,
            NSWidth(self.bounds), 2.0));
    }
}

- (void)drawSelectionInRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Selection) setFill];
    NSRectFill(self.bounds);
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Focus, 0.82) setStroke];
    NSBezierPath* path = [NSBezierPath bezierPathWithRect:NSInsetRect(self.bounds, 0.5, 0.5)];
    [path setLineWidth:1.0];
    [path stroke];
}

@end

@interface S3GTrackerSongMuteButton : NSButton
@end

@implementation S3GTrackerSongMuteButton

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

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    const BOOL muted = self.state == NSControlStateValueOn;
    const NSRect box = NSInsetRect(self.bounds, 2.0, 3.0);
    [(muted ? s3gSongColor(0x303030) : s3gSongColor(0x242424)) setFill];
    NSRectFill(box);
    [(muted ? S3GTrackerThemeColor(S3GTrackerThemeRole::Danger)
            : S3GTrackerThemeColor(S3GTrackerThemeRole::Border)) setStroke];
    NSFrameRect(box);
    if (muted) {
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Danger) setFill];
        NSRectFill(NSMakeRect(box.origin.x + 1.0, NSMaxY(box) - 3.0,
            box.size.width - 2.0, 2.0));
    }
    if (self.window.firstResponder == self) {
        [S3GTrackerThemeColor(S3GTrackerThemeRole::Focus) setStroke];
        NSFrameRect(NSInsetRect(box, 2.0, 2.0));
    }

    NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc] init];
    paragraph.alignment = NSTextAlignmentCenter;
    NSString* label = muted ? @"×"
        : (self.title != nil ? self.title : @"");
    NSColor* textColor = muted
        ? S3GTrackerThemeColor(S3GTrackerThemeRole::Danger)
        : S3GTrackerThemeColor(S3GTrackerThemeRole::TextSecondary);
    if (!textColor) textColor = NSColor.secondaryLabelColor;
    NSFont* font = s3gSongFont(10.0, NSFontWeightSemibold);
    if (!font) font = [NSFont systemFontOfSize:10.0
        weight:NSFontWeightSemibold];
    NSMutableDictionary<NSAttributedStringKey, id>* attributes =
        [[NSMutableDictionary alloc] initWithCapacity:3u];
    if (textColor)
        attributes[NSForegroundColorAttributeName] = textColor;
    if (font)
        attributes[NSFontAttributeName] = font;
    if (paragraph)
        attributes[NSParagraphStyleAttributeName] = paragraph;
    [label drawInRect:NSOffsetRect(self.bounds, 0.0, 5.0)
        withAttributes:attributes];
}

@end

@interface S3GTrackerSongMuteMatrixView : NSView
@end


@implementation S3GTrackerSongMuteMatrixView

- (void)layout
{
    [super layout];
    constexpr NSInteger kColumns = 16;
    constexpr NSInteger kRows = 2;
    const CGFloat slotWidth = std::max(22.0,
        (NSWidth(self.bounds) - 8.0) / kColumns);
    const CGFloat slotHeight = std::max(20.0,
        NSHeight(self.bounds) / kRows);
    NSInteger lane = 0;
    for (NSView* view in self.subviews) {
        const NSInteger row = lane / kColumns;
        const NSInteger column = lane % kColumns;
        view.frame = NSMakeRect(4.0 + column * slotWidth,
            NSHeight(self.bounds) - (row + 1) * slotHeight,
            slotWidth - 2.0, slotHeight);
        ++lane;
    }
}

@end

@interface S3GTrackerSongWindowController ()
    <NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate,
    NSWindowDelegate>
@property(nonatomic, strong) NSMutableArray<S3GTrackerSongRow*>* rows;
@property(nonatomic, strong) NSTableView* tableView;
@property(nonatomic, strong) NSTextField* summaryLabel;
@property(nonatomic, strong) NSButton* addButton;
@property(nonatomic, strong) NSButton* removeButton;
@property(nonatomic, strong) S3GTrackerActionButton* songModeButton;
@property(nonatomic, strong) S3GTrackerActionButton* songLoopButton;
@property(nonatomic, strong) S3GTrackerPopupButton* launchQuantizationPopup;
@property(nonatomic, strong) S3GTrackerActionButton* queueButton;
@property(nonatomic, copy) NSString* arrangementName;
@property(nonatomic, copy) NSArray<NSString*>* availablePatternIds;
@property(nonatomic, copy) NSString* activePatternId;
@property(nonatomic) BOOL arrangementLoops;
@property(nonatomic) NSInteger arrangementTicksPerBeat;
@property(nonatomic) NSUInteger currentPlaybackRow;
@property(nonatomic) BOOL currentPlaybackRowValid;
@property(nonatomic) NSUInteger pendingPlaybackRow;
@property(nonatomic) BOOL pendingPlaybackRowValid;
@property(nonatomic) BOOL playbackLocked;
@end

@implementation S3GTrackerSongWindowController

+ (instancetype)sharedController
{
    static S3GTrackerSongWindowController* controller = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        controller = [[S3GTrackerSongWindowController alloc] init];
    });
    return controller;
}

- (instancetype)init
{
    const NSWindowStyleMask style = NSWindowStyleMaskTitled
        | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
        | NSWindowStyleMaskResizable;
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0.0, 0.0, 1080.0, 610.0)
                  styleMask:style
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self = [super initWithWindow:window];
    if (!self) return nil;

    _rows = [[NSMutableArray alloc] init];
    [_rows addObject:[self newRowWithPattern:@"A01"]];
    _arrangementName = @"SONG";
    _availablePatternIds = @[ @"A01" ];
    _activePatternId = @"A01";
    _arrangementLoops = NO;
    _arrangementTicksPerBeat = 4;
    _playbackEnabled = NO;
    _currentPlaybackRowValid = NO;

    window.title = @"s3g Tracker — Song";
    window.minSize = NSMakeSize(900.0, 430.0);
    window.releasedWhenClosed = NO;
    window.delegate = self;
    window.tabbingMode = NSWindowTabbingModeDisallowed;
    window.titlebarAppearsTransparent = YES;
    window.backgroundColor = s3gSongColor(0x0c0c0c);
    window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    S3GTrackerRestoreWindowFrame(window, @"S3GTrackerSongWindow");

    [self buildInterface];
    return self;
}

- (S3GTrackerSongRow*)newRowWithPattern:(NSString*)pattern
{
    S3GTrackerSongRow* row = [[S3GTrackerSongRow alloc] init];
    row.pattern = pattern;
    row.repeats = 1;
    row.bpm = 126.0;
    row.hasBPMOverride = NO;
    row.ticks = 4;
    row.swing = 56.0;
    row.hasSwingOverride = YES;
    row.mutedLanes = [[NSMutableIndexSet alloc] init];
    return row;
}

- (NSTextField*)label:(NSString*)text size:(CGFloat)size color:(NSColor*)color
    weight:(NSFontWeight)weight
{
    NSTextField* label = [NSTextField labelWithString:text];
    label.font = s3gSongFont(size, weight);
    label.textColor = color;
    label.translatesAutoresizingMaskIntoConstraints = NO;
    return label;
}

- (NSButton*)actionButton:(NSString*)title action:(SEL)action
{
    S3GTrackerActionButton* button = [[S3GTrackerActionButton alloc]
        initWithFrame:NSZeroRect];
    button.title = title;
    button.target = self;
    button.action = action;
    button.translatesAutoresizingMaskIntoConstraints = NO;
    return button;
}

- (void)buildInterface
{
    S3GTrackerSongRootView* root = [[S3GTrackerSongRootView alloc]
        initWithFrame:self.window.contentView.bounds];
    self.window.contentView = root;

    NSTextField* title = [self label:@"SONG / ARRANGEMENT" size:18.0
        color:s3gSongColor(0xa8a8a8) weight:NSFontWeightMedium];
    [root addSubview:title];

    _summaryLabel = [self label:self.songSummary size:10.0
        color:s3gSongColor(0xb8b8b8) weight:NSFontWeightMedium];
    [root addSubview:_summaryLabel];

    NSScrollView* scrollView = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    scrollView.translatesAutoresizingMaskIntoConstraints = NO;
    scrollView.hasVerticalScroller = YES;
    scrollView.hasHorizontalScroller = YES;
    scrollView.autohidesScrollers = YES;
    scrollView.scrollerStyle = NSScrollerStyleOverlay;
    scrollView.scrollerKnobStyle = NSScrollerKnobStyleLight;
    scrollView.borderType = NSNoBorder;
    scrollView.drawsBackground = YES;
    scrollView.backgroundColor = s3gSongColor(0x0e0e0e);
    [root addSubview:scrollView];

    _tableView = [[NSTableView alloc] initWithFrame:NSZeroRect];
    _tableView.delegate = self;
    _tableView.dataSource = self;
    _tableView.rowHeight = 48.0;
    _tableView.intercellSpacing = NSMakeSize(1.0, 1.0);
    _tableView.backgroundColor = s3gSongColor(0x0e0e0e);
    _tableView.gridColor = s3gSongColor(0x303030);
    _tableView.gridStyleMask = NSTableViewSolidVerticalGridLineMask
        | NSTableViewSolidHorizontalGridLineMask;
    _tableView.selectionHighlightStyle = NSTableViewSelectionHighlightStyleRegular;
    _tableView.columnAutoresizingStyle = NSTableViewNoColumnAutoresizing;
    _tableView.allowsColumnReordering = NO;
    _tableView.allowsColumnResizing = YES;
    _tableView.allowsMultipleSelection = NO;

    [self addColumn:S3GSongColumnRow title:@"ROW" width:48.0 minWidth:44.0];
    [self addColumn:S3GSongColumnPattern title:@"PATTERN" width:180.0 minWidth:110.0];
    [self addColumn:S3GSongColumnRepeats title:@"REP" width:70.0 minWidth:56.0];
    [self addColumn:S3GSongColumnTicks title:@"TICKS" width:80.0 minWidth:62.0];
    [self addColumn:S3GSongColumnSwing title:@"SWING %" width:92.0 minWidth:76.0];
    [self addColumn:S3GSongColumnMutes
        title:@"LANE MUTES  1–16 TOP · 17–32 BOTTOM"
        width:500.0 minWidth:420.0];
    [self addColumn:S3GSongColumnDelete title:@"DEL" width:50.0 minWidth:46.0];
    CGFloat tableWidth = 0.0;
    for (NSTableColumn* column in _tableView.tableColumns)
        tableWidth += column.width + _tableView.intercellSpacing.width;
    _tableView.frame = NSMakeRect(0.0, 0.0, tableWidth,
        _tableView.rowHeight);
    _tableView.autoresizingMask = NSViewWidthSizable;
    scrollView.documentView = _tableView;

    _addButton = [self actionButton:@"＋ ADD ROW" action:@selector(addRow:)];
    [root addSubview:_addButton];
    _removeButton = [self actionButton:@"− REMOVE SELECTED"
        action:@selector(removeSelectedRow:)];
    _removeButton.tag = 2;
    _removeButton.enabled = NO;
    [root addSubview:_removeButton];

    _songModeButton = (S3GTrackerActionButton*)[self
        actionButton:@"SONG TRANSPORT: OFF" action:@selector(toggleSongMode:)];
    _songModeButton.buttonType = NSButtonTypeToggle;
    _songModeButton.tag = 1;
    [root addSubview:_songModeButton];

    _songLoopButton = (S3GTrackerActionButton*)[self
        actionButton:@"LOOP SONG: OFF" action:@selector(toggleSongLoop:)];
    _songLoopButton.buttonType = NSButtonTypeToggle;
    _songLoopButton.tag = 1;
    [root addSubview:_songLoopButton];

    _launchQuantizationPopup = [[S3GTrackerPopupButton alloc]
        initWithFrame:NSZeroRect pullsDown:NO];
    _launchQuantizationPopup.translatesAutoresizingMaskIntoConstraints = NO;
    [_launchQuantizationPopup addItemsWithTitles:@[
        @"NEXT TICK", @"NEXT BEAT", @"NEXT CYCLE", @"NEXT SONG ROW"
    ]];
    [_launchQuantizationPopup selectItemAtIndex:2];
    _launchQuantizationPopup.enabled = NO;
    [root addSubview:_launchQuantizationPopup];

    _queueButton = (S3GTrackerActionButton*)[self
        actionButton:@"QUEUE SELECTED" action:@selector(queueSelectedRow:)];
    _queueButton.enabled = NO;
    [root addSubview:_queueButton];

    NSTextField* hint = [self label:
        @"GREEN = PLAYING · YELLOW = QUEUED · RED = MUTED · — = BASE · STABLE PATTERN IDS"
        size:9.0 color:s3gSongColor(0x737879) weight:NSFontWeightMedium];
    hint.alignment = NSTextAlignmentRight;
    [root addSubview:hint];

    [NSLayoutConstraint activateConstraints:@[
        [title.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24.0],
        [title.topAnchor constraintEqualToAnchor:root.topAnchor constant:16.0],
        [_summaryLabel.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
        [_summaryLabel.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:4.0],
        [scrollView.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24.0],
        [scrollView.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24.0],
        [scrollView.topAnchor constraintEqualToAnchor:root.topAnchor constant:88.0],
        [scrollView.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-58.0],
        [_addButton.leadingAnchor constraintEqualToAnchor:scrollView.leadingAnchor],
        [_addButton.topAnchor constraintEqualToAnchor:scrollView.bottomAnchor constant:13.0],
        [_removeButton.leadingAnchor constraintEqualToAnchor:_addButton.trailingAnchor constant:10.0],
        [_removeButton.centerYAnchor constraintEqualToAnchor:_addButton.centerYAnchor],
        [_songModeButton.leadingAnchor constraintEqualToAnchor:_removeButton.trailingAnchor
            constant:16.0],
        [_songModeButton.centerYAnchor constraintEqualToAnchor:_addButton.centerYAnchor],
        [_songLoopButton.leadingAnchor constraintEqualToAnchor:_songModeButton.trailingAnchor
            constant:10.0],
        [_songLoopButton.centerYAnchor constraintEqualToAnchor:_addButton.centerYAnchor],
        [_launchQuantizationPopup.leadingAnchor constraintEqualToAnchor:_songLoopButton.trailingAnchor
            constant:10.0],
        [_launchQuantizationPopup.centerYAnchor constraintEqualToAnchor:_addButton.centerYAnchor],
        [_queueButton.leadingAnchor constraintEqualToAnchor:_launchQuantizationPopup.trailingAnchor
            constant:8.0],
        [_queueButton.centerYAnchor constraintEqualToAnchor:_addButton.centerYAnchor],
        [hint.trailingAnchor constraintEqualToAnchor:scrollView.trailingAnchor],
        [hint.centerYAnchor constraintEqualToAnchor:_addButton.centerYAnchor],
        [hint.leadingAnchor constraintGreaterThanOrEqualToAnchor:_queueButton.trailingAnchor
            constant:16.0],
    ]];

    [_tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:0]
        byExtendingSelection:NO];
}

- (void)addColumn:(NSString*)identifier title:(NSString*)title
    width:(CGFloat)width minWidth:(CGFloat)minWidth
{
    NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:identifier];
    column.title = title;
    column.width = width;
    column.minWidth = minWidth;
    column.headerCell = [[S3GTrackerSongHeaderCell alloc] initTextCell:title];
    [_tableView addTableColumn:column];
}

- (void)showWindow:(id)sender
{
    [super showWindow:sender];
    if (self.window.miniaturized) [self.window deminiaturize:sender];
    [self.window makeKeyAndOrderFront:sender];
}

- (BOOL)windowShouldClose:(NSWindow*)sender
{
    (void)sender;
    // releasedWhenClosed is NO and this singleton retains the row model, so
    // normal close semantics preserve the draft while still allowing Cocoa's
    // last-window termination behavior to work.
    return YES;
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    (void)tableView;
    return static_cast<NSInteger>(self.rows.count);
}

- (NSTableRowView*)tableView:(NSTableView*)tableView rowViewForRow:(NSInteger)row
{
    (void)tableView;
    S3GTrackerSongRowView* view = [[S3GTrackerSongRowView alloc] initWithFrame:NSZeroRect];
    view.songRow = row;
    view.playbackActive = self.currentPlaybackRowValid
        && self.currentPlaybackRow == static_cast<NSUInteger>(row);
    view.playbackPending = self.pendingPlaybackRowValid
        && self.pendingPlaybackRow == static_cast<NSUInteger>(row);
    return view;
}

- (NSTextField*)cellText:(NSString*)text row:(NSInteger)row
    column:(NSString*)column editable:(BOOL)editable alignment:(NSTextAlignment)alignment
{
    NSTextField* field = [[NSTextField alloc] initWithFrame:NSZeroRect];
    field.stringValue = text;
    field.font = s3gSongFont(11.0, editable ? NSFontWeightMedium : NSFontWeightRegular);
    field.textColor = editable ? s3gSongColor(0xd4d4d4) : s3gSongColor(0x858b8c);
    field.alignment = alignment;
    field.bordered = NO;
    field.drawsBackground = NO;
    const BOOL canEdit = editable && !self.playbackLocked;
    field.editable = canEdit;
    field.selectable = canEdit;
    field.delegate = canEdit ? self : nil;
    field.identifier = column;
    field.tag = row;
    const NSInteger columnIndex = [self.tableView columnWithIdentifier:column];
    const CGFloat columnWidth = columnIndex >= 0
        ? self.tableView.tableColumns[(NSUInteger)columnIndex].width : 108.0;
    field.frame = NSMakeRect(4.0,
        std::max(2.0, (self.tableView.rowHeight - 23.0) * 0.5),
        std::max(20.0, columnWidth - 8.0), 23.0);
    field.autoresizingMask = NSViewWidthSizable;
    return field;
}

- (NSView*)tableView:(NSTableView*)tableView
    viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)rowIndex
{
    (void)tableView;
    if (rowIndex < 0 || rowIndex >= (NSInteger)self.rows.count) return nil;
    S3GTrackerSongRow* row = self.rows[(NSUInteger)rowIndex];
    NSString* column = tableColumn.identifier;
    NSTableCellView* cell = [[NSTableCellView alloc]
        initWithFrame:NSMakeRect(0.0, 0.0, tableColumn.width, tableView.rowHeight)];

    if ([column isEqualToString:S3GSongColumnRow]) {
        [cell addSubview:[self cellText:[NSString stringWithFormat:@"%02ld", rowIndex + 1]
            row:rowIndex column:column editable:NO alignment:NSTextAlignmentCenter]];
    } else if ([column isEqualToString:S3GSongColumnPattern]) {
        S3GTrackerPopupButton* pattern = [[S3GTrackerPopupButton alloc]
            initWithFrame:NSMakeRect(4.0,
                std::max(2.0, (tableView.rowHeight - 28.0) * 0.5),
                std::max(40.0, tableColumn.width - 8.0), 28.0)
            pullsDown:NO];
        pattern.autoresizingMask = NSViewWidthSizable;
        pattern.target = self;
        pattern.action = @selector(patternPopupChanged:);
        pattern.tag = rowIndex;
        pattern.enabled = !self.playbackLocked;
        pattern.accessibilityLabel = [NSString stringWithFormat:
            @"Song row %ld pattern", rowIndex + 1];
        for (NSString* patternId in self.availablePatternIds) {
            [pattern addItemWithTitle:patternId];
            pattern.lastItem.representedObject = patternId;
        }
        const NSUInteger selectionIndex = [self.availablePatternIds
            indexOfObject:row.pattern];
        NSInteger selection = selectionIndex == NSNotFound
            ? -1 : static_cast<NSInteger>(selectionIndex);
        if (selectionIndex == NSNotFound) {
            NSString* missing = [NSString stringWithFormat:@"MISSING · %@",
                row.pattern.length > 0u ? row.pattern : @"—"];
            [pattern addItemWithTitle:missing];
            pattern.lastItem.representedObject = row.pattern.length > 0u
                ? row.pattern : @"";
            selection = static_cast<NSInteger>(pattern.numberOfItems - 1u);
            pattern.toolTip = @"This Song row references a pattern that is not in the bank";
        }
        if (selection >= 0) [pattern selectItemAtIndex:selection];
        [cell addSubview:pattern];
    } else if ([column isEqualToString:S3GSongColumnRepeats]) {
        [cell addSubview:[self cellText:[NSString stringWithFormat:@"%ld", row.repeats]
            row:rowIndex column:column editable:YES alignment:NSTextAlignmentCenter]];
    } else if ([column isEqualToString:S3GSongColumnBPM]) {
        NSString* bpm = row.hasBPMOverride
            ? [NSString stringWithFormat:@"%.1f", row.bpm] : @"—";
        [cell addSubview:[self cellText:bpm
            row:rowIndex column:column editable:YES alignment:NSTextAlignmentCenter]];
    } else if ([column isEqualToString:S3GSongColumnTicks]) {
        [cell addSubview:[self cellText:[NSString stringWithFormat:@"%ld", row.ticks]
            row:rowIndex column:column editable:YES alignment:NSTextAlignmentCenter]];
    } else if ([column isEqualToString:S3GSongColumnSwing]) {
        NSString* swing = row.hasSwingOverride
            ? [NSString stringWithFormat:@"%.1f", row.swing] : @"—";
        [cell addSubview:[self cellText:swing
            row:rowIndex column:column editable:YES alignment:NSTextAlignmentCenter]];
    } else if ([column isEqualToString:S3GSongColumnMutes]) {
        NSView* matrix = [[S3GTrackerSongMuteMatrixView alloc]
            initWithFrame:NSMakeRect(2.0, 1.0,
            MAX(416.0, tableColumn.width - 4.0), tableView.rowHeight - 2.0)];
        matrix.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        for (NSInteger lane = 0; lane < 32; ++lane) {
            S3GTrackerSongMuteButton* button = [[S3GTrackerSongMuteButton alloc]
                initWithFrame:NSZeroRect];
            button.title = [NSString stringWithFormat:@"%ld", lane + 1];
            button.buttonType = NSButtonTypeToggle;
            button.bordered = NO;
            button.state = [row.mutedLanes containsIndex:(NSUInteger)lane]
                ? NSControlStateValueOn : NSControlStateValueOff;
            button.tag = rowIndex * 32 + lane;
            button.target = self;
            button.action = @selector(toggleLaneMute:);
            button.enabled = !self.playbackLocked;
            button.toolTip = [NSString stringWithFormat:@"Toggle lane %ld mute", lane + 1];
            button.accessibilityLabel = [NSString stringWithFormat:
                @"Song row %ld lane %ld mute", rowIndex + 1, lane + 1];
            [matrix addSubview:button];
        }
        [cell addSubview:matrix];
    } else if ([column isEqualToString:S3GSongColumnDelete]) {
        S3GTrackerActionButton* button = [[S3GTrackerActionButton alloc]
            initWithFrame:NSZeroRect];
        button.title = @"×";
        button.target = self;
        button.action = @selector(deleteRowButton:);
        button.enabled = !self.playbackLocked;
        button.frame = NSMakeRect(4.0,
            std::max(2.0, (tableView.rowHeight - 30.0) * 0.5),
            40.0, 30.0);
        button.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        button.identifier = @"danger";
        button.tag = rowIndex;
        button.toolTip = @"Delete this song row";
        button.accessibilityLabel = [NSString stringWithFormat:
            @"Delete song row %ld", rowIndex + 1];
        [cell addSubview:button];
    }
    return cell;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
    (void)notification;
    self.removeButton.enabled = !self.playbackLocked
        && self.tableView.selectedRow >= 0;
    self.queueButton.enabled = self.playbackEnabled && self.playbackLocked
        && self.tableView.selectedRow >= 0;
}

- (void)controlTextDidEndEditing:(NSNotification*)notification
{
    NSTextField* field = (NSTextField*)notification.object;
    if (![field isKindOfClass:[NSTextField class]]) return;
    [self commitField:field];
}

- (void)controlTextDidBeginEditing:(NSNotification*)notification
{
    NSTextField* field = (NSTextField*)notification.object;
    if ([field isKindOfClass:[NSTextField class]])
        S3GTrackerStyleTextEditor(field);
}

- (void)commitField:(NSTextField*)field
{
    const NSInteger rowIndex = field.tag;
    if (rowIndex < 0 || rowIndex >= (NSInteger)self.rows.count) return;
    S3GTrackerSongRow* row = self.rows[(NSUInteger)rowIndex];
    NSString* column = field.identifier;
    BOOL changed = NO;
    BOOL valid = YES;

    if ([column isEqualToString:S3GSongColumnRepeats]) {
        NSInteger value = 0;
        valid = s3gScanInteger(field.stringValue, value);
        if (valid) {
            value = s3gClampInteger(value, 1, 999);
            changed = value != row.repeats;
            row.repeats = value;
        }
    } else if ([column isEqualToString:S3GSongColumnBPM]) {
        if (s3gUsesProjectValue(field.stringValue)) {
            changed = row.hasBPMOverride;
            row.hasBPMOverride = NO;
        } else {
            double value = 0.0;
            valid = s3gScanDouble(field.stringValue, value);
            if (valid) {
            value = s3gClampDouble(value, 20.0, 400.0);
            changed = !row.hasBPMOverride || value != row.bpm;
            row.bpm = value;
            row.hasBPMOverride = YES;
            }
        }
    } else if ([column isEqualToString:S3GSongColumnTicks]) {
        NSInteger value = 0;
        valid = s3gScanInteger(field.stringValue, value);
        if (valid) {
            value = s3gClampInteger(value, 1, 96);
            changed = value != row.ticks;
            row.ticks = value;
        }
    } else if ([column isEqualToString:S3GSongColumnSwing]) {
        if (s3gUsesProjectValue(field.stringValue)) {
            changed = row.hasSwingOverride;
            row.hasSwingOverride = NO;
        } else {
            double value = 0.0;
            valid = s3gScanDouble(field.stringValue, value);
            if (valid) {
            value = s3gClampDouble(value, 50.0, 75.0);
            changed = !row.hasSwingOverride || value != row.swing;
            row.swing = value;
            row.hasSwingOverride = YES;
            }
        }
    }

    const NSInteger columnIndex = [self.tableView columnWithIdentifier:column];
    if (columnIndex >= 0)
        [self.tableView reloadDataForRowIndexes:
            [NSIndexSet indexSetWithIndex:(NSUInteger)rowIndex]
            columnIndexes:[NSIndexSet indexSetWithIndex:
                (NSUInteger)columnIndex]];
    if (changed) [self songDidChange];
}

- (void)patternPopupChanged:(S3GTrackerPopupButton*)sender
{
    if (self.playbackLocked) return;
    const NSInteger rowIndex = sender.tag;
    if (rowIndex < 0 || rowIndex >= (NSInteger)self.rows.count) return;
    NSString* patternId = sender.selectedItem.representedObject;
    if (![patternId isKindOfClass:NSString.class]
        || patternId.length == 0u) return;
    S3GTrackerSongRow* row = self.rows[(NSUInteger)rowIndex];
    if ([row.pattern isEqualToString:patternId]) return;
    row.pattern = patternId;
    [self songDidChange];
}

- (void)addRow:(id)sender
{
    (void)sender;
    if (self.playbackLocked) return;
    NSInteger insertion = self.tableView.selectedRow;
    insertion = insertion < 0 ? (NSInteger)self.rows.count : insertion + 1;
    NSString* pattern = self.activePatternId;
    if (pattern.length == 0u) pattern = self.rows.firstObject.pattern;
    if (pattern.length == 0u) pattern = self.availablePatternIds.firstObject;
    if (pattern.length == 0u) pattern = @"A01";
    S3GTrackerSongRow* row = [self newRowWithPattern:pattern];
    if (insertion > 0 && insertion <= (NSInteger)self.rows.count) {
        S3GTrackerSongRow* prior = self.rows[(NSUInteger)insertion - 1u];
        row.bpm = prior.bpm;
        row.hasBPMOverride = prior.hasBPMOverride;
        row.ticks = prior.ticks;
        row.swing = prior.swing;
        row.hasSwingOverride = prior.hasSwingOverride;
    }
    [self.rows insertObject:row atIndex:(NSUInteger)insertion];
    [self.tableView reloadData];
    [self.tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)insertion]
        byExtendingSelection:NO];
    [self.tableView scrollRowToVisible:insertion];
    [self songDidChange];
}

- (void)removeSelectedRow:(id)sender
{
    (void)sender;
    const NSInteger row = self.tableView.selectedRow;
    [self removeRowAtIndex:row];
}

- (void)deleteRowButton:(NSButton*)sender
{
    [self removeRowAtIndex:sender.tag];
}

- (void)removeRowAtIndex:(NSInteger)row
{
    if (self.playbackLocked) return;
    if (row < 0 || row >= (NSInteger)self.rows.count) return;
    [self.rows removeObjectAtIndex:(NSUInteger)row];
    [self.tableView reloadData];
    if (self.rows.count > 0) {
        const NSUInteger selection = std::min((NSUInteger)row, self.rows.count - 1u);
        [self.tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:selection]
            byExtendingSelection:NO];
    } else {
        self.removeButton.enabled = NO;
    }
    [self songDidChange];
}

- (void)toggleLaneMute:(S3GTrackerSongMuteButton*)sender
{
    if (self.playbackLocked) return;
    const NSInteger rowIndex = sender.tag / 32;
    const NSInteger lane = sender.tag % 32;
    if (rowIndex < 0 || rowIndex >= (NSInteger)self.rows.count
        || lane < 0 || lane >= 32) return;
    S3GTrackerSongRow* row = self.rows[(NSUInteger)rowIndex];
    if ([row.mutedLanes containsIndex:(NSUInteger)lane])
        [row.mutedLanes removeIndex:(NSUInteger)lane];
    else
        [row.mutedLanes addIndex:(NSUInteger)lane];
    sender.state = [row.mutedLanes containsIndex:(NSUInteger)lane]
        ? NSControlStateValueOn : NSControlStateValueOff;
    [sender setNeedsDisplay:YES];
    [self songDidChange];
}

- (NSString*)songSummary
{
    const NSUInteger count = self.rows.count;
    if (count == 0) return @"0 ROWS · EMPTY ARRANGEMENT";
    NSInteger passes = 0;
    NSMutableSet<NSString*>* patterns = [[NSMutableSet alloc] init];
    for (S3GTrackerSongRow* row in self.rows) {
        passes += row.repeats;
        [patterns addObject:row.pattern];
    }
    return [NSString stringWithFormat:@"%lu ROW%@ · %lu PATTERN%@ · %ld PASS%@ · HOST TEMPO",
        (unsigned long)count, count == 1 ? @"" : @"S",
        (unsigned long)patterns.count, patterns.count == 1 ? @"" : @"S",
        passes, passes == 1 ? @"" : @"ES"];
}

- (void)songDidChange
{
    NSString* summary = self.songSummary;
    self.summaryLabel.stringValue = summary;
    if (self.changeHandler) self.changeHandler(summary);
}

- (void)setPlaybackEnabled:(BOOL)playbackEnabled
{
    _playbackEnabled = playbackEnabled;
    self.songModeButton.state = playbackEnabled
        ? NSControlStateValueOn : NSControlStateValueOff;
    self.songModeButton.title = playbackEnabled
        ? @"SONG TRANSPORT: ON" : @"SONG TRANSPORT: OFF";
    self.queueButton.enabled = playbackEnabled
        && self.playbackLocked
        && self.tableView.selectedRow >= 0;
    [self.songModeButton setNeedsDisplay:YES];
}

- (void)toggleSongMode:(NSButton*)sender
{
    if (self.playbackLocked) return;
    self.playbackEnabled = sender.state == NSControlStateValueOn;
    if (self.modeChangeHandler)
        self.modeChangeHandler(self.playbackEnabled);
}

- (void)toggleSongLoop:(NSButton*)sender
{
    if (self.playbackLocked) return;
    self.arrangementLoops = sender.state == NSControlStateValueOn;
    self.songLoopButton.title = self.arrangementLoops
        ? @"LOOP SONG: ON" : @"LOOP SONG: OFF";
    [self.songLoopButton setNeedsDisplay:YES];
    [self songDidChange];
}

- (void)queueSelectedRow:(id)sender
{
    (void)sender;
    const NSInteger row = self.tableView.selectedRow;
    if (row < 0 || !self.launchHandler) return;
    const NSInteger quantization = std::clamp<NSInteger>(
        self.launchQuantizationPopup.indexOfSelectedItem, 0, 3);
    self.launchHandler(static_cast<NSUInteger>(row), quantization);
}

- (void)setPlaybackRow:(NSUInteger)row valid:(BOOL)valid
{
    if (self.currentPlaybackRow == row
        && self.currentPlaybackRowValid == valid) return;
    self.currentPlaybackRow = row;
    self.currentPlaybackRowValid = valid;
    [self.tableView reloadData];
}

- (void)setPendingPlaybackRow:(NSUInteger)row valid:(BOOL)valid
{
    if (self.pendingPlaybackRow == row
        && self.pendingPlaybackRowValid == valid) return;
    self.pendingPlaybackRow = row;
    self.pendingPlaybackRowValid = valid;
    [self.tableView reloadData];
}

- (void)setPlaybackLocked:(BOOL)locked
{
    if (self.playbackLocked == locked) return;
    _playbackLocked = locked;
    if (locked) [self.window makeFirstResponder:self.tableView];
    self.addButton.enabled = !locked;
    self.removeButton.enabled = !locked && self.tableView.selectedRow >= 0;
    self.songModeButton.enabled = !locked;
    self.songLoopButton.enabled = !locked;
    self.launchQuantizationPopup.enabled = locked;
    self.queueButton.enabled = locked && self.playbackEnabled
        && self.tableView.selectedRow >= 0;
    [self.tableView reloadData];
}

- (void)setAvailablePatternIds:(NSArray<NSString*>*)patternIds
    activePatternId:(NSString*)activePatternId
{
    NSMutableArray<NSString*>* available = [[NSMutableArray alloc] init];
    for (NSString* patternId in patternIds) {
        if (![patternId isKindOfClass:NSString.class]
            || patternId.length == 0u
            || [available containsObject:patternId]) continue;
        [available addObject:patternId.copy];
    }
    if (available.count == 0u) [available addObject:@"A01"];
    _availablePatternIds = available.copy;
    _activePatternId = [available containsObject:activePatternId]
        ? activePatternId.copy : available.firstObject;
    [self.tableView reloadData];
}

- (s3g::tracker::SongArrangement)songArrangement
{
    s3g::tracker::SongArrangement arrangement;
    const char* name = self.arrangementName.UTF8String;
    arrangement.name = name ? name : "SONG";
    arrangement.loop = self.arrangementLoops;
    arrangement.ticksPerBeat = static_cast<uint32_t>(std::clamp<NSInteger>(
        self.arrangementTicksPerBeat, 1, 96));
    arrangement.rows.reserve(self.rows.count);
    for (S3GTrackerSongRow* source in self.rows) {
        s3g::tracker::SongRow row;
        const char* pattern = source.pattern.UTF8String;
        row.patternId = pattern ? pattern : "";
        row.durationTicks = static_cast<uint32_t>(std::clamp<NSInteger>(
            source.ticks, 1, 1 << 20));
        row.repeats = static_cast<uint32_t>(std::clamp<NSInteger>(
            source.repeats, 1, 65535));
        if (source.hasBPMOverride)
            row.bpm = std::clamp(source.bpm, 20.0, 400.0);
        if (source.hasSwingOverride)
            row.swing = std::clamp(source.swing * 0.01, 0.5, 0.75);
        __block uint32_t muteMask = 0u;
        [source.mutedLanes enumerateIndexesUsingBlock:
            ^(NSUInteger index, BOOL* stop) {
                (void)stop;
                if (index < 32u) muteMask |= (1u << index);
            }];
        row.mutedTracks = muteMask;
        arrangement.rows.push_back(std::move(row));
    }
    return arrangement;
}

- (void)setSongArrangement:(const s3g::tracker::SongArrangement&)arrangement
{
    [self.rows removeAllObjects];
    self.arrangementName = [NSString stringWithUTF8String:
        arrangement.name.c_str()];
    self.arrangementLoops = arrangement.loop;
    self.songLoopButton.state = arrangement.loop
        ? NSControlStateValueOn : NSControlStateValueOff;
    self.songLoopButton.title = arrangement.loop
        ? @"LOOP SONG: ON" : @"LOOP SONG: OFF";
    self.arrangementTicksPerBeat = static_cast<NSInteger>(
        arrangement.ticksPerBeat == 0u ? 4u : arrangement.ticksPerBeat);
    for (const auto& source : arrangement.rows) {
        NSString* pattern = [NSString stringWithUTF8String:
            source.patternId.c_str()];
        S3GTrackerSongRow* row = [self newRowWithPattern:
            pattern ? pattern : @"A01"];
        row.ticks = static_cast<NSInteger>(source.durationTicks);
        row.repeats = static_cast<NSInteger>(source.repeats);
        row.bpm = source.bpm.value_or(126.0);
        row.hasBPMOverride = source.bpm.has_value();
        row.swing = source.swing.value_or(0.56) * 100.0;
        row.hasSwingOverride = source.swing.has_value();
        [row.mutedLanes removeAllIndexes];
        for (NSUInteger lane = 0u; lane < 32u; ++lane) {
            if ((source.mutedTracks & (1u << lane)) != 0u)
                [row.mutedLanes addIndex:lane];
        }
        [self.rows addObject:row];
    }
    [self.tableView reloadData];
    if (self.rows.count > 0u) {
        [self.tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:0u]
            byExtendingSelection:NO];
    } else {
        [self.tableView deselectAll:nil];
        self.removeButton.enabled = NO;
    }
    [self songDidChange];
}

@end
