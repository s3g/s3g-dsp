#import "s3g_tracker_assemble_view.h"
#import "s3g_tracker_controls.h"

#include "s3g/tracker/fx_catalog.h"
#include "s3g_gui_layout.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

using namespace s3g::tracker;
using namespace s3g::tracker::app;

constexpr CGFloat kAssemblyRowHeight = 6.0;
constexpr CGFloat kAssemblyMinimumBlockHeight = 22.0;
constexpr CGFloat kAssemblyHeaderHeight = 28.0;
constexpr CGFloat kAssemblyGutterWidth = 50.0;

NSString *assemblyString(const std::string &value) {
  NSString *string = [NSString stringWithUTF8String:value.c_str()];
  return string ? string : @"";
}

const PhraseLibrary *assemblyPhraseLibrary(const TrackerViewState *state,
                                           AssetBankId bankId) {
  if (!state)
    return nullptr;
  if (bankId == state->activePhraseBankId)
    return &state->phraseLibrary;
  const auto *bank = findPhraseBank(state->phraseBanks, bankId);
  return bank ? &bank->library : nullptr;
}

PhraseLibrary *mutableAssemblyPhraseLibrary(TrackerViewState *state,
                                            AssetBankId bankId) {
  if (!state)
    return nullptr;
  if (bankId == state->activePhraseBankId)
    return &state->phraseLibrary;
  auto *bank = findPhraseBank(state->phraseBanks, bankId);
  return bank ? &bank->library : nullptr;
}

const PhraseDefinition *assemblyPhrase(const TrackerViewState *state,
                                       const PhraseAssemblyBlock &block) {
  const auto *library = assemblyPhraseLibrary(state, block.phraseBankId);
  if (!library || block.phraseSlot >= library->phrases.size())
    return nullptr;
  const auto &phrase = library->phrases[block.phraseSlot];
  return phrase.empty() && phrase.name.empty() ? nullptr : &phrase;
}

std::size_t blockRows(const TrackerViewState *state,
                      const PhraseAssemblyBlock &block) {
  const auto *phrase = assemblyPhrase(state, block);
  return phrase ? phrase->length *
                      std::clamp<uint32_t>(block.repeats, 1u,
                                           kMaximumAssemblyBlockRepeats)
                : 0u;
}

CGFloat blockPixelHeight(const TrackerViewState *state,
                         const PhraseAssemblyBlock &block) {
  return std::max<CGFloat>(kAssemblyMinimumBlockHeight,
                           static_cast<CGFloat>(blockRows(state, block)) *
                               kAssemblyRowHeight);
}

std::size_t assemblyRows(const TrackerViewState *state) {
  if (!state)
    return 0u;
  std::size_t rows = 0u;
  for (const auto &block : state->assembly.blocks)
    rows += blockRows(state, block);
  return rows;
}

CGFloat assemblyPixelYForRow(const TrackerViewState *state, std::size_t row) {
  CGFloat y = kAssemblyHeaderHeight;
  std::size_t firstRow = 0u;
  if (!state)
    return y;
  for (const auto &block : state->assembly.blocks) {
    const std::size_t rows = blockRows(state, block);
    if (row < firstRow + rows)
      return y + static_cast<CGFloat>(row - firstRow) * kAssemblyRowHeight;
    firstRow += rows;
    y += blockPixelHeight(state, block);
  }
  return y;
}

bool noteIsEmpty(const NoteCell &cell) {
  return cell.state == NoteCellState::Rest;
}
bool valueIsEmpty(const ValueCell &cell) {
  return cell.state == ValueCellState::Default ||
         cell.state == ValueCellState::Previous;
}
bool actionIsEmpty(const FxActionCell &cell) {
  return cell.state == FxActionCellState::Empty ||
         cell.state == FxActionCellState::Previous;
}
bool fxValueIsEmpty(const FxValueCell &cell) {
  return cell.state == FxValueCellState::Previous;
}
bool gateIsEmpty(const GateCell &cell) { return cell.voiceCount == 0u; }

void copyPhraseRow(Track &track, const PhraseDefinition &phrase,
                   std::size_t source, std::size_t destination, bool merge) {
  const auto size = destination + 1u;
  track.notes.resize(std::max(track.notes.size(), size), NoteCell::rest());
  track.velocities.resize(std::max(track.velocities.size(), size),
                          ValueCell::defaultValue());
  track.gates.resize(std::max(track.gates.size(), size),
                     GateCell::defaultValue());
  if (source < phrase.notes.size() &&
      (!merge || noteIsEmpty(track.notes[destination])))
    track.notes[destination] = phrase.notes[source];
  if (source < phrase.velocities.size() &&
      (!merge || valueIsEmpty(track.velocities[destination])))
    track.velocities[destination] = phrase.velocities[source];
  if (source < phrase.gates.size() &&
      (!merge || gateIsEmpty(track.gates[destination])))
    track.gates[destination] = phrase.gates[source];
  for (std::size_t pairIndex = 0u; pairIndex < kFxPairCount; ++pairIndex) {
    auto &destinationPair = track.fxPairs[pairIndex];
    const auto &sourcePair = phrase.fxPairs[pairIndex];
    destinationPair.actions.resize(
        std::max(destinationPair.actions.size(), size), FxActionCell::empty());
    destinationPair.values.resize(std::max(destinationPair.values.size(), size),
                                  FxValueCell::previous());
    if (source < sourcePair.actions.size() &&
        (!merge || actionIsEmpty(destinationPair.actions[destination])))
      destinationPair.actions[destination] = sourcePair.actions[source];
    if (source < sourcePair.values.size() &&
        (!merge || fxValueIsEmpty(destinationPair.values[destination])))
      destinationPair.values[destination] = sourcePair.values[source];
  }
}

uint16_t previewPosition(const PhraseDefinition &phrase, std::size_t row,
                         std::size_t voice) {
  for (const auto &pair : phrase.fxPairs) {
    if (row >= pair.actions.size() || row >= pair.values.size())
      continue;
    const auto &action = pair.actions[row];
    const auto &value = pair.values[row];
    if (action.state == FxActionCellState::Sequencer &&
        action.sequencerAction == SequencerAction::MicroTime &&
        value.state == FxValueCellState::Value) {
      const float mt = std::clamp(value.valueVoice(voice), 0.0f, 1.0f);
      return static_cast<uint16_t>(std::lround(mt * 65535.0f));
    }
  }
  return 0u;
}

void appendPhrasePreviewEvents(const TrackerViewState *state,
                               const PhraseDefinition &phrase,
                               std::size_t rowOffset,
                               std::vector<PitchPreviewEvent> &events) {
  if (!state)
    return;
  float velocity = 100.0f / 127.0f;
  for (std::size_t row = 0u; row < phrase.length; ++row) {
    if (row < phrase.velocities.size() &&
        phrase.velocities[row].state == ValueCellState::Value)
      velocity = std::clamp(phrase.velocities[row].normalized, 0.0f, 1.0f);
    if (row >= phrase.notes.size())
      continue;
    const auto &note = phrase.notes[row];
    if (note.state == NoteCellState::Burst) {
      const auto *bank =
          state ? findBurstBank(state->burstBanks, note.burstBankId) : nullptr;
      const auto *library = note.burstBankId == state->activeBurstBankId
                                ? &state->session.burstLibrary
                                : bank ? &bank->library : nullptr;
      if (!library || note.note >= library->bursts.size())
        continue;
      const auto &burst = library->bursts[note.note];
      for (std::size_t index = 0u; index < burst.eventCount; ++index) {
        const auto &event = burst.events[index];
        events.push_back({static_cast<uint16_t>(rowOffset + row), event.note,
                          event.velocity, event.gatePercent, event.position});
      }
    } else if (note.state == NoteCellState::Note) {
      for (std::size_t voice = 0u; voice < note.noteVoiceCount(); ++voice) {
        uint8_t gate = 70u;
        if (row < phrase.gates.size()) {
          const auto value = phrase.gates[row].gateVoice(voice);
          if (value.mode == GateVoiceMode::Rows)
            gate = static_cast<uint8_t>(
                std::clamp(std::lround(value.rows * 100.0f), 1l, 100l));
          else if (value.mode == GateVoiceMode::Tie)
            gate = 100u;
        }
        const float voiceVelocity =
            row < phrase.velocities.size() &&
                    phrase.velocities[row].state == ValueCellState::Value
                ? phrase.velocities[row].valueVoice(voice)
                : velocity;
        events.push_back(
            {static_cast<uint16_t>(rowOffset + row), note.noteVoice(voice),
             static_cast<uint8_t>(
                 std::lround(std::clamp(voiceVelocity, 0.0f, 1.0f) * 127.0f)),
             gate, previewPosition(phrase, row, voice)});
      }
    }
  }
}

} // namespace

@class S3GTrackerAssembleView;

@interface S3GTrackerAssembleRootView
    : S3GTrackerFocusReleaseView <S3GTrackerAssembleKeyHandling>
@property(nonatomic, weak) S3GTrackerAssembleView *owner;
@end

@interface S3GTrackerAssemblyCanvas : NSView
@property(nonatomic, weak) S3GTrackerAssembleView *owner;
@property(nonatomic, strong) NSMutableIndexSet *selectedBlocks;
@property(nonatomic) NSInteger dragBlock;
@property(nonatomic) NSInteger dropIndex;
@property(nonatomic) BOOL draggingBlocks;
- (void)reloadModel;
@end

@interface S3GTrackerAssembleView ()
@property(nonatomic, assign) TrackerViewState *trackerState;
@property(nonatomic, assign) WorkspaceCallbacks *trackerCallbacks;
@property(nonatomic, strong) S3GTrackerToolboxView *canvasPanel;
@property(nonatomic, strong) S3GTrackerToolboxView *phrasePanel;
@property(nonatomic, strong) S3GTrackerToolboxView *assemblyPanel;
@property(nonatomic, strong) S3GTrackerToolboxView *targetPanel;
@property(nonatomic, strong) NSScrollView *canvasScroll;
@property(nonatomic, strong) S3GTrackerAssemblyCanvas *canvas;
@property(nonatomic, strong) S3GTrackerPopupButton *bankPopup;
@property(nonatomic, strong) S3GTrackerPopupButton *phrasePopup;
@property(nonatomic, strong) S3GTrackerPopupButton *repeatPopup;
@property(nonatomic, strong) S3GTrackerPopupButton *scopePopup;
@property(nonatomic, strong) S3GTrackerPopupButton *channelPopup;
@property(nonatomic, strong) S3GTrackerPopupButton *patternPopup;
@property(nonatomic, strong) S3GTrackerPopupButton *lanePopup;
@property(nonatomic, strong) S3GTrackerPopupButton *rowPopup;
@property(nonatomic, strong) S3GTrackerPopupButton *modePopup;
@property(nonatomic, strong) S3GTrackerPopupButton *fitPopup;
@property(nonatomic, strong) S3GTrackerActionButton *appendButton;
@property(nonatomic, strong) S3GTrackerActionButton *previewButton;
@property(nonatomic, strong) S3GTrackerActionButton *loopButton;
@property(nonatomic, strong) S3GTrackerActionButton *upButton;
@property(nonatomic, strong) S3GTrackerActionButton *downButton;
@property(nonatomic, strong) S3GTrackerActionButton *duplicateButton;
@property(nonatomic, strong) S3GTrackerActionButton *deleteButton;
@property(nonatomic, strong) S3GTrackerActionButton *clearButton;
@property(nonatomic, strong) S3GTrackerActionButton *savePhraseButton;
@property(nonatomic, strong) S3GTrackerActionButton *placeButton;
@property(nonatomic, strong) S3GTrackerActionButton *revealButton;
@property(nonatomic, strong) NSTextField *summaryLabel;
@property(nonatomic, strong) NSTextField *statusLabel;
@property(nonatomic, strong) NSTimer *previewTimer;
@property(nonatomic) NSInteger previewRow;
@property(nonatomic) NSInteger previewEndRow;
@property(nonatomic) BOOL previewingAssembly;
- (void)layoutInterface;
- (void)assemblyEdited;
- (void)stopPreview;
- (void)moveSelectedBlocksToIndex:(NSUInteger)destination copy:(BOOL)copy;
- (void)deletePressed:(id)sender;
- (BOOL)s3gHandleAssembleKeyEquivalent:(NSEvent *)event;
@end

@implementation S3GTrackerAssembleRootView
- (BOOL)isFlipped {
  return YES;
}
- (void)layout {
  [super layout];
  [self.owner layoutInterface];
}
- (BOOL)s3gHandleAssembleKeyEquivalent:(NSEvent *)event {
  return [self.owner s3gHandleAssembleKeyEquivalent:event];
}
@end

@implementation S3GTrackerAssemblyCanvas

- (instancetype)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (self) {
    self.wantsLayer = YES;
    self.selectedBlocks = [[NSMutableIndexSet alloc] init];
    self.dragBlock = -1;
    self.dropIndex = -1;
  }
  return self;
}

- (BOOL)isFlipped {
  return YES;
}
- (BOOL)acceptsFirstResponder {
  return YES;
}

- (NSInteger)blockAtPoint:(NSPoint)point insertion:(BOOL)insertion {
  auto *state = self.owner.trackerState;
  if (!state || point.y < kAssemblyHeaderHeight)
    return insertion ? 0 : -1;
  CGFloat y = kAssemblyHeaderHeight;
  for (NSUInteger index = 0u; index < state->assembly.blocks.size(); ++index) {
    const CGFloat height =
        blockPixelHeight(state, state->assembly.blocks[index]);
    if (insertion && point.y < y + height * 0.5)
      return index;
    if (!insertion && point.y >= y && point.y < y + height)
      return index;
    y += height;
  }
  return insertion ? static_cast<NSInteger>(state->assembly.blocks.size()) : -1;
}

- (void)mouseDown:(NSEvent *)event {
  const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
  const NSInteger index = [self blockAtPoint:point insertion:NO];
  if (index < 0) {
    [self.selectedBlocks removeAllIndexes];
    [self setNeedsDisplay:YES];
    return;
  }
  if ((event.modifierFlags & NSEventModifierFlagShift) != 0u) {
    if ([self.selectedBlocks containsIndex:(NSUInteger)index])
      [self.selectedBlocks removeIndex:(NSUInteger)index];
    else
      [self.selectedBlocks addIndex:(NSUInteger)index];
  } else if (![self.selectedBlocks containsIndex:(NSUInteger)index]) {
    [self.selectedBlocks removeAllIndexes];
    [self.selectedBlocks addIndex:(NSUInteger)index];
  }
  self.dragBlock = index;
  self.dropIndex = index;
  self.draggingBlocks = NO;
  [self.window makeFirstResponder:self];
  [self setNeedsDisplay:YES];
}

- (void)mouseDragged:(NSEvent *)event {
  if (self.dragBlock < 0)
    return;
  self.draggingBlocks = YES;
  const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
  self.dropIndex = [self blockAtPoint:point insertion:YES];
  [self autoscroll:event];
  [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent *)event {
  if (self.draggingBlocks && self.dropIndex >= 0)
    [self.owner moveSelectedBlocksToIndex:(NSUInteger)self.dropIndex
                                     copy:(event.modifierFlags &
                                           NSEventModifierFlagOption) != 0u];
  self.dragBlock = -1;
  self.dropIndex = -1;
  self.draggingBlocks = NO;
  [self setNeedsDisplay:YES];
}

- (void)keyDown:(NSEvent *)event {
  if (event.keyCode == 51u || event.keyCode == 117u) {
    [self.owner deletePressed:nil];
    return;
  }
  if ([self.owner s3gHandleAssembleKeyEquivalent:event])
    return;
  [super keyDown:event];
}

- (void)reloadModel {
  auto *state = self.owner.trackerState;
  const NSUInteger count = state ? state->assembly.blocks.size() : 0u;
  [self.selectedBlocks
      removeIndexesInRange:NSMakeRange(count, NSUIntegerMax - count)];
  CGFloat contentHeight = kAssemblyHeaderHeight + 12.0;
  if (state)
    for (const auto &block : state->assembly.blocks)
      contentHeight += blockPixelHeight(state, block);
  const CGFloat height =
      std::max<CGFloat>(contentHeight, NSHeight(self.superview.bounds));
  [self setFrameSize:NSMakeSize(std::max<CGFloat>(
                                    560.0, NSWidth(self.superview.bounds)),
                                height)];
  [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  auto *state = self.owner.trackerState;
  [S3GTrackerThemeColor(S3GTrackerThemeRole::Workspace) setFill];
  NSRectFill(self.bounds);
  [S3GTrackerThemeColor(S3GTrackerThemeRole::Raised) setFill];
  NSRectFill(NSMakeRect(0.0, 0.0, NSWidth(self.bounds), kAssemblyHeaderHeight));
  NSDictionary *header = @{
    NSFontAttributeName : S3GTrackerFont(9.0, NSFontWeightSemibold),
    NSForegroundColorAttributeName :
        S3GTrackerThemeColor(S3GTrackerThemeRole::TextSecondary)
  };
  [@"ROW" drawAtPoint:NSMakePoint(12.0, 8.0) withAttributes:header];
  NSString *lane =
      state && !state->session.pattern.tracks.empty()
          ? [NSString stringWithFormat:@"TARGET L%02u  /  PHRASE BLOCKS",
                                       state->assembly.targetTrack + 1u]
          : @"PHRASE BLOCKS";
  [lane drawAtPoint:NSMakePoint(kAssemblyGutterWidth + 10.0, 8.0)
      withAttributes:header];
  if (!state)
    return;
  CGFloat y = kAssemblyHeaderHeight;
  std::size_t startRow = state->assembly.targetRow;
  for (NSUInteger index = 0u; index < state->assembly.blocks.size(); ++index) {
    const auto &block = state->assembly.blocks[index];
    const auto *phrase = assemblyPhrase(state, block);
    const std::size_t rows = blockRows(state, block);
    const CGFloat height = blockPixelHeight(state, block);
    const BOOL selected = [self.selectedBlocks containsIndex:index];
    NSRect blockRect = NSMakeRect(
        kAssemblyGutterWidth + 4.0, y + 2.0,
        NSWidth(self.bounds) - kAssemblyGutterWidth - 12.0, height - 4.0);
    [(selected ? S3GTrackerThemeColor(S3GTrackerThemeRole::Selection)
               : S3GTrackerThemeColor(S3GTrackerThemeRole::Control)) setFill];
    [[NSBezierPath bezierPathWithRoundedRect:blockRect xRadius:3.0
                                     yRadius:3.0] fill];
    [(selected ? S3GTrackerThemeColor(S3GTrackerThemeRole::Focus)
               : S3GTrackerThemeColor(S3GTrackerThemeRole::Border)) setStroke];
    [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(blockRect, .5, .5)
                                     xRadius:3.0
                                     yRadius:3.0] stroke];
    NSString *identity =
        [NSString stringWithFormat:@"BK%03u:P%02u", block.phraseBankId,
                                   block.phraseSlot + 1u];
    NSString *title =
        phrase && !phrase->name.empty()
            ? [NSString
                  stringWithFormat:@"%@  ·  %@  ·  %lu ROWS%@", identity,
                                   assemblyString(phrase->name),
                                   static_cast<unsigned long>(phrase->length),
                                   block.repeats > 1u
                                       ? [NSString
                                             stringWithFormat:@" ×%u",
                                                              block.repeats]
                                       : @""]
            : [identity stringByAppendingString:@"  ·  MISSING"];
    NSDictionary *text = @{
      NSFontAttributeName : S3GTrackerFont(9.0, NSFontWeightMedium),
      NSForegroundColorAttributeName : phrase
          ? S3GTrackerThemeColor(S3GTrackerThemeRole::TextPrimary)
          : S3GTrackerThemeColor(S3GTrackerThemeRole::Danger)
    };
    [title drawInRect:NSInsetRect(blockRect, 8.0,
                                  std::max<CGFloat>(2.0, (height - 18.0) * 0.5))
        withAttributes:text];
    NSDictionary *rowText = @{
      NSFontAttributeName : S3GTrackerFont(8.0),
      NSForegroundColorAttributeName :
          S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted)
    };
    [[NSString
        stringWithFormat:@"%03lu", static_cast<unsigned long>(startRow + 1u)]
           drawAtPoint:NSMakePoint(10.0, y + 5.0)
        withAttributes:rowText];
    if (self.owner.previewingAssembly && self.owner.previewRow >= 0 &&
        self.owner.previewRow >=
            static_cast<NSInteger>(startRow - state->assembly.targetRow) &&
        self.owner.previewRow <
            static_cast<NSInteger>(startRow - state->assembly.targetRow +
                                   rows)) {
      const CGFloat py =
          y + static_cast<CGFloat>(self.owner.previewRow -
                                   static_cast<NSInteger>(
                                       startRow - state->assembly.targetRow)) *
                  kAssemblyRowHeight;
      [S3GTrackerThemeColor(S3GTrackerThemeRole::Live, 0.85) setFill];
      NSRectFill(NSMakeRect(kAssemblyGutterWidth + 4.0, py,
                            NSWidth(self.bounds) - kAssemblyGutterWidth - 12.0,
                            2.0));
    }
    y += height;
    startRow += rows;
  }
  if (self.draggingBlocks && self.dropIndex >= 0) {
    CGFloat lineY = kAssemblyHeaderHeight;
    for (NSInteger index = 0;
         index < self.dropIndex &&
         index < static_cast<NSInteger>(state->assembly.blocks.size());
         ++index)
      lineY +=
          blockPixelHeight(state, state->assembly.blocks[(NSUInteger)index]);
    [S3GTrackerThemeColor(S3GTrackerThemeRole::Focus) setFill];
    NSRectFill(NSMakeRect(kAssemblyGutterWidth + 2.0, lineY - 2.0,
                          NSWidth(self.bounds) - kAssemblyGutterWidth - 6.0,
                          4.0));
  }
}

@end

@implementation S3GTrackerAssembleView

- (instancetype)initWithState:(TrackerViewState *)state
                    callbacks:(WorkspaceCallbacks *)callbacks {
  self = [super initWithNibName:nil bundle:nil];
  if (self) {
    self.trackerState = state;
    self.trackerCallbacks = callbacks;
    self.previewRow = -1;
    self.previewEndRow = -1;
  }
  return self;
}

- (void)dealloc {
  [self.previewTimer invalidate];
}

- (S3GTrackerActionButton *)button:(NSString *)title action:(SEL)action {
  S3GTrackerActionButton *button =
      [[S3GTrackerActionButton alloc] initWithFrame:NSZeroRect];
  button.s3gUsesSuiteStyle = YES;
  button.s3gUsesNeutralTitle = YES;
  button.title = title;
  button.target = self;
  button.action = action;
  return button;
}

- (NSTextField *)label:(NSString *)title panel:(NSView *)panel {
  S3GTrackerSuiteLabel *label =
      [[S3GTrackerSuiteLabel alloc] initWithFrame:NSZeroRect];
  label.stringValue = title;
  [panel addSubview:label];
  return label;
}

- (S3GTrackerPopupButton *)popup:(NSArray<NSString *> *)titles
                           panel:(NSView *)panel {
  S3GTrackerPopupButton *popup =
      [[S3GTrackerPopupButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  popup.s3gUsesCanvasMenu = YES;
  [popup addItemsWithTitles:titles];
  [panel addSubview:popup];
  return popup;
}

- (void)loadView {
  S3GTrackerAssembleRootView *root = [[S3GTrackerAssembleRootView alloc]
      initWithFrame:NSMakeRect(0.0, 0.0, 1320.0, 780.0)];
  root.wantsLayer = YES;
  root.layer.backgroundColor =
      S3GTrackerThemeColor(S3GTrackerThemeRole::Canvas).CGColor;
  root.owner = self;
  self.view = root;
  self.canvasPanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
  self.canvasPanel.toolboxTitle = @"ASSEMBLY TRAY  /  VERTICAL PATTERN FLOW";
  self.phrasePanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
  self.phrasePanel.toolboxTitle = @"PHRASE PALETTE";
  self.assemblyPanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
  self.assemblyPanel.toolboxTitle = @"ASSEMBLY / AUDITION";
  self.targetPanel = [[S3GTrackerToolboxView alloc] initWithFrame:NSZeroRect];
  self.targetPanel.toolboxTitle = @"PLACEMENT";
  for (NSView *panel in @[
         self.canvasPanel, self.phrasePanel, self.assemblyPanel,
         self.targetPanel
       ])
    [root addSubview:panel];
  self.canvas = [[S3GTrackerAssemblyCanvas alloc] initWithFrame:NSZeroRect];
  self.canvas.owner = self;
  self.canvasScroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
  self.canvasScroll.documentView = self.canvas;
  self.canvasScroll.hasVerticalScroller = YES;
  self.canvasScroll.hasHorizontalScroller = NO;
  self.canvasScroll.drawsBackground = YES;
  self.canvasScroll.backgroundColor =
      S3GTrackerThemeColor(S3GTrackerThemeRole::Workspace);
  [self.canvasPanel addSubview:self.canvasScroll];

  [self label:@"BANK" panel:self.phrasePanel];
  [self label:@"PHRASE" panel:self.phrasePanel];
  [self label:@"REPEAT" panel:self.phrasePanel];
  self.bankPopup = [self popup:@[] panel:self.phrasePanel];
  self.bankPopup.action = @selector(bankChanged:);
  self.bankPopup.target = self;
  self.phrasePopup = [self popup:@[] panel:self.phrasePanel];
  self.phrasePopup.action = @selector(phraseChanged:);
  self.phrasePopup.target = self;
  self.repeatPopup = [self popup:@[ @"1×", @"2×", @"4×", @"8×", @"16×" ]
                           panel:self.phrasePanel];
  self.appendButton = [self button:@"APPEND  [A]"
                            action:@selector(appendPressed:)];
  [self.phrasePanel addSubview:self.appendButton];

  [self label:@"PREVIEW" panel:self.assemblyPanel];
  [self label:@"MIDI CH" panel:self.assemblyPanel];
  self.scopePopup = [self popup:@[ @"ASSEMBLY", @"SELECTED PHRASE" ]
                          panel:self.assemblyPanel];
  NSMutableArray *channels = [NSMutableArray array];
  for (NSInteger channel = 1; channel <= 16; ++channel)
    [channels addObject:[NSString stringWithFormat:@"%02ld", channel]];
  self.channelPopup = [self popup:channels panel:self.assemblyPanel];
  self.channelPopup.target = self;
  self.channelPopup.action = @selector(channelChanged:);
  self.previewButton = [self button:@"LISTEN ▶"
                             action:@selector(previewPressed:)];
  self.previewButton.toolTip =
      @"Audition the selected Phrase or the complete Assembly at project tempo";
  self.loopButton = [self button:@"LOOP: OFF" action:@selector(loopPressed:)];
  self.upButton = [self button:@"↑" action:@selector(upPressed:)];
  self.downButton = [self button:@"↓" action:@selector(downPressed:)];
  self.duplicateButton = [self button:@"DUP"
                               action:@selector(duplicatePressed:)];
  self.deleteButton = [self button:@"DELETE" action:@selector(deletePressed:)];
  self.clearButton = [self button:@"CLEAR" action:@selector(clearPressed:)];
  self.savePhraseButton = [self button:@"SAVE AS PHRASE"
                                action:@selector(saveAsPhrasePressed:)];
  for (NSButton *button in @[
         self.previewButton, self.loopButton, self.upButton, self.downButton,
         self.duplicateButton, self.deleteButton, self.clearButton,
         self.savePhraseButton
       ])
    [self.assemblyPanel addSubview:button];
  self.summaryLabel = [self label:@"EMPTY" panel:self.assemblyPanel];

  [self label:@"PATTERN" panel:self.targetPanel];
  [self label:@"LANE" panel:self.targetPanel];
  [self label:@"ROW" panel:self.targetPanel];
  [self label:@"MODE" panel:self.targetPanel];
  [self label:@"FIT" panel:self.targetPanel];
  self.patternPopup = [self popup:@[] panel:self.targetPanel];
  self.patternPopup.target = self;
  self.patternPopup.action = @selector(patternChanged:);
  self.lanePopup = [self popup:@[] panel:self.targetPanel];
  self.lanePopup.target = self;
  self.lanePopup.action = @selector(laneChanged:);
  NSMutableArray *rows = [NSMutableArray array];
  for (NSInteger row = 1; row <= 256; ++row)
    [rows addObject:[NSString stringWithFormat:@"%03ld", row]];
  self.rowPopup = [self popup:rows panel:self.targetPanel];
  self.rowPopup.target = self;
  self.rowPopup.action = @selector(rowChanged:);
  self.modePopup = [self popup:@[ @"REPLACE", @"MERGE EMPTY" ]
                         panel:self.targetPanel];
  self.modePopup.target = self;
  self.modePopup.action = @selector(modeChanged:);
  self.fitPopup =
      [self popup:@[ @"EXTEND PATTERN", @"CROP AT 256", @"WRAP AT 256" ]
            panel:self.targetPanel];
  self.fitPopup.target = self;
  self.fitPopup.action = @selector(fitChanged:);
  self.placeButton = [self button:@"PLACE IN TRACKER"
                           action:@selector(placePressed:)];
  [self.targetPanel addSubview:self.placeButton];
  self.revealButton = [self button:@"REVEAL IN TRACKER"
                            action:@selector(revealPressed:)];
  [self.targetPanel addSubview:self.revealButton];
  self.statusLabel = [self label:@"" panel:self.targetPanel];
  self.statusLabel.font = S3GTrackerFont(8.0);
  self.statusLabel.textColor =
      S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted);
  [self layoutInterface];
  [self reloadModel];
}

- (void)layoutInterface {
  if (!self.isViewLoaded || !self.canvasPanel)
    return;
  const auto family = s3g::gui_layout::trackerGeometryFamilyLayout(
      {(double)NSWidth(self.view.bounds), (double)NSHeight(self.view.bounds)},
      9u, 4u, false);
  const auto rect = [](const s3g::gui_layout::Rect &r) {
    return NSMakeRect(r.x, r.y, r.width, r.height);
  };
  self.canvasPanel.frame = rect(family.fieldPanel);
  self.phrasePanel.frame = rect(family.laneCycle.frame);
  self.assemblyPanel.frame = rect(family.editShape.frame);
  self.targetPanel.frame = rect(family.trackerBridge.frame);
  const CGFloat header = s3g::gui_layout::kStandardMetrics.headerHeight;
  self.canvasScroll.frame =
      NSMakeRect(1, header, NSWidth(self.canvasPanel.bounds) - 2,
                 NSHeight(self.canvasPanel.bounds) - header - 1);
  const CGFloat lx = s3g::gui_layout::kStandardMetrics.labelInset,
                cx = s3g::gui_layout::kStandardMetrics.controlInset,
                first = s3g::gui_layout::kStandardMetrics.firstRowOffset,
                pitch = s3g::gui_layout::kStandardMetrics.rowPitch,
                right = s3g::gui_layout::kStandardMetrics.panelRightInset;
  const auto arrange = ^(S3GTrackerToolboxView *panel,
                         NSArray<NSView *> *labels,
                         NSArray<NSView *> *controls) {
    for (NSUInteger i = 0; i < labels.count; ++i)
      labels[i].frame = NSMakeRect(lx, first + i * pitch - 1, cx - lx - 6, 15);
    for (NSUInteger i = 0; i < controls.count; ++i)
      controls[i].frame = NSMakeRect(cx, first + i * pitch - 1,
                                     NSWidth(panel.bounds) - cx - right, 15);
  };
  arrange(
      self.phrasePanel,
      [self.phrasePanel.subviews
          filteredArrayUsingPredicate:[NSPredicate predicateWithBlock:^BOOL(
                                                       id v, NSDictionary *b) {
            (void)b;
            return [v isKindOfClass:S3GTrackerSuiteLabel.class];
      }]],
      @[ self.bankPopup, self.phrasePopup, self.repeatPopup ]);
  self.appendButton.frame =
      NSMakeRect(cx, first + 3 * pitch - 1,
                 NSWidth(self.phrasePanel.bounds) - cx - right, 15);
  NSArray *alabels = [self.assemblyPanel.subviews
      filteredArrayUsingPredicate:[NSPredicate predicateWithBlock:^BOOL(
                                                   id v, NSDictionary *b) {
        (void)b;
        return [v isKindOfClass:S3GTrackerSuiteLabel.class];
      }]];
  arrange(self.assemblyPanel, alabels, @[ self.scopePopup, self.channelPopup ]);
  const CGFloat aw = NSWidth(self.assemblyPanel.bounds) - cx - right, g = 4,
                w = (aw - g) / 2;
  const CGFloat auditionWidth = 78.0;
  self.previewButton.frame = NSMakeRect(
      NSWidth(self.assemblyPanel.bounds) - 12.0 - auditionWidth, 3.0,
      auditionWidth, 15.0);
  self.loopButton.frame = NSMakeRect(NSMinX(self.previewButton.frame) - g -
                                         auditionWidth,
                                     3.0, auditionWidth, 15.0);
  NSArray<NSView *> *edit = @[
    self.upButton, self.downButton, self.duplicateButton, self.deleteButton
  ];
  const CGFloat ew = (aw - 3 * g) / 4;
  for (NSUInteger i = 0; i < edit.count; ++i)
    ((NSView *)edit[i]).frame =
        NSMakeRect(cx + i * (ew + g), first + 5 * pitch, ew, 15);
  self.clearButton.frame = NSMakeRect(cx, first + 6 * pitch, w, 15);
  self.savePhraseButton.frame =
      NSMakeRect(cx + w + g, first + 6 * pitch, w, 15);
  self.summaryLabel.frame =
      NSMakeRect(lx, first + 8 * pitch,
                 NSWidth(self.assemblyPanel.bounds) - lx - right, 15);
  NSArray *tlabels = [self.targetPanel.subviews
      filteredArrayUsingPredicate:[NSPredicate predicateWithBlock:^BOOL(
                                                   id v, NSDictionary *b) {
        (void)b;
        return [v isKindOfClass:S3GTrackerSuiteLabel.class];
      }]];
  arrange(self.targetPanel, tlabels, @[
    self.patternPopup, self.lanePopup, self.rowPopup, self.modePopup,
    self.fitPopup
  ]);
  self.placeButton.frame =
      NSMakeRect(NSWidth(self.targetPanel.bounds) - 154, 3, 142, 15);
  self.revealButton.frame = NSMakeRect(
      cx, first + 6 * pitch, NSWidth(self.targetPanel.bounds) - cx - right, 15);
  self.statusLabel.frame = NSMakeRect(
      lx, first + 8 * pitch, NSWidth(self.targetPanel.bounds) - lx - right, 30);
  [self.canvas reloadModel];
}

- (void)reloadModel {
  if (!self.isViewLoaded || !self.trackerState)
    return;
  auto *state = self.trackerState;
  [self.bankPopup removeAllItems];
  NSInteger selectedBank = -1;
  for (const auto &bank : state->phraseBanks) {
    [self.bankPopup
        addItemWithTitle:[NSString
                             stringWithFormat:@"%@ · %@",
                                              assemblyString(
                                                  assetBankToken(bank.id)),
                                              assemblyString(bank.name)]];
    self.bankPopup.lastItem.representedObject = @(bank.id);
    if (bank.id == state->activePhraseBankId)
      selectedBank = self.bankPopup.numberOfItems - 1;
  }
  if (selectedBank >= 0)
    [self.bankPopup selectItemAtIndex:selectedBank];
  [self.phrasePopup removeAllItems];
  const auto *library = assemblyPhraseLibrary(state, state->activePhraseBankId);
  if (library)
    for (std::size_t slot = 0; slot < library->phrases.size(); ++slot) {
      const auto &phrase = library->phrases[slot];
      NSString *name =
          phrase.name.empty() ? @"EMPTY" : assemblyString(phrase.name);
      [self.phrasePopup
          addItemWithTitle:[NSString
                               stringWithFormat:@"P%02lu · %@ · %lu",
                                                (unsigned long)slot + 1, name,
                                                (unsigned long)phrase.length]];
      self.phrasePopup.lastItem.representedObject = @(slot);
    }
  [self.phrasePopup
      selectItemAtIndex:(NSInteger)std::min<std::size_t>(
                            state->selectedPhrase, kPhraseLibrarySlots - 1)];
  [self.channelPopup
      selectItemAtIndex:std::clamp<int>(state->assembly.previewMidiChannel, 1,
                                        16) -
                        1];
  [self.loopButton setState:state->assembly.loopPreview
                                ? NSControlStateValueOn
                                : NSControlStateValueOff];
  self.loopButton.title =
      state->assembly.loopPreview ? @"LOOP: ON" : @"LOOP: OFF";
  [self.patternPopup removeAllItems];
  NSInteger active = -1;
  for (const auto &entry : state->patternBank.entries) {
    [self.patternPopup
        addItemWithTitle:[NSString stringWithFormat:@"%@ · %@",
                                                    assemblyString(entry.id),
                                                    assemblyString(
                                                        entry.pattern.name)]];
    self.patternPopup.lastItem.representedObject = assemblyString(entry.id);
    if (entry.id == state->patternBank.activePatternId)
      active = self.patternPopup.numberOfItems - 1;
  }
  if (active >= 0)
    [self.patternPopup selectItemAtIndex:active];
  state->assembly.targetPatternId = state->patternBank.activePatternId;
  [self.lanePopup removeAllItems];
  for (std::size_t lane = 0; lane < state->session.pattern.tracks.size();
       ++lane) {
    [self.lanePopup
        addItemWithTitle:[NSString stringWithFormat:@"L%02lu · %@",
                                                    (unsigned long)lane + 1,
                                                    assemblyString(
                                                        state->session.pattern
                                                            .tracks[lane]
                                                            .name)]];
  }
  if (!state->session.pattern.tracks.empty()) {
    state->assembly.targetTrack =
        std::min<uint32_t>(state->assembly.targetTrack,
                           (uint32_t)state->session.pattern.tracks.size() - 1);
    [self.lanePopup selectItemAtIndex:state->assembly.targetTrack];
  }
  [self.rowPopup
      selectItemAtIndex:std::min<uint32_t>(state->assembly.targetRow, 255u)];
  [self.modePopup selectItemAtIndex:(NSInteger)state->assembly.placementMode];
  [self.fitPopup selectItemAtIndex:(NSInteger)state->assembly.fitMode];
  const auto rows = assemblyRows(state);
  self.summaryLabel.stringValue =
      [NSString stringWithFormat:@"%lu BLOCKS  ·  %lu ROWS",
                                 (unsigned long)state->assembly.blocks.size(),
                                 (unsigned long)rows];
  self.appendButton.enabled =
      state->assembly.blocks.size() < kMaximumAssemblyBlocks && rows < 256;
  self.placeButton.enabled = !state->assembly.blocks.empty() &&
                             !state->session.pattern.tracks.empty() &&
                             !state->songPlaybackActive;
  self.savePhraseButton.enabled =
      rows >= kMinimumPhraseRows && rows <= kMaximumPhraseRows;
  [self.canvas reloadModel];
}

- (void)assemblyEdited {
  if (self.trackerCallbacks && self.trackerCallbacks->patternChanged)
    self.trackerCallbacks->patternChanged();
  [self reloadModel];
}
- (void)bankChanged:(id)sender {
  (void)sender;
  NSNumber *bank = self.bankPopup.selectedItem.representedObject;
  if (bank && self.trackerCallbacks && self.trackerCallbacks->selectPhraseBank)
    self.trackerCallbacks->selectPhraseBank(bank.unsignedIntValue);
}
- (void)phraseChanged:(id)sender {
  (void)sender;
  self.trackerState->selectedPhrase =
      (std::size_t)self.phrasePopup.indexOfSelectedItem;
  [self reloadModel];
}
- (void)channelChanged:(id)sender {
  (void)sender;
  self.trackerState->assembly.previewMidiChannel =
      (uint8_t)(self.channelPopup.indexOfSelectedItem + 1);
  [self assemblyEdited];
}
- (void)patternChanged:(id)sender {
  (void)sender;
  NSString *pattern = self.patternPopup.selectedItem.representedObject;
  if (pattern && self.trackerCallbacks && self.trackerCallbacks->selectPattern)
    self.trackerCallbacks->selectPattern(pattern.UTF8String);
}
- (void)laneChanged:(id)sender {
  (void)sender;
  self.trackerState->assembly.targetTrack =
      (uint32_t)std::max<NSInteger>(0, self.lanePopup.indexOfSelectedItem);
  [self assemblyEdited];
}
- (void)rowChanged:(id)sender {
  (void)sender;
  self.trackerState->assembly.targetRow =
      (uint32_t)std::max<NSInteger>(0, self.rowPopup.indexOfSelectedItem);
  [self assemblyEdited];
}
- (void)modeChanged:(id)sender {
  (void)sender;
  self.trackerState->assembly.placementMode =
      (AssemblyPlacementMode)self.modePopup.indexOfSelectedItem;
  [self assemblyEdited];
}
- (void)fitChanged:(id)sender {
  (void)sender;
  self.trackerState->assembly.fitMode =
      (AssemblyFitMode)self.fitPopup.indexOfSelectedItem;
  [self assemblyEdited];
}
- (void)loopPressed:(id)sender {
  (void)sender;
  self.trackerState->assembly.loopPreview =
      !self.trackerState->assembly.loopPreview;
  [self assemblyEdited];
}

- (void)appendPressed:(id)sender {
  (void)sender;
  auto *state = self.trackerState;
  if (!state || state->assembly.blocks.size() >= kMaximumAssemblyBlocks)
    return;
  PhraseAssemblyBlock block;
  block.phraseBankId = state->activePhraseBankId;
  block.phraseSlot = (uint32_t)state->selectedPhrase;
  const uint32_t choices[] = {1, 2, 4, 8, 16};
  block.repeats = choices[std::clamp<NSInteger>(
      self.repeatPopup.indexOfSelectedItem, 0, 4)];
  const auto *phrase = assemblyPhrase(state, block);
  if (!phrase || assemblyRows(state) + phrase->length * block.repeats > 256u) {
    NSBeep();
    return;
  }
  state->assembly.blocks.push_back(block);
  [self.canvas.selectedBlocks removeAllIndexes];
  [self.canvas.selectedBlocks addIndex:state->assembly.blocks.size() - 1u];
  [self assemblyEdited];
}

- (void)moveSelectedBlocksToIndex:(NSUInteger)destination copy:(BOOL)copy {
  auto *state = self.trackerState;
  if (!state || self.canvas.selectedBlocks.count == 0)
    return;
  std::vector<PhraseAssemblyBlock> moving;
  for (NSUInteger index = self.canvas.selectedBlocks.firstIndex;
       index != NSNotFound;
       index = [self.canvas.selectedBlocks indexGreaterThanIndex:index])
    if (index < state->assembly.blocks.size())
      moving.push_back(state->assembly.blocks[index]);
  if (copy &&
      state->assembly.blocks.size() + moving.size() > kMaximumAssemblyBlocks) {
    NSBeep();
    return;
  }
  NSUInteger removedBefore = 0;
  if (!copy) {
    for (NSUInteger index = self.canvas.selectedBlocks.firstIndex;
         index != NSNotFound;
         index = [self.canvas.selectedBlocks indexGreaterThanIndex:index])
      if (index < destination)
        ++removedBefore;
    for (NSUInteger index = self.canvas.selectedBlocks.lastIndex;
         index != NSNotFound;
         index = [self.canvas.selectedBlocks indexLessThanIndex:index])
      if (index < state->assembly.blocks.size())
        state->assembly.blocks.erase(state->assembly.blocks.begin() +
                                     (std::ptrdiff_t)index);
    destination -= std::min(destination, removedBefore);
  }
  destination =
      std::min<NSUInteger>(destination, state->assembly.blocks.size());
  state->assembly.blocks.insert(state->assembly.blocks.begin() +
                                    (std::ptrdiff_t)destination,
                                moving.begin(), moving.end());
  [self.canvas.selectedBlocks removeAllIndexes];
  [self.canvas.selectedBlocks
      addIndexesInRange:NSMakeRange(destination, moving.size())];
  [self assemblyEdited];
}
- (void)upPressed:(id)sender {
  (void)sender;
  NSUInteger first = self.canvas.selectedBlocks.firstIndex;
  if (first != NSNotFound && first > 0)
    [self moveSelectedBlocksToIndex:first - 1 copy:NO];
}
- (void)downPressed:(id)sender {
  (void)sender;
  NSUInteger last = self.canvas.selectedBlocks.lastIndex;
  if (last != NSNotFound &&
      last + 1 < self.trackerState->assembly.blocks.size())
    [self moveSelectedBlocksToIndex:last + 2 copy:NO];
}
- (void)duplicatePressed:(id)sender {
  (void)sender;
  NSUInteger last = self.canvas.selectedBlocks.lastIndex;
  if (last != NSNotFound)
    [self moveSelectedBlocksToIndex:last + 1 copy:YES];
}
- (void)deletePressed:(id)sender {
  (void)sender;
  auto *state = self.trackerState;
  if (!state)
    return;
  [self.canvas.selectedBlocks
      enumerateIndexesWithOptions:NSEnumerationReverse
                       usingBlock:^(NSUInteger index, BOOL *stop) {
                         (void)stop;
                         if (index < state->assembly.blocks.size())
                           state->assembly.blocks.erase(
                               state->assembly.blocks.begin() +
                               (std::ptrdiff_t)index);
                       }];
  [self.canvas.selectedBlocks removeAllIndexes];
  [self assemblyEdited];
}
- (void)clearPressed:(id)sender {
  (void)sender;
  [self stopPreview];
  self.trackerState->assembly.blocks.clear();
  [self.canvas.selectedBlocks removeAllIndexes];
  [self assemblyEdited];
}

- (std::vector<PitchPreviewEvent>)previewEventsForAssembly:(BOOL)whole
                                                  firstRow:
                                                      (std::size_t *)firstRow {
  std::vector<PitchPreviewEvent> events;
  auto *state = self.trackerState;
  if (!state)
    return events;
  std::size_t offset = 0;
  if (!whole) {
    PhraseAssemblyBlock block{state->activePhraseBankId,
                              (uint32_t)state->selectedPhrase, 1};
    const auto *phrase = assemblyPhrase(state, block);
    if (phrase)
      appendPhrasePreviewEvents(state, *phrase, 0, events);
  } else
    for (const auto &block : state->assembly.blocks) {
      const auto *phrase = assemblyPhrase(state, block);
      if (!phrase)
        continue;
      for (uint32_t repeat = 0; repeat < block.repeats; ++repeat) {
        appendPhrasePreviewEvents(state, *phrase, offset, events);
        offset += phrase->length;
      }
    }
  if (events.empty())
    return events;
  std::size_t first = events.front().row;
  for (const auto &event : events)
    first = std::min<std::size_t>(first, event.row);
  for (auto &event : events)
    event.row = (uint16_t)(event.row - first);
  if (firstRow)
    *firstRow = first;
  return events;
}

- (void)startPreviewWhole:(BOOL)whole {
  if (!self.trackerCallbacks || !self.trackerCallbacks->previewPitchSequence)
    return;
  std::size_t first = 0;
  auto events = [self previewEventsForAssembly:whole firstRow:&first];
  if (events.empty()) {
    NSBeep();
    return;
  }
  auto *state = self.trackerState;
  const double bpm =
      state->hostBpm > 0 ? state->hostBpm : state->session.transport.bpm;
  [self stopPreview];
  self.previewButton.state = NSControlStateValueOn;
  self.trackerCallbacks->previewPitchSequence(
      events, state->assembly.previewMidiChannel, bpm,
      state->session.transport.ticksPerBeat);
  self.previewingAssembly = whole;
  self.previewRow = (NSInteger)first;
  self.previewEndRow = (NSInteger)(
      (whole ? assemblyRows(state)
             : assemblyPhrase(state, {state->activePhraseBankId,
                                      (uint32_t)state->selectedPhrase, 1})
                   ->length) -
      1);
  [self.canvas setNeedsDisplay:YES];
  const double seconds =
      60.0 /
      (std::max(1.0, bpm) *
       std::clamp<uint32_t>(state->session.transport.ticksPerBeat, 1, 96));
  __weak S3GTrackerAssembleView *weak = self;
  self.previewTimer = [NSTimer
      timerWithTimeInterval:seconds
                    repeats:YES
                      block:^(NSTimer *timer) {
                        S3GTrackerAssembleView *owner = weak;
                        if (!owner || owner.trackerState->playing) {
                          [timer invalidate];
                          [owner stopPreview];
                          return;
                        }
                        if (owner.previewRow >= owner.previewEndRow) {
                          if (owner.trackerState->assembly.loopPreview) {
                            [timer invalidate];
                            owner.previewTimer = nil;
                            [owner startPreviewWhole:whole];
                          } else
                            [owner stopPreview];
                          return;
                        }
                        ++owner.previewRow;
                        [owner.canvas setNeedsDisplay:YES];
                        [owner.canvas
                            scrollRectToVisible:NSMakeRect(
                                                    0,
                                                    assemblyPixelYForRow(
                                                        owner.trackerState,
                                                        owner.previewRow),
                                                    NSWidth(
                                                        owner.canvas.bounds),
                                                    kAssemblyRowHeight)];
                      }];
  [[NSRunLoop mainRunLoop] addTimer:self.previewTimer
                            forMode:NSRunLoopCommonModes];
}
- (void)previewPressed:(id)sender {
  (void)sender;
  if (self.previewTimer) {
    [self stopPreview];
    return;
  }
  [self startPreviewWhole:self.scopePopup.indexOfSelectedItem == 0];
}
- (void)stopPreview {
  [self.previewTimer invalidate];
  self.previewTimer = nil;
  self.previewRow = -1;
  self.previewEndRow = -1;
  self.previewingAssembly = NO;
  self.previewButton.state = NSControlStateValueOff;
  [self.canvas setNeedsDisplay:YES];
}

- (void)placePressed:(id)sender {
  (void)sender;
  auto *state = self.trackerState;
  if (!state || state->songPlaybackActive ||
      state->assembly.targetTrack >= state->session.pattern.tracks.size())
    return;
  auto candidate = state->session.pattern;
  auto &track = candidate.tracks[state->assembly.targetTrack];
  const bool merge =
      state->assembly.placementMode == AssemblyPlacementMode::MergeIntoEmpty;
  std::size_t sequenceRow = 0, maxWritten = candidate.visibleRows;
  for (const auto &block : state->assembly.blocks) {
    const auto *phrase = assemblyPhrase(state, block);
    if (!phrase)
      continue;
    for (uint32_t repeat = 0; repeat < block.repeats; ++repeat)
      for (std::size_t row = 0; row < phrase->length; ++row, ++sequenceRow) {
        std::size_t destination = state->assembly.targetRow + sequenceRow;
        if (destination >= 256u) {
          if (state->assembly.fitMode == AssemblyFitMode::Crop)
            goto placed;
          if (state->assembly.fitMode == AssemblyFitMode::Wrap)
            destination %= 256u;
          else {
            NSBeep();
            self.statusLabel.stringValue = @"ASSEMBLY EXCEEDS ROW 256";
            return;
          }
        }
        copyPhraseRow(track, *phrase, row, destination, merge);
        maxWritten = std::max(maxWritten, destination + 1u);
      }
  }
placed:
  candidate.visibleRows = std::min<std::size_t>(256u, maxWritten);
  track.noteColumn.length =
      std::max(track.noteColumn.length, candidate.visibleRows);
  track.velocityColumn.length =
      std::max(track.velocityColumn.length, candidate.visibleRows);
  track.gateColumn.length =
      std::max(track.gateColumn.length, candidate.visibleRows);
  for (auto &pair : track.fxPairs) {
    pair.actionColumn.length =
        std::max(pair.actionColumn.length, candidate.visibleRows);
    pair.valueColumn.length =
        std::max(pair.valueColumn.length, candidate.visibleRows);
  }
  state->session.pattern = std::move(candidate);
  state->session.selectedTrack = state->assembly.targetTrack;
  state->session.selectedRow = state->assembly.targetRow;
  self.statusLabel.stringValue = [NSString
      stringWithFormat:@"PLACED %lu ROWS IN L%02u", (unsigned long)sequenceRow,
                       state->assembly.targetTrack + 1u];
  self.placeButton.state = NSControlStateValueOn;
  dispatch_after(
      dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.16 * NSEC_PER_SEC)),
      dispatch_get_main_queue(), ^{
        self.placeButton.state = NSControlStateValueOff;
      });
  [self assemblyEdited];
}

- (void)revealPressed:(id)sender {
  (void)sender;
  if (!self.trackerState)
    return;
  self.trackerState->session.selectedTrack =
      self.trackerState->assembly.targetTrack;
  self.trackerState->session.selectedRow =
      self.trackerState->assembly.targetRow;
  if (self.trackerCallbacks && self.trackerCallbacks->showTrackerPage)
    self.trackerCallbacks->showTrackerPage();
}

- (void)saveAsPhrasePressed:(id)sender {
  (void)sender;
  auto *state = self.trackerState;
  const auto rows = assemblyRows(state);
  if (!state || rows < 2 || rows > 64) {
    NSBeep();
    return;
  }
  auto *library = mutableAssemblyPhraseLibrary(state, kProjectAssetBankId);
  if (!library)
    return;
  auto found = std::find_if(library->phrases.begin(), library->phrases.end(),
                            [](const PhraseDefinition &phrase) {
                              return phrase.empty() && phrase.name.empty();
                            });
  if (found == library->phrases.end()) {
    NSBeep();
    return;
  }
  Track track;
  track.notes.resize(rows, NoteCell::rest());
  track.velocities.resize(rows, ValueCell::defaultValue());
  track.gates.resize(rows, GateCell::defaultValue());
  for (auto &pair : track.fxPairs) {
    pair.actions.resize(rows, FxActionCell::empty());
    pair.values.resize(rows, FxValueCell::previous());
  }
  std::size_t at = 0;
  for (const auto &block : state->assembly.blocks) {
    const auto *phrase = assemblyPhrase(state, block);
    if (!phrase)
      continue;
    for (uint32_t repeat = 0; repeat < block.repeats; ++repeat)
      for (std::size_t row = 0; row < phrase->length; ++row)
        copyPhraseRow(track, *phrase, row, at++, false);
  }
  PhraseDefinition result = makeBlankPhrase(rows);
  capturePhrase(track, 0, rows - 1, result);
  const std::size_t destination =
      (std::size_t)(found - library->phrases.begin());
  result.name = "ASSEMBLY " + std::to_string(destination + 1u);
  result.previewMidiChannel = state->assembly.previewMidiChannel;
  *found = std::move(result);
  if (state->activePhraseBankId != kProjectAssetBankId)
    state->phraseLibrary = *library;
  state->activePhraseBankId = kProjectAssetBankId;
  state->selectedPhrase = destination;
  self.statusLabel.stringValue = @"SAVED TO PROJECT PHRASES";
  [self assemblyEdited];
}

- (BOOL)s3gHandleAssembleKeyEquivalent:(NSEvent *)event {
  const auto modifiers =
      event.modifierFlags &
      (NSEventModifierFlagCommand | NSEventModifierFlagControl |
       NSEventModifierFlagOption | NSEventModifierFlagShift);
  if (modifiers == 0 &&
      [[event.characters lowercaseString] isEqualToString:@"a"]) {
    [self appendPressed:nil];
    return YES;
  }
  if (modifiers == 0 && event.keyCode == 49u) {
    [self previewPressed:nil];
    return YES;
  }
  return NO;
}
- (void)refreshPlaybackDisplay {
  if (self.trackerState && self.trackerState->playing)
    [self stopPreview];
}

@end
