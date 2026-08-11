// The owning translation unit hosts this canvas in the shared
// ResponsiveViewport; the controls below follow the Processor-family contract.
constexpr s3g::gui_layout::Canvas kFeedbackCanvas {
    static_cast<double>(kGuiWidth), static_cast<double>(kGuiHeight) };
constexpr NSRect kFeedbackPageBand {
    18.0, 42.0, 1064.0, 24.0 };
constexpr s3g::gui_layout::Panel kMatrixPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Routing,
    { 18.0, 74.0, 640.0, 668.0 }, 36.0, 26.0, 0u };
constexpr s3g::gui_layout::Panel kPatchScenePanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Modulation,
    { 670.0, 74.0, 412.0, 188.0 }, 54.0, 30.0, 4u };
constexpr s3g::gui_layout::Panel kNodeStripPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 18.0, 74.0, 1064.0, 62.0 }, 0.0, 0.0, 0u };
constexpr s3g::gui_layout::Panel kNodePanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 18.0, 148.0, 340.0, 594.0 }, 54.0, 45.0, 9u };
constexpr s3g::gui_layout::Panel kInsertSelectorPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 370.0, 148.0, 712.0, 106.0 }, 52.0, 30.0, 2u };
constexpr s3g::gui_layout::Panel kAuxSendPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 18.0, 74.0, 1064.0, 250.0 }, 0.0, 0.0, 0u };
constexpr s3g::gui_layout::Panel kAuxSendLeftPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 18.0, 74.0, 532.0, 250.0 }, 62.0, 38.0, 4u };
constexpr s3g::gui_layout::Panel kAuxSendRightPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 550.0, 74.0, 532.0, 250.0 }, 62.0, 38.0, 4u };
constexpr s3g::gui_layout::Panel kAuxFeedbackPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::ToneShape,
    { 18.0, 336.0, 520.0, 406.0 }, 58.0, 48.0, 6u };
constexpr s3g::gui_layout::Panel kPostGrainPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::ToneShape,
    { 550.0, 336.0, 532.0, 406.0 }, 50.0, 34.0, 10u };
constexpr s3g::gui_layout::Panel kExcitationPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Engine,
    { 18.0, 74.0, 340.0, 410.0 }, 58.0, 48.0, 5u };
constexpr s3g::gui_layout::Panel kMorphPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Modulation,
    { 370.0, 74.0, 712.0, 410.0 }, 58.0, 48.0, 5u };
constexpr s3g::gui_layout::Panel kMorphLeftPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Modulation,
    { 370.0, 74.0, 356.0, 410.0 }, 58.0, 48.0, 5u };
constexpr s3g::gui_layout::Panel kMorphRightPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Modulation,
    { 726.0, 74.0, 356.0, 410.0 }, 58.0, 48.0, 5u };
constexpr s3g::gui_layout::Panel kOutputPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Output,
    { 18.0, 496.0, 340.0, 246.0 }, 58.0, 44.0, 4u };
constexpr s3g::gui_layout::Panel kEcologyActivityPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 370.0, 496.0, 712.0, 246.0 }, 0.0, 0.0, 0u };
constexpr s3g::gui_layout::Panel kCrosspointPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Routing,
    { 670.0, 274.0, 412.0, 468.0 }, 62.0, 42.0, 2u };

constexpr CGFloat kFeedbackMatrixX = 150.0;
constexpr CGFloat kFeedbackMatrixY = 145.0;
constexpr CGFloat kFeedbackMatrixCell = 50.0;
constexpr CGFloat kFeedbackMenuItemHeight = 18.0;

enum FeedbackPage : uint32_t {
    kFeedbackPagePatch = 0u,
    kFeedbackPageInsert,
    kFeedbackPageEcology,
    kFeedbackPageAux,
    kFeedbackPageCount,
};

enum FeedbackMenu : int {
    kFeedbackMenuNone = 0,
    kFeedbackMenuPreset,
    kFeedbackMenuNodeMode,
    kFeedbackMenuInsertCategory,
    kFeedbackMenuInsertEffect,
    kFeedbackMenuExciterSource,
    kFeedbackMenuMorphSource,
    kFeedbackMenuOutputMode,
    kFeedbackMenuRun,
    kFeedbackMenuMorphHold,
    kFeedbackMenuMorphSync,
    kFeedbackMenuMorphDivision,
    kFeedbackMenuMorphShape,
    kFeedbackMenuEffectBase = 100,
};

s3g::gui_layout::Panel feedbackEffectPanel(uint32_t rows)
{
    const double rowPitch = s3g::gui_layout::kStandardMetrics.rowPitch;
    (void)rows;
    return {
        s3g::gui_layout::PluginClass::MixerMatrixLane,
        s3g::gui_layout::PanelRole::ToneShape,
        { 370.0, 266.0, 712.0, 476.0 },
        s3g::gui_layout::kStandardMetrics.firstRowOffset, rowPitch, rows,
    };
}

NSRect feedbackMatrixCellRect(uint32_t destination, uint32_t source)
{
    return NSMakeRect(kFeedbackMatrixX
            + static_cast<CGFloat>(source) * kFeedbackMatrixCell,
        kFeedbackMatrixY
            + static_cast<CGFloat>(destination) * kFeedbackMatrixCell,
        34.0, 34.0);
}

NSRect feedbackPageButtonRect(uint32_t page)
{
    return NSMakeRect(28.0 + static_cast<CGFloat>(page) * 92.0,
        44.0, 86.0, 20.0);
}

NSRect feedbackNodeButtonRect(uint32_t page, uint32_t node)
{
    if (page == kFeedbackPageInsert) {
        return NSMakeRect(34.0 + static_cast<CGFloat>(node) * 129.0,
            104.0, 58.0, 22.0);
    }
    return NSMakeRect(386.0 + static_cast<CGFloat>(node) * 84.0,
        528.0, 72.0, 22.0);
}

NSRect feedbackNodeRandomButtonRect(uint32_t node)
{
    return NSMakeRect(96.0 + static_cast<CGFloat>(node) * 129.0,
        104.0, 58.0, 22.0);
}

NSRect feedbackSceneButtonRect(bool sceneB)
{
    return NSMakeRect(sceneB ? 748.0 : 710.0, 44.0, 32.0, 20.0);
}

NSRect feedbackRandomButtonRect(bool sceneB)
{
    return NSMakeRect(sceneB ? 882.0 : 790.0, 44.0, 86.0, 20.0);
}

NSRect feedbackSceneCopyButtonRect(bool toSceneB)
{
    return NSMakeRect(686.0, toSceneB ? 140.0 : 170.0,
        380.0, 20.0);
}

NSRect feedbackProcessorMenuRect(
    const s3g::gui_layout::Panel& panel, uint32_t row)
{
    return NSMakeRect(
        s3g::gui_layout::processorControlX(panel.frame.x),
        s3g::gui_layout::rowY(panel, row) - 1.0,
        s3g::gui_layout::processorMenuWidth(panel.frame.width), 15.0);
}

NSRect feedbackActionButtonRect(uint32_t index)
{
    const CGFloat y = s3g::gui_layout::rowY(kExcitationPanel, 4u) - 2.0;
    return NSMakeRect(34.0 + static_cast<CGFloat>(index) * 150.0,
        y, 140.0, 20.0);
}

NSRect feedbackEngineBankButtonRect(uint32_t index)
{
    return NSMakeRect(221.0 + static_cast<CGFloat>(index) * 40.0,
        80.0, 35.0, 18.0);
}

NSRect feedbackCrosspointActionRect(uint32_t index)
{
    const CGFloat x = s3g::gui_layout::processorControlX(
        kCrosspointPanel.frame.x);
    const CGFloat y = s3g::gui_layout::rowY(kCrosspointPanel, 1u) - 2.0;
    return NSMakeRect(x + static_cast<CGFloat>(index) * 140.0,
        y, 130.0, 20.0);
}

clap_id feedbackPedalStorageParamId(uint32_t node, uint32_t storageSlot)
{
    if (storageSlot < 4u) {
        return nodeParamId(node, kNodePedalAmountOffset + storageSlot);
    }
    return nodeParamId(node,
        kNodePedalExtra0Offset + storageSlot - 4u);
}

uint32_t feedbackVisiblePedalControlCount(s3g::FeedbackPedalType pedal)
{
    const uint32_t count = s3g::feedbackPedalControlCount(pedal);
    return pedal == s3g::FeedbackPedalType::Fracture && count > 0u
        ? count - 1u : count;
}

uint32_t feedbackVisiblePedalControlIndex(s3g::FeedbackPedalType pedal,
    uint32_t row)
{
    return pedal == s3g::FeedbackPedalType::Fracture ? row + 1u : row;
}

CGFloat feedbackNormalizedParam(const Plugin& plugin, clap_id id)
{
    uint32_t node = 0u;
    clap_id offset = 0u;
    const double value = paramValue(plugin, id);
    bool sceneB = false;
    if (decodeSceneParam(id, sceneB, node, offset)
        && offset == kSceneFrequencyOffset) {
        return s3g::feedbackShiftFrequencyControlNorm(
            static_cast<float>(value));
    }
    ParamRange range;
    if (!paramRange(id, range)) return 0.0;
    return static_cast<CGFloat>((value - range.minimum)
        / std::max(1.0e-9, range.maximum - range.minimum));
}

NSString* feedbackParamText(const Plugin& plugin, clap_id id)
{
    char text[48] {};
    paramsValueToText(&plugin.plugin, id, paramValue(plugin, id),
        text, sizeof(text));
    return [NSString stringWithUTF8String:text];
}

NSString* feedbackPedalControlText(
    const s3g::FeedbackPedalControlInfo& info,
    CGFloat normalized, s3g::FeedbackPedalType pedal, uint32_t control)
{
    normalized = std::clamp(normalized, 0.0, 1.0);
    using Display = s3g::FeedbackPedalControlDisplay;
    switch (info.display) {
    case Display::Percent:
        return [NSString stringWithFormat:@"%3.0f%%", normalized * 100.0];
    case Display::Bipolar: {
        const double value = info.displayMinimum
            + normalized * (info.displayMaximum - info.displayMinimum);
        return [NSString stringWithFormat:@"%+4.0f%%", value];
    }
    case Display::HertzLog: {
        const double value = info.displayMinimum * std::pow(
            info.displayMaximum / info.displayMinimum, normalized);
        if (value < 1.0)
            return [NSString stringWithFormat:@"%.2f Hz", value];
        if (value < 10.0)
            return [NSString stringWithFormat:@"%.1f Hz", value];
        return value < 1000.0
            ? [NSString stringWithFormat:@"%.0f Hz", value]
            : [NSString stringWithFormat:@"%.2f k", value * 0.001];
    }
    case Display::MillisecondsLog: {
        const double value = info.displayMinimum * std::pow(
            info.displayMaximum / info.displayMinimum, normalized);
        if (value < 1.0)
            return [NSString stringWithFormat:@"%.3f ms", value];
        if (value < 10.0)
            return [NSString stringWithFormat:@"%.2f ms", value];
        return [NSString stringWithFormat:@"%.0f ms", value];
    }
    case Display::MillisecondsSpacing: {
        const double value = normalized <= 0.001 ? 0.0
            : 2000.0 * std::pow((normalized - 0.001) / 0.999, 2.0);
        if (value < 10.0)
            return [NSString stringWithFormat:@"%.2f ms", value];
        return [NSString stringWithFormat:@"%.0f ms", value];
    }
    case Display::Count: {
        const double value = info.displayMinimum + normalized
            * (info.displayMaximum - info.displayMinimum);
        return [NSString stringWithFormat:@"%.0f", value];
    }
    case Display::Semitones: {
        const double value = info.displayMinimum + normalized
            * (info.displayMaximum - info.displayMinimum);
        return [NSString stringWithFormat:@"%+.1f st", value];
    }
    case Display::Decibels: {
        const double value = info.displayMinimum + normalized
            * (info.displayMaximum - info.displayMinimum);
        return [NSString stringWithFormat:@"%+.1f dB", value];
    }
    case Display::Menu: {
        const uint32_t item = info.menuCount > 1u
            ? static_cast<uint32_t>(std::lround(normalized
                * static_cast<CGFloat>(info.menuCount - 1u))) : 0u;
        return [NSString stringWithUTF8String:
            s3g::feedbackPedalMenuItem(pedal, control, item)];
    }
    case Display::Ratio: {
        const double value = info.displayMinimum + normalized
            * (info.displayMaximum - info.displayMinimum);
        return [NSString stringWithFormat:@"%.2fx", value];
    }
    }
    return @"";
}

NSString* feedbackAuxControlText(
    const s3g::FeedbackPedalControlInfo& info,
    CGFloat normalized, uint32_t control)
{
    normalized = std::clamp(normalized, 0.0, 1.0);
    if (info.display == s3g::FeedbackPedalControlDisplay::Menu) {
        const uint32_t item = info.menuCount > 1u
            ? static_cast<uint32_t>(std::lround(normalized
                * static_cast<CGFloat>(info.menuCount - 1u))) : 0u;
        return [NSString stringWithUTF8String:
            s3g::feedbackAuxMenuItem(control, item)];
    }
    return feedbackPedalControlText(
        info, normalized, s3g::FeedbackPedalType::Bypass, control);
}

void drawFeedbackActionButton(NSRect rect, NSString* title, bool active,
    const s3g::clap_gui::Style& style)
{
    [(active ? s3g::clap_gui::color(0x58675e) : style.strip) setFill];
    NSRectFill(rect);
    [style.grid setStroke];
    NSFrameRect(rect);
    NSDictionary* attrs = s3g::clap_gui::softLabelAttrs();
    const NSSize size = [title sizeWithAttributes:attrs];
    [title drawAtPoint:NSMakePoint(NSMidX(rect) - size.width * 0.5,
        NSMidY(rect) - size.height * 0.5) withAttributes:attrs];
}

@interface S3GFeedbackShiftView : NSView {
    void* _plugin;
    int _dragParam;
    CGFloat _dragPanelX;
    CGFloat _dragPanelWidth;
    uint32_t _selectedNode;
    bool _editingSceneB;
    bool _editingAux;
    uint32_t _selectedSource;
    uint32_t _selectedDestination;
    uint32_t _page;
    int _factoryPresetIndex;
    uint32_t _randomSeed;
    uint32_t _engineBank;
    char _titlePresetName[64];
    int _openMenu;
    uint32_t _openMenuControl;
    int _hoverMenuItem;
    NSPoint _menuOrigin;
    CGFloat _menuWidth;
    NSTimer* _timer;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)updateSlider:(NSPoint)point;
- (uint32_t)openMenuItemCount;
- (NSRect)openMenuRect;
- (void)updateMenuHover:(NSPoint)point;
- (void)openMenu:(int)menu control:(uint32_t)control rect:(NSRect)rect;
- (void)selectMenuItem:(uint32_t)item;
@end

@implementation S3GFeedbackShiftView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragParam = -1;
        _dragPanelX = 0.0;
        _dragPanelWidth = 0.0;
        _selectedNode = 0u;
        _editingSceneB = false;
        _editingAux = false;
        _selectedSource = 0u;
        _selectedDestination = 0u;
        _page = kFeedbackPagePatch;
        _factoryPresetIndex = 0;
        _randomSeed = 0x53334642u;
        _engineBank = 1u;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            s3g::kFeedbackShiftPresetInfo[0u].name);
        _openMenu = kFeedbackMenuNone;
        _openMenuControl = 0u;
        _hoverMenuItem = -1;
        _menuOrigin = NSZeroPoint;
        _menuWidth = 0.0;
        _timer = nil;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    NSArray* existing = [[self trackingAreas] copy];
    for (NSTrackingArea* area in existing) [self removeTrackingArea:area];
    [existing release];
    const NSTrackingAreaOptions options = NSTrackingMouseMoved
        | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect;
    NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:NSZeroRect
        options:options owner:self userInfo:nil];
    [self addTrackingArea:area];
    [area release];
}

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 30.0 target:self
        selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (_timer) {
        [_timer invalidate];
        _timer = nil;
    }
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if (![self isHidden] && _plugin
        && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES];
}

- (uint32_t)openMenuItemCount
{
    switch (_openMenu) {
    case kFeedbackMenuPreset: return s3g::kFeedbackShiftPresetCount;
    case kFeedbackMenuNodeMode: return 2u;
    case kFeedbackMenuInsertCategory:
        return s3g::kFeedbackInsertCategoryCount;
    case kFeedbackMenuInsertEffect: {
        const auto pedal = static_cast<s3g::FeedbackPedalType>(
            static_cast<uint32_t>(paramValue(
                *static_cast<Plugin*>(_plugin),
                nodeParamId(_selectedNode, kNodePedalOffset))));
        return s3g::feedbackInsertEffectCount(
            s3g::feedbackInsertCategory(pedal));
    }
    case kFeedbackMenuExciterSource:
        return s3g::kFeedbackExciterSourceCount;
    case kFeedbackMenuMorphSource:
        return s3g::kFeedbackMorphSourceCount;
    case kFeedbackMenuOutputMode: return s3g::kFeedbackShiftOutputModeCount;
    case kFeedbackMenuRun:
    case kFeedbackMenuMorphHold:
    case kFeedbackMenuMorphSync: return 2u;
    case kFeedbackMenuMorphDivision:
        return static_cast<uint32_t>(s3g::kFeedbackPulseDivisionBeats.size());
    case kFeedbackMenuMorphShape: return s3g::kFeedbackPulseShapeCount;
    default:
        if (_openMenu >= kFeedbackMenuEffectBase) {
            const auto pedal = static_cast<s3g::FeedbackPedalType>(
                static_cast<uint32_t>(paramValue(
                    *static_cast<Plugin*>(_plugin),
                    nodeParamId(_selectedNode, kNodePedalOffset))));
            return (_editingAux
                    ? s3g::feedbackAuxControlInfo(_openMenuControl)
                    : s3g::feedbackPedalControlInfo(
                        pedal, _openMenuControl)).menuCount;
        }
        return 0u;
    }
}

- (NSString*)menuItemText:(uint32_t)item
{
    switch (_openMenu) {
    case kFeedbackMenuPreset:
        return [NSString stringWithUTF8String:
            s3g::kFeedbackShiftPresetInfo[
                std::min(item, s3g::kFeedbackShiftPresetCount - 1u)].name];
    case kFeedbackMenuNodeMode: return item == 0u ? @"SHIFT" : @"RING";
    case kFeedbackMenuInsertCategory:
        return [NSString stringWithUTF8String:
            s3g::feedbackInsertCategoryName(
                static_cast<s3g::FeedbackInsertCategory>(std::min(item,
                    s3g::kFeedbackInsertCategoryCount - 1u)))];
    case kFeedbackMenuInsertEffect: {
        const auto pedal = static_cast<s3g::FeedbackPedalType>(
            static_cast<uint32_t>(paramValue(
                *static_cast<Plugin*>(_plugin),
                nodeParamId(_selectedNode, kNodePedalOffset))));
        return [NSString stringWithUTF8String:s3g::feedbackInsertEffectName(
            s3g::feedbackInsertCategory(pedal), item)];
    }
    case kFeedbackMenuExciterSource:
        return [NSString stringWithUTF8String:s3g::feedbackExciterSourceName(
            static_cast<s3g::FeedbackExciterSource>(std::min(item,
                s3g::kFeedbackExciterSourceCount - 1u)))];
    case kFeedbackMenuMorphSource:
        return [NSString stringWithUTF8String:s3g::feedbackMorphSourceName(
            static_cast<s3g::FeedbackMorphSource>(std::min(item,
                s3g::kFeedbackMorphSourceCount - 1u)))];
    case kFeedbackMenuOutputMode:
        return [NSString stringWithUTF8String:s3g::feedbackShiftOutputModeName(
            static_cast<s3g::FeedbackShiftOutputMode>(std::min(item,
                s3g::kFeedbackShiftOutputModeCount - 1u)))];
    case kFeedbackMenuRun:
    case kFeedbackMenuMorphHold:
    case kFeedbackMenuMorphSync: return item == 0u ? @"OFF" : @"ON";
    case kFeedbackMenuMorphDivision:
        return [NSString stringWithUTF8String:
            s3g::feedbackPulseDivisionName(item)];
    case kFeedbackMenuMorphShape:
        return [NSString stringWithUTF8String:s3g::feedbackPulseShapeName(
            static_cast<s3g::FeedbackPulseShape>(std::min(item,
                s3g::kFeedbackPulseShapeCount - 1u)))];
    default:
        if (_openMenu >= kFeedbackMenuEffectBase) {
            if (_editingAux) {
                return [NSString stringWithUTF8String:
                    s3g::feedbackAuxMenuItem(_openMenuControl, item)];
            }
            const auto pedal = static_cast<s3g::FeedbackPedalType>(
                static_cast<uint32_t>(paramValue(
                    *static_cast<Plugin*>(_plugin),
                    nodeParamId(_selectedNode, kNodePedalOffset))));
            return [NSString stringWithUTF8String:
                s3g::feedbackPedalMenuItem(pedal, _openMenuControl, item)];
        }
        return @"";
    }
}

- (int)selectedMenuItem
{
    auto* plugin = static_cast<Plugin*>(_plugin);
    switch (_openMenu) {
    case kFeedbackMenuPreset: return _factoryPresetIndex;
    case kFeedbackMenuNodeMode: return static_cast<int>(paramValue(*plugin,
        nodeParamId(_selectedNode, kNodeModeOffset)));
    case kFeedbackMenuInsertCategory: {
        const auto pedal = static_cast<s3g::FeedbackPedalType>(
            static_cast<uint32_t>(paramValue(*plugin,
                nodeParamId(_selectedNode, kNodePedalOffset))));
        return static_cast<int>(s3g::feedbackInsertCategory(pedal));
    }
    case kFeedbackMenuInsertEffect: {
        const auto pedal = static_cast<s3g::FeedbackPedalType>(
            static_cast<uint32_t>(paramValue(*plugin,
                nodeParamId(_selectedNode, kNodePedalOffset))));
        const uint32_t fractureProcessor = static_cast<uint32_t>(std::lround(
            feedbackNormalizedParam(*plugin,
                feedbackPedalStorageParamId(_selectedNode, 6u))
            * static_cast<CGFloat>(s3g::kFractureProcessorCount - 1u)));
        return static_cast<int>(s3g::feedbackInsertEffectIndex(
            pedal, fractureProcessor));
    }
    case kFeedbackMenuExciterSource:
        return static_cast<int>(paramValue(*plugin,
            nodeParamId(_selectedNode, kNodeExciterSourceOffset)));
    case kFeedbackMenuMorphSource:
        return static_cast<int>(paramValue(*plugin, kMorphSourceParamId));
    case kFeedbackMenuOutputMode:
        return static_cast<int>(paramValue(*plugin, kOutputModeParamId));
    case kFeedbackMenuRun:
        return static_cast<int>(paramValue(*plugin, kRunParamId));
    case kFeedbackMenuMorphHold:
        return static_cast<int>(paramValue(*plugin, kMorphHoldParamId));
    case kFeedbackMenuMorphSync:
        return static_cast<int>(paramValue(*plugin, kMorphSyncParamId));
    case kFeedbackMenuMorphDivision:
        return static_cast<int>(paramValue(*plugin, kMorphDivisionParamId));
    case kFeedbackMenuMorphShape:
        return static_cast<int>(paramValue(*plugin, kMorphShapeParamId));
    default:
        if (_openMenu >= kFeedbackMenuEffectBase) {
            const auto pedal = static_cast<s3g::FeedbackPedalType>(
                static_cast<uint32_t>(paramValue(*plugin,
                    nodeParamId(_selectedNode, kNodePedalOffset))));
            const auto info = _editingAux
                ? s3g::feedbackAuxControlInfo(_openMenuControl)
                : s3g::feedbackPedalControlInfo(
                    pedal, _openMenuControl);
            const clap_id id = _editingAux
                ? auxParamId(info.storageSlot)
                : feedbackPedalStorageParamId(
                    _selectedNode, info.storageSlot);
            const CGFloat normalized = feedbackNormalizedParam(*plugin, id);
            return info.menuCount > 1u ? static_cast<int>(std::lround(
                normalized * static_cast<CGFloat>(info.menuCount - 1u))) : 0;
        }
        return -1;
    }
}

- (NSRect)openMenuRect
{
    return NSMakeRect(_menuOrigin.x, _menuOrigin.y, _menuWidth,
        kFeedbackMenuItemHeight
            * static_cast<CGFloat>([self openMenuItemCount]));
}

- (void)openMenu:(int)menu control:(uint32_t)control rect:(NSRect)rect
{
    _openMenu = menu;
    _openMenuControl = control;
    _hoverMenuItem = -1;
    _menuWidth = rect.size.width;
    const CGFloat height = kFeedbackMenuItemHeight
        * static_cast<CGFloat>([self openMenuItemCount]);
    const CGFloat below = NSMaxY(rect) + 3.0;
    const CGFloat y = below + height <= kGuiHeight - 8.0
        ? below : NSMinY(rect) - height - 3.0;
    _menuOrigin = NSMakePoint(NSMinX(rect), std::max<CGFloat>(8.0, y));
    [self setNeedsDisplay:YES];
}

- (void)updateMenuHover:(NSPoint)point
{
    const uint32_t count = [self openMenuItemCount];
    if (count == 0u) return;
    const int hover = s3g::clap_gui::dropdownHitIndex(
        point, [self openMenuRect], kFeedbackMenuItemHeight, count);
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        [self setNeedsDisplay:YES];
    }
}

- (void)drawPanel:(NSString*)title
    panel:(const s3g::gui_layout::Panel&)panel
    style:(const s3g::clap_gui::Style&)style
    labels:(NSDictionary*)labels
{
    s3g::clap_gui::drawPanelFrame(panel.frame.x, panel.frame.y,
        panel.frame.width, panel.frame.height, style);
    s3g::clap_gui::drawPanelHeader(title, true, panel.frame.x, panel.frame.y,
        panel.frame.width, s3g::gui_layout::kStandardMetrics.headerHeight,
        labels, style);
}

- (void)drawSlider:(NSString*)label id:(clap_id)id
    row:(uint32_t)row panel:(const s3g::gui_layout::Panel&)panel
    style:(const s3g::clap_gui::Style&)style labels:(NSDictionary*)labels
    values:(NSDictionary*)values
{
    s3g::clap_gui::drawProcessorSlider(label,
        feedbackParamText(*static_cast<Plugin*>(_plugin), id),
        feedbackNormalizedParam(*static_cast<Plugin*>(_plugin), id),
        s3g::gui_layout::rowY(panel, row), panel.frame.x, panel.frame.width,
        labels, values, style);
}

- (void)drawMenu:(NSString*)label id:(clap_id)id
    row:(uint32_t)row panel:(const s3g::gui_layout::Panel&)panel
    style:(const s3g::clap_gui::Style&)style labels:(NSDictionary*)labels
    values:(NSDictionary*)values
{
    s3g::clap_gui::drawProcessorMenu(label,
        feedbackParamText(*static_cast<Plugin*>(_plugin), id),
        s3g::gui_layout::rowY(panel, row), panel.frame.x, panel.frame.width,
        labels, values, style);
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* plugin = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    float peak = 0.0f;
    for (const auto& meter : plugin->outputPeaks) {
        peak = std::max(peak, meter.load(std::memory_order_relaxed));
    }
    const auto outputMode = static_cast<s3g::FeedbackShiftOutputMode>(
        static_cast<uint32_t>(paramValue(*plugin, kOutputModeParamId)));
    const float liveMorph = plugin->morphValue.load(std::memory_order_relaxed);
    NSString* status = [NSString stringWithFormat:
        @"%s  //  A %.0f%%  B %.0f%%  //  %@",
        s3g::feedbackShiftOutputModeName(outputMode),
        (1.0f - liveMorph) * 100.0f, liveMorph * 100.0f,
        s3g::clap_gui::peakDbText(peak)];
    const auto titleBand = s3g::gui_layout::matrixTitleBand(kFeedbackCanvas);
    s3g::clap_gui::drawProcessorTitleBand(
        @"s3g PROCESSOR FEEDBACK SHIFT 8CH",
        [NSString stringWithUTF8String:_titlePresetName], status,
        titleBand, s3g::clap_gui::softTitleAttrs(), labels, values, style);
    [style.strip setFill];
    NSRectFill(kFeedbackPageBand);
    [style.grid setStroke];
    NSFrameRect(kFeedbackPageBand);
    static NSString* pageNames[kFeedbackPageCount] = {
        @"PATCH", @"INSERT", @"ECOLOGY", @"AUX",
    };
    for (uint32_t page = 0u; page < kFeedbackPageCount; ++page) {
        drawFeedbackActionButton(feedbackPageButtonRect(page),
            pageNames[page], _page == page, style);
    }
    [@"EDIT SCENE" drawAtPoint:NSMakePoint(632.0, 48.0)
        withAttributes:labels];
    drawFeedbackActionButton(feedbackSceneButtonRect(false), @"A",
        !_editingSceneB, style);
    drawFeedbackActionButton(feedbackSceneButtonRect(true), @"B",
        _editingSceneB, style);
    drawFeedbackActionButton(feedbackRandomButtonRect(false), @"RANDOM A",
        false, style);
    drawFeedbackActionButton(feedbackRandomButtonRect(true), @"RANDOM B",
        false, style);

    const auto pedal = static_cast<s3g::FeedbackPedalType>(
        static_cast<uint32_t>(paramValue(*plugin,
            nodeParamId(_selectedNode, kNodePedalOffset))));
    const auto fractureProcessor = static_cast<s3g::FractureProcessor>(
        std::min<uint32_t>(s3g::kFractureProcessorCount - 1u,
            static_cast<uint32_t>(std::lround(
                feedbackNormalizedParam(*plugin,
                    feedbackPedalStorageParamId(_selectedNode, 6u))
                * static_cast<CGFloat>(
                    s3g::kFractureProcessorCount - 1u)))));
    const auto shredCircuit = static_cast<s3g::MacroShredCircuit>(
        std::min<uint32_t>(s3g::kMacroShredCircuitCount - 1u,
            static_cast<uint32_t>(std::lround(
                feedbackNormalizedParam(*plugin,
                    feedbackPedalStorageParamId(_selectedNode, 11u))
                * static_cast<CGFloat>(
                    s3g::kMacroShredCircuitCount - 1u)))));
    const uint32_t effectRows = _editingAux
        ? s3g::kFeedbackAuxControlCount
        : feedbackVisiblePedalControlCount(pedal);
    const auto pedalPanel = feedbackEffectPanel(effectRows);
    const bool temporalPedal = !_editingAux
        && (pedal == s3g::FeedbackPedalType::Repeater
            || pedal == s3g::FeedbackPedalType::TimeMangler);
    const uint32_t temporalPhase = plugin->temporalPhases[_selectedNode].load(
        std::memory_order_relaxed);
    NSString* temporalState = @"LISTEN";
    if (temporalPhase == 1u) temporalState = @"CAPTURE";
    else if (temporalPhase == 2u) {
        if (pedal == s3g::FeedbackPedalType::Repeater) {
            temporalState = @"REPEAT";
        } else {
            const auto modeInfo = s3g::feedbackPedalControlInfo(pedal, 3u);
            const CGFloat modeNorm = feedbackNormalizedParam(*plugin,
                feedbackPedalStorageParamId(
                    _selectedNode, modeInfo.storageSlot));
            const uint32_t mode = static_cast<uint32_t>(std::lround(
                modeNorm * static_cast<CGFloat>(modeInfo.menuCount - 1u)));
            temporalState = [NSString stringWithUTF8String:
                s3g::feedbackPedalMenuItem(pedal, 3u, mode)];
        }
    }
    const clap_id selectedRouteId = matrixParamId(_editingSceneB,
        _selectedDestination, _selectedSource);
    if (_page == kFeedbackPagePatch) {
        [self drawPanel:[NSString stringWithFormat:
                @"SCENE %c MATRIX // SOURCE > DESTINATION",
                _editingSceneB ? 'B' : 'A']
            panel:kMatrixPanel style:style labels:labels];
        [self drawPanel:@"SCENE OPERATIONS" panel:kPatchScenePanel
            style:style labels:labels];
        [self drawPanel:[NSString stringWithFormat:
                @"CROSSPOINT // S%u > D%u", _selectedSource + 1u,
                _selectedDestination + 1u]
            panel:kCrosspointPanel style:style labels:labels];
    } else if (_page == kFeedbackPageInsert) {
        [self drawPanel:@"NODE INSERTS // SELECT + RANDOMIZE"
            panel:kNodeStripPanel style:style labels:labels];
        [self drawPanel:[NSString stringWithFormat:
                @"SCENE %c // NODE %u CORE", _editingSceneB ? 'B' : 'A',
                _selectedNode + 1u]
            panel:kNodePanel style:style labels:labels];
        [self drawPanel:@"INSERT SELECTION" panel:kInsertSelectorPanel
            style:style labels:labels];
        [self drawPanel:(pedal == s3g::FeedbackPedalType::Fracture
                ? [NSString stringWithFormat:@"INSERT // FRACTURE // %s",
                    s3g::fractureProcessorName(fractureProcessor)]
                : pedal == s3g::FeedbackPedalType::MacroShred
                ? [NSString stringWithFormat:@"INSERT // SHRED // %s",
                    s3g::macroShredCircuitName(shredCircuit)]
                : temporalPedal
                ? [NSString stringWithFormat:@"INSERT // %s // %@",
                    s3g::feedbackPedalName(pedal), temporalState]
                : [NSString stringWithFormat:@"INSERT // %s",
                    s3g::feedbackPedalName(pedal)]) panel:pedalPanel
            style:style labels:labels];
    } else if (_page == kFeedbackPageAux) {
        [self drawPanel:[NSString stringWithFormat:
                @"SCENE %c AUX SENDS // NODE > BUS",
                _editingSceneB ? 'B' : 'A']
            panel:kAuxSendPanel style:style labels:labels];
        [self drawPanel:[NSString stringWithFormat:
                @"FEEDBACK AUX // %.1f dB GR",
                plugin->auxGainReductionDb.load(std::memory_order_relaxed)]
            panel:kAuxFeedbackPanel style:style labels:labels];
        [self drawPanel:[NSString stringWithFormat:
                @"POST GRANULATOR // %.1f GRAINS",
                plugin->auxGrainActivity.load(
                    std::memory_order_relaxed) * 10.0f]
            panel:kPostGrainPanel style:style labels:labels];
    }
    const CGFloat restActivity = plugin->restActivity.load(
        std::memory_order_relaxed);
    const uint32_t restingLanes = plugin->restingLaneCount.load(
        std::memory_order_relaxed);
    const CGFloat spliceEventsPerSecond =
        s3g::feedbackSpliceRateHz(static_cast<float>(
            paramValue(*plugin, kSpliceRateParamId)), static_cast<float>(
            paramValue(*plugin, kSpliceRateFineParamId)))
        * s3g::kFeedbackShiftChannels;
    NSString* spliceTitle = spliceEventsPerSecond < 1.0
        ? [NSString stringWithFormat:@"SPLICE // %.1f/MIN",
            spliceEventsPerSecond * 60.0]
        : spliceEventsPerSecond < 10.0
        ? [NSString stringWithFormat:@"SPLICE // %.1f/S",
            spliceEventsPerSecond]
        : [NSString stringWithFormat:@"SPLICE // %.0f/S",
            spliceEventsPerSecond];
    NSString* engineTitle = _engineBank == 2u
        ? [NSString stringWithFormat:@"SUB // %.1f HZ",
            s3g::feedbackSubBassFrequencyHz(static_cast<float>(
                paramValue(*plugin, kSubBassTuneParamId)))]
        : _engineBank == 1u
        ? spliceTitle
        : restingLanes > 0u
        ? [NSString stringWithFormat:@"ENGINE // %u LANE%s REST",
            restingLanes, restingLanes == 1u ? "" : "S"]
        : restActivity > 0.01f
            ? [NSString stringWithFormat:@"ENGINE // REST ENTER %.0f%%",
                restActivity * 100.0f]
            : @"ENGINE";
    if (_page == kFeedbackPageEcology) {
        [self drawPanel:engineTitle panel:kExcitationPanel
            style:style labels:labels];
        drawFeedbackActionButton(feedbackEngineBankButtonRect(0u), @"SRC",
            _engineBank == 0u, style);
        drawFeedbackActionButton(feedbackEngineBankButtonRect(1u), @"SPL",
            _engineBank == 1u, style);
        drawFeedbackActionButton(feedbackEngineBankButtonRect(2u), @"SUB",
            _engineBank == 2u, style);
        [self drawPanel:@"ECOLOGY MORPH // A > B" panel:kMorphPanel
            style:style labels:labels];
        [self drawPanel:@"OUTPUT TOPOLOGY" panel:kOutputPanel
            style:style labels:labels];
        [self drawPanel:@"NODE ECOLOGY // SELECTED LANE ACTIVITY"
            panel:kEcologyActivityPanel style:style labels:labels];
    }
    if (_page == kFeedbackPageInsert
        && temporalPedal && temporalPhase != 0u) {
        NSRect temporalBar = NSMakeRect(pedalPanel.frame.x + 1.0,
            pedalPanel.frame.y + s3g::gui_layout::kStandardMetrics.headerHeight
                - 3.0,
            (pedalPanel.frame.width - 2.0)
                * plugin->temporalProgress[_selectedNode].load(
                    std::memory_order_relaxed), 2.0);
        [(temporalPhase == 1u ? s3g::clap_gui::color(0xb28a72)
                             : s3g::clap_gui::color(0x86a48c)) setFill];
        NSRectFill(temporalBar);
    }

    if (_page == kFeedbackPagePatch) {
    [@"SOURCE" drawAtPoint:NSMakePoint(304.0, 108.0)
        withAttributes:labels];
    [@"DEST" drawAtPoint:NSMakePoint(68.0, 322.0)
        withAttributes:labels];
    for (uint32_t index = 0u; index < s3g::kFeedbackShiftChannels; ++index) {
        NSString* number = [NSString stringWithFormat:@"%u", index + 1u];
        [number drawAtPoint:NSMakePoint(kFeedbackMatrixX
            + static_cast<CGFloat>(index) * kFeedbackMatrixCell + 13.0,
            122.0) withAttributes:values];
        [number drawAtPoint:NSMakePoint(116.0, kFeedbackMatrixY
            + static_cast<CGFloat>(index) * kFeedbackMatrixCell + 8.0)
            withAttributes:values];
    }
    [style.grid setStroke];
    const CGFloat matrixFirstCenter = kFeedbackMatrixX + 17.0;
    const CGFloat matrixLastCenter = matrixFirstCenter
        + kFeedbackMatrixCell * 7.0;
    for (uint32_t index = 0u; index < s3g::kFeedbackShiftChannels; ++index) {
        const CGFloat x = matrixFirstCenter
            + static_cast<CGFloat>(index) * kFeedbackMatrixCell;
        const CGFloat y = kFeedbackMatrixY + 17.0
            + static_cast<CGFloat>(index) * kFeedbackMatrixCell;
        [NSBezierPath strokeLineFromPoint:NSMakePoint(x, kFeedbackMatrixY + 17.0)
            toPoint:NSMakePoint(x, matrixLastCenter)];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(matrixFirstCenter, y)
            toPoint:NSMakePoint(matrixLastCenter, y)];
    }
    for (uint32_t destination = 0u;
         destination < s3g::kFeedbackShiftChannels; ++destination) {
        for (uint32_t source = 0u;
             source < s3g::kFeedbackShiftChannels; ++source) {
            const uint32_t index = destination
                * s3g::kFeedbackShiftChannels + source;
            const double route = paramValue(*plugin,
                matrixParamId(_editingSceneB, destination, source));
            const float activity = plugin->routeActivity[index].load(
                std::memory_order_relaxed);
            const NSRect cell = feedbackMatrixCellRect(destination, source);
            const bool active = std::abs(route) > 0.001;
            const bool selected = source == _selectedSource
                && destination == _selectedDestination;
            const uint32_t routeBase = route >= 0.0 ? 0xc95e3b : 0x5daeb6;
            const uint32_t routeLive = route >= 0.0 ? 0xff7047 : 0x69d2dc;
            const NSRect node = NSMakeRect(NSMidX(cell) - 8.0,
                NSMidY(cell) - 8.0, 16.0, 16.0);
            [s3g::clap_gui::color(0x141414) setFill];
            NSRectFill(node);
            if (active) {
                [[s3g::clap_gui::color(routeBase)
                    colorWithAlphaComponent:0.58] setFill];
                NSRectFill(NSInsetRect(node, 1.0, 1.0));
                const CGFloat effective = std::clamp<CGFloat>(
                    std::abs(route) * (0.72 + activity * 0.58), 0.0, 1.0);
                const CGFloat liveSize = 3.0 + effective * 11.0;
                [[s3g::clap_gui::color(routeLive)
                    colorWithAlphaComponent:0.56 + activity * 0.40] setFill];
                NSRectFill(NSMakeRect(NSMidX(node) - liveSize * 0.5,
                    NSMidY(node) - liveSize * 0.5, liveSize, liveSize));
            }
            [(selected ? s3g::clap_gui::color(0xc8c8c8)
                       : s3g::clap_gui::color(0x5a5a5a)) setStroke];
            NSFrameRect(node);
        }
    }
    [@"CLICK: PATCH / SELECT · CLICK AGAIN: DISSOLVE · OPTION: NEGATIVE"
        drawAtPoint:NSMakePoint(kMatrixPanel.frame.x + 18.0,
            kMatrixPanel.frame.y + kMatrixPanel.frame.height - 26.0)
        withAttributes:values];
    const double selectedRoute = paramValue(*plugin, selectedRouteId);
    s3g::clap_gui::drawProcessorSlider(@"GAIN",
        [NSString stringWithFormat:@"%+.3f", selectedRoute],
        (selectedRoute + 1.0) * 0.5,
        s3g::gui_layout::rowY(kCrosspointPanel, 0u),
        kCrosspointPanel.frame.x, kCrosspointPanel.frame.width,
        labels, values, style);
    drawFeedbackActionButton(feedbackCrosspointActionRect(0u),
        selectedRoute < 0.0 ? @"NEGATIVE" : @"POSITIVE",
        std::abs(selectedRoute) > 0.001, style);
    drawFeedbackActionButton(feedbackCrosspointActionRect(1u),
        @"CLEAR", false, style);
    const CGFloat selectedActivity = plugin->routeActivity[
        _selectedDestination * s3g::kFeedbackShiftChannels
            + _selectedSource].load(std::memory_order_relaxed);
    [@"POLARITY" drawAtPoint:NSMakePoint(686.0, 420.0)
        withAttributes:labels];
    [s3g::clap_gui::color(0xc95e3b) setFill];
    NSRectFill(NSMakeRect(772.0, 421.0, 12.0, 12.0));
    [@"POSITIVE" drawAtPoint:NSMakePoint(792.0, 419.0)
        withAttributes:values];
    [s3g::clap_gui::color(0x5daeb6) setFill];
    NSRectFill(NSMakeRect(886.0, 421.0, 12.0, 12.0));
    [@"NEGATIVE" drawAtPoint:NSMakePoint(906.0, 419.0)
        withAttributes:values];
    [@"LIVE ROUTE" drawAtPoint:NSMakePoint(686.0, 462.0)
        withAttributes:labels];
    const NSRect routeMeter = NSMakeRect(772.0, 464.0, 276.0, 10.0);
    [s3g::clap_gui::color(0x080808) setFill]; NSRectFill(routeMeter);
    [style.grid setStroke]; NSFrameRect(routeMeter);
    [(selectedRoute >= 0.0 ? s3g::clap_gui::color(0xff7047)
                           : s3g::clap_gui::color(0x69d2dc)) setFill];
    NSRect routeFill = NSInsetRect(routeMeter, 1.0, 1.0);
    routeFill.size.width *= std::clamp<CGFloat>(
        std::abs(selectedRoute) * (0.6 + selectedActivity * 0.6), 0.0, 1.0);
    NSRectFill(routeFill);
    [@"RANDOM A/B mutates one scene only. COPY commits the authored scene."
        drawAtPoint:NSMakePoint(686.0, 108.0) withAttributes:values];
    drawFeedbackActionButton(feedbackSceneCopyButtonRect(true),
        @"COPY A > B", false, style);
    drawFeedbackActionButton(feedbackSceneCopyButtonRect(false),
        @"COPY B > A", false, style);
    [@"Orange = positive gain · Cyan = inverted gain"
        drawAtPoint:NSMakePoint(686.0, 214.0) withAttributes:values];
    }

    if (_page == kFeedbackPageInsert
        || _page == kFeedbackPageEcology) {
    for (uint32_t node = 0u; node < s3g::kFeedbackShiftChannels; ++node) {
        const bool selected = node == _selectedNode;
        drawFeedbackActionButton(feedbackNodeButtonRect(_page, node),
            [NSString stringWithFormat:@"N%u", node + 1u], selected, style);
        if (_page == kFeedbackPageInsert) {
            drawFeedbackActionButton(feedbackNodeRandomButtonRect(node),
                @"RND", false, style);
        }
        const CGFloat laneRest = plugin->restLaneActivities[node].load(
            std::memory_order_relaxed);
        if (laneRest > 0.005f) {
            NSRect restMeter = feedbackNodeButtonRect(_page, node);
            restMeter.origin.y += 1.0;
            restMeter.size.height = 3.0;
            restMeter.size.width = std::max<CGFloat>(1.0,
                restMeter.size.width * laneRest);
            [s3g::clap_gui::color(0xb28a72) setFill];
            NSRectFill(restMeter);
        }
        const CGFloat laneEnvelope = plugin->laneEcologyEnvelopes[node].load(
            std::memory_order_relaxed);
        const CGFloat laneEdge = plugin->laneEcologyEdges[node].load(
            std::memory_order_relaxed);
        const CGFloat activity = std::max<CGFloat>(
            static_cast<CGFloat>(plugin->nodeActivity[node].load(std::memory_order_relaxed)),
            laneEnvelope);
        if (activity > 0.001) {
            NSRect meter = feedbackNodeButtonRect(_page, node);
            meter.origin.y = NSMaxY(meter) - 2.0;
            meter.size.height = 2.0;
            meter.size.width *= std::min<CGFloat>(1.0, activity);
            [s3g::clap_gui::color(0x86a48c) setFill];
            NSRectFill(meter);
        }
        if (laneEdge > 0.015f) {
            const NSRect nodeRect = feedbackNodeButtonRect(_page, node);
            const CGFloat diameter = 2.0f + laneEdge * 4.0f;
            [s3g::clap_gui::color(0xc9b86f) setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
                NSMaxX(nodeRect) - diameter - 3.0,
                NSMidY(nodeRect) - diameter * 0.5,
                diameter, diameter)] fill];
        }
        const CGFloat splicePulse = plugin->spliceLaneActivities[node].load(
            std::memory_order_relaxed);
        if (splicePulse > 0.01f) {
            const NSRect nodeRect = feedbackNodeButtonRect(_page, node);
            [s3g::clap_gui::color(0x71a6a0) setFill];
            NSRectFill(NSMakeRect(NSMaxX(nodeRect) - 2.0,
                nodeRect.origin.y + 2.0, 2.0,
                (nodeRect.size.height - 4.0) * splicePulse));
        }
    }
    }
    const CGFloat auxActivity = plugin->auxActivity.load(
        std::memory_order_relaxed);
    if (_page == kFeedbackPageAux && auxActivity > 0.001f) {
        NSRect meter = NSMakeRect(kAuxFeedbackPanel.frame.x + 1.0,
            kAuxFeedbackPanel.frame.y
                + s3g::gui_layout::kStandardMetrics.headerHeight - 3.0,
            (kAuxFeedbackPanel.frame.width - 2.0)
                * std::min<CGFloat>(1.0, auxActivity * 2.0), 2.0);
        [s3g::clap_gui::color(0xff7047) setFill];
        NSRectFill(meter);
    }
    if (_page == kFeedbackPageAux) {
        [style.grid setFill];
        NSRectFill(NSMakeRect(kAuxSendRightPanel.frame.x,
            kAuxSendPanel.frame.y
                + s3g::gui_layout::kStandardMetrics.headerHeight,
            1.0, kAuxSendPanel.frame.height
                - s3g::gui_layout::kStandardMetrics.headerHeight));
        for (uint32_t node = 0u;
             node < s3g::kFeedbackShiftChannels; ++node) {
            const auto& panel = node < 4u
                ? kAuxSendLeftPanel : kAuxSendRightPanel;
            [self drawSlider:[NSString stringWithFormat:@"NODE %u", node + 1u]
                id:sceneParamId(_editingSceneB, node, kSceneAuxSendOffset)
                row:node % 4u panel:panel
                style:style labels:labels values:values];
        }
    } else if (_page == kFeedbackPageInsert) {
        [self drawMenu:@"MODE" id:nodeParamId(_selectedNode, kNodeModeOffset)
            row:0u panel:kNodePanel style:style labels:labels values:values];
        const auto insertCategory = s3g::feedbackInsertCategory(pedal);
        const uint32_t insertEffect = s3g::feedbackInsertEffectIndex(pedal,
            static_cast<uint32_t>(fractureProcessor),
            static_cast<uint32_t>(shredCircuit));
        s3g::clap_gui::drawProcessorMenu(@"CATEGORY",
            [NSString stringWithUTF8String:
                s3g::feedbackInsertCategoryName(insertCategory)],
            s3g::gui_layout::rowY(kInsertSelectorPanel, 0u),
            kInsertSelectorPanel.frame.x, kInsertSelectorPanel.frame.width,
            labels, values, style);
        s3g::clap_gui::drawProcessorMenu(@"EFFECT",
            [NSString stringWithUTF8String:
                s3g::feedbackInsertEffectName(insertCategory, insertEffect)],
            s3g::gui_layout::rowY(kInsertSelectorPanel, 1u),
            kInsertSelectorPanel.frame.x, kInsertSelectorPanel.frame.width,
            labels, values, style);
        [self drawMenu:@"SOURCE" id:nodeParamId(_selectedNode,
            kNodeExciterSourceOffset) row:1u panel:kNodePanel
            style:style labels:labels values:values];
        [self drawSlider:@"INPUT" id:nodeParamId(_selectedNode,
            kNodeExciterGainOffset) row:2u panel:kNodePanel
            style:style labels:labels values:values];
        [self drawSlider:@"SHIFT" id:sceneParamId(_editingSceneB,
            _selectedNode, kSceneFrequencyOffset)
            row:3u panel:kNodePanel style:style
            labels:labels values:values];
        [self drawSlider:@"REGEN" id:sceneParamId(_editingSceneB,
            _selectedNode, kSceneRegenerationOffset)
            row:4u panel:kNodePanel style:style
            labels:labels values:values];
        [self drawSlider:@"COLOR" id:sceneParamId(_editingSceneB,
            _selectedNode, kSceneColorOffset)
            row:5u panel:kNodePanel style:style
            labels:labels values:values];
        [self drawSlider:@"BODY" id:sceneParamId(_editingSceneB,
            _selectedNode, kSceneBodyOffset)
            row:6u panel:kNodePanel style:style
            labels:labels values:values];
        [self drawSlider:@"LEVEL" id:sceneParamId(_editingSceneB,
            _selectedNode, kSceneLevelOffset)
            row:7u panel:kNodePanel
            style:style labels:labels values:values];
        [self drawSlider:@"AUX SEND" id:sceneParamId(_editingSceneB,
            _selectedNode, kSceneAuxSendOffset)
            row:8u panel:kNodePanel
            style:style labels:labels values:values];
    }

    if (_page == kFeedbackPageInsert || _page == kFeedbackPageAux) {
    if (effectRows == 0u) {
        [@"NO INSERT // SHIFT/RING REMAINS ACTIVE"
            drawAtPoint:NSMakePoint(pedalPanel.frame.x + 16.0,
                pedalPanel.frame.y + 37.0) withAttributes:values];
    }
    for (uint32_t row = 0u; row < effectRows; ++row) {
        const uint32_t control = _editingAux ? row
            : feedbackVisiblePedalControlIndex(pedal, row);
        auto info = _editingAux
            ? s3g::feedbackAuxControlInfo(row)
            : s3g::feedbackPedalControlInfo(pedal, control);
        if (!_editingAux && pedal == s3g::FeedbackPedalType::Fracture) {
            if (control == 1u) info.label = s3g::fractureAmountLabel(
                fractureProcessor);
            else if (control == 2u) info.label = s3g::fractureColorLabel(
                fractureProcessor);
            else if (control == 3u) info.label = s3g::fractureBiasLabel(
                fractureProcessor);
        }
        const clap_id id = _editingAux ? auxParamId(info.storageSlot)
            : feedbackPedalStorageParamId(_selectedNode, info.storageSlot);
        const CGFloat normalized = feedbackNormalizedParam(*plugin, id);
        NSString* value = _editingAux
            ? feedbackAuxControlText(info, normalized, row)
            : feedbackPedalControlText(info, normalized, pedal, control);
        const auto& controlPanel = _editingAux
            ? (row < 6u ? kAuxFeedbackPanel : kPostGrainPanel)
            : pedalPanel;
        const uint32_t controlRow = _editingAux && row >= 6u
            ? row - 6u : row;
        if (info.display == s3g::FeedbackPedalControlDisplay::Menu) {
            s3g::clap_gui::drawProcessorMenu(
                [NSString stringWithUTF8String:info.label], value,
                s3g::gui_layout::rowY(controlPanel, controlRow),
                controlPanel.frame.x, controlPanel.frame.width,
                labels, values, style);
        } else {
            s3g::clap_gui::drawProcessorSlider(
                [NSString stringWithUTF8String:info.label], value, normalized,
                s3g::gui_layout::rowY(controlPanel, controlRow),
                controlPanel.frame.x, controlPanel.frame.width,
                labels, values, style);
        }
    }
    }

    if (_page == kFeedbackPageEcology) {
    if (_engineBank == 0u) {
        [self drawSlider:@"INT NOISE" id:kExciteParamId row:0u
            panel:kExcitationPanel style:style labels:labels values:values];
        [self drawSlider:@"DRIFT" id:kDriftParamId row:1u
            panel:kExcitationPanel style:style labels:labels values:values];
        [self drawMenu:@"ENGINE RUN" id:kRunParamId row:2u
            panel:kExcitationPanel style:style labels:labels values:values];
        [self drawSlider:@"REST" id:kGovernorRestParamId row:3u
            panel:kExcitationPanel style:style labels:labels values:values];
        drawFeedbackActionButton(feedbackActionButtonRect(0u),
            [NSString stringWithFormat:@"HIT N%u", _selectedNode + 1u],
            false, style);
        drawFeedbackActionButton(feedbackActionButtonRect(1u), @"PANIC",
            false, style);
    } else if (_engineBank == 1u) {
        [self drawSlider:@"SPLICE" id:kSpliceAmountParamId row:0u
            panel:kExcitationPanel style:style labels:labels values:values];
        [self drawSlider:@"RANGE" id:kSpliceRateParamId row:1u
            panel:kExcitationPanel style:style labels:labels values:values];
        [self drawSlider:@"FINE" id:kSpliceRateFineParamId row:2u
            panel:kExcitationPanel style:style labels:labels values:values];
        [self drawSlider:@"CONTRAST" id:kSpliceContrastParamId row:3u
            panel:kExcitationPanel style:style labels:labels values:values];
        [self drawSlider:@"SPACE" id:kSpliceSpaceParamId row:4u
            panel:kExcitationPanel style:style labels:labels values:values];
    } else {
        [self drawSlider:@"TUNE" id:kSubBassTuneParamId row:0u
            panel:kExcitationPanel style:style labels:labels values:values];
        [self drawSlider:@"SHAPE" id:kSubBassShapeParamId row:1u
            panel:kExcitationPanel style:style labels:labels values:values];
        [self drawSlider:@"DRIVE" id:kSubBassDriveParamId row:2u
            panel:kExcitationPanel style:style labels:labels values:values];
        [self drawSlider:@"DECAY" id:kSubBassDecayParamId row:3u
            panel:kExcitationPanel style:style labels:labels values:values];
        [self drawSlider:@"SUSTAIN" id:kSubBassSustainParamId row:4u
            panel:kExcitationPanel style:style labels:labels values:values];
    }

    [style.grid setFill];
    NSRectFill(NSMakeRect(kMorphRightPanel.frame.x,
        kMorphPanel.frame.y + s3g::gui_layout::kStandardMetrics.headerHeight,
        1.0, kMorphPanel.frame.height
            - s3g::gui_layout::kStandardMetrics.headerHeight));
    [self drawMenu:@"DRIVER" id:kMorphSourceParamId row:0u
        panel:kMorphLeftPanel style:style labels:labels values:values];
    const CGFloat morphRowY = s3g::gui_layout::rowY(kMorphLeftPanel, 1u);
    s3g::clap_gui::drawProcessorSlider(@"MORPH",
        [NSString stringWithFormat:@"B %.0f%%", liveMorph * 100.0f],
        liveMorph, morphRowY, kMorphLeftPanel.frame.x,
        kMorphLeftPanel.frame.width, labels, values, style);
    // A full-track scene gradient shows the actual live fade. The fine dark
    // tick preserves the editable manual/base position while the larger gold
    // readhead follows automatic morphology.
    const CGFloat morphTrackX = s3g::gui_layout::processorControlX(
        kMorphLeftPanel.frame.x);
    const CGFloat morphTrackWidth = s3g::gui_layout::processorTrackWidth(
        kMorphLeftPanel.frame.width);
    const NSRect morphTrack = NSMakeRect(
        morphTrackX, morphRowY + 1.0, morphTrackWidth, 9.0);
    const NSRect morphInner = NSInsetRect(morphTrack, 1.0, 1.0);
    NSGradient* sceneGradient = [[NSGradient alloc]
        initWithStartingColor:[s3g::clap_gui::color(0x86a48c)
            colorWithAlphaComponent:0.72]
        endingColor:[s3g::clap_gui::color(0xb28a72)
            colorWithAlphaComponent:0.72]];
    [sceneGradient drawInRect:morphInner angle:0.0];
    [sceneGradient release];
    [style.grid setStroke];
    NSFrameRect(morphTrack);
    const CGFloat morphBase = std::clamp<CGFloat>(
        paramValue(*plugin, kMorphParamId), 0.0, 1.0);
    [style.bg setFill];
    NSRectFill(NSMakeRect(morphTrackX + morphTrackWidth * morphBase,
        morphTrack.origin.y, 1.0, morphTrack.size.height));
    [s3g::clap_gui::color(0xc9b86f) setFill];
    NSRectFill(NSMakeRect(std::clamp(
            morphTrackX + morphTrackWidth * liveMorph - 1.5,
            morphTrackX + 1.0, morphTrackX + morphTrackWidth - 4.0),
        morphTrack.origin.y - 2.0, 3.0, morphTrack.size.height + 4.0));
    [self drawSlider:@"DEPTH" id:kMorphDepthParamId row:2u
        panel:kMorphLeftPanel style:style labels:labels values:values];
    [self drawSlider:@"INERTIA" id:kMorphInertiaParamId row:3u
        panel:kMorphLeftPanel style:style labels:labels values:values];
    const auto morphSource = static_cast<s3g::FeedbackMorphSource>(
        static_cast<uint32_t>(paramValue(*plugin, kMorphSourceParamId)));
    const bool pulseMorph = morphSource == s3g::FeedbackMorphSource::Pulse;
    const bool edgeMorph = morphSource == s3g::FeedbackMorphSource::Edge;
    const CGFloat ecologyEnvelope = std::clamp<CGFloat>(
        plugin->inputEnvelope.load(std::memory_order_relaxed), 0.0, 1.0);
    const CGFloat ecologyEdge = std::clamp<CGFloat>(
        plugin->ecologyEdge.load(std::memory_order_relaxed), 0.0, 1.0);
    const CGFloat ecologyMeter = edgeMorph ? ecologyEdge : ecologyEnvelope;
    s3g::clap_gui::drawProcessorSlider(
        edgeMorph ? @"ECOLOGY EDGE" : @"ECOLOGY ENV",
        [NSString stringWithFormat:@"%3.0f%%", ecologyMeter * 100.0f],
        ecologyMeter, s3g::gui_layout::rowY(kMorphLeftPanel, 4u),
        kMorphLeftPanel.frame.x, kMorphLeftPanel.frame.width,
        labels, values, style);
    if (pulseMorph) {
        [self drawSlider:@"RATE" id:kMorphRateParamId row:0u
            panel:kMorphRightPanel style:style labels:labels values:values];
        [self drawMenu:@"HOLD" id:kMorphHoldParamId row:1u
            panel:kMorphRightPanel style:style labels:labels values:values];
        [self drawMenu:@"SYNC" id:kMorphSyncParamId row:2u
            panel:kMorphRightPanel style:style labels:labels values:values];
        [self drawMenu:@"DIV" id:kMorphDivisionParamId row:3u
            panel:kMorphRightPanel style:style labels:labels values:values];
        [self drawMenu:@"SHAPE" id:kMorphShapeParamId row:4u
            panel:kMorphRightPanel style:style labels:labels values:values];
    } else {
        [self drawSlider:@"REFLEX" id:kGovernorReflexParamId row:0u
            panel:kMorphRightPanel style:style labels:labels values:values];
        [self drawSlider:@"SENSE" id:kGovernorSensitivityParamId row:1u
            panel:kMorphRightPanel style:style labels:labels values:values];
        [self drawSlider:@"RECOVER" id:kGovernorRecoveryParamId row:2u
            panel:kMorphRightPanel style:style labels:labels values:values];
        [self drawSlider:@"RATE" id:kMorphRateParamId row:3u
            panel:kMorphRightPanel style:style labels:labels values:values];
        [self drawMenu:@"HOLD" id:kMorphHoldParamId row:4u
            panel:kMorphRightPanel style:style labels:labels values:values];
    }
    const CGFloat phase = plugin->morphPhase.load(std::memory_order_relaxed);
    const CGFloat driver = plugin->morphDriver.load(std::memory_order_relaxed);
    [s3g::clap_gui::color(0xc9b86f) setFill];
    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
        kMorphPanel.frame.x + 164.0 + phase * 78.0,
        kMorphPanel.frame.y + 7.0, 5.0, 5.0)] fill];
    [s3g::clap_gui::color(0x86a48c) setFill];
    NSRectFill(NSMakeRect(kMorphPanel.frame.x + 258.0,
        kMorphPanel.frame.y + 20.0, driver * 258.0, 2.0));

    [self drawSlider:@"OUT" id:kOutputParamId row:0u panel:kOutputPanel
        style:style labels:labels values:values];
    [self drawMenu:@"MODE" id:kOutputModeParamId row:1u panel:kOutputPanel
        style:style labels:labels values:values];
    [self drawSlider:@"ROTATE" id:kOutputRotationParamId row:2u
        panel:kOutputPanel style:style labels:labels values:values];
    const CGFloat containment = 1.0f - plugin->minimumGovernor.load(
        std::memory_order_relaxed);
    s3g::clap_gui::drawProcessorSlider(@"GOV",
        [NSString stringWithFormat:@"%3.0f%%", containment * 100.0],
        containment, s3g::gui_layout::rowY(kOutputPanel, 3u),
        kOutputPanel.frame.x, kOutputPanel.frame.width,
        labels, values, style);
    for (uint32_t node = 0u; node < s3g::kFeedbackShiftChannels; ++node) {
        const uint32_t column = node / 4u;
        const uint32_t row = node % 4u;
        const CGFloat x = 386.0 + static_cast<CGFloat>(column) * 340.0;
        const CGFloat y = 570.0 + static_cast<CGFloat>(row) * 38.0;
        [[NSString stringWithFormat:@"N%u", node + 1u]
            drawAtPoint:NSMakePoint(x, y - 3.0) withAttributes:values];
        const NSRect activityTrack = NSMakeRect(x + 28.0, y, 290.0, 8.0);
        [s3g::clap_gui::color(0x080808) setFill]; NSRectFill(activityTrack);
        [style.grid setStroke]; NSFrameRect(activityTrack);
        NSRect activityFill = NSInsetRect(activityTrack, 1.0, 1.0);
        activityFill.size.width *= std::clamp<CGFloat>(
            plugin->laneEcologyEnvelopes[node].load(
                std::memory_order_relaxed), 0.0, 1.0);
        [s3g::clap_gui::color(0x69d2dc) setFill]; NSRectFill(activityFill);
        NSRect restFill = NSMakeRect(x + 28.0, y + 13.0, 290.0, 3.0);
        restFill.size.width *= std::clamp<CGFloat>(
            plugin->restLaneActivities[node].load(
                std::memory_order_relaxed), 0.0, 1.0);
        [s3g::clap_gui::color(0xff7047) setFill]; NSRectFill(restFill);
    }
    }

    if ([self openMenuItemCount] > 0u) {
        NSString* items[32u] {};
        const uint32_t count = [self openMenuItemCount];
        for (uint32_t item = 0u; item < count; ++item) {
            items[item] = [self menuItemText:item];
        }
        s3g::clap_gui::drawDropdownMenu([self openMenuRect],
            kFeedbackMenuItemHeight, items, count, [self selectedMenuItem],
            _hoverMenuItem, values, style);
    }
}

- (void)updateSlider:(NSPoint)point
{
    if (_dragParam < 0 || _dragPanelWidth <= 0.0) return;
    auto* plugin = static_cast<Plugin*>(_plugin);
    const clap_id id = static_cast<clap_id>(_dragParam);
    const double trackX = s3g::gui_layout::processorControlX(_dragPanelX);
    const double trackWidth = s3g::gui_layout::processorTrackWidth(
        _dragPanelWidth);
    const double normalized = std::clamp(
        (point.x - trackX) / std::max(1.0, trackWidth), 0.0, 1.0);
    uint32_t node = 0u;
    clap_id offset = 0u;
    bool sceneB = false;
    double value = 0.0;
    if (decodeSceneParam(id, sceneB, node, offset)
        && offset == kSceneFrequencyOffset) {
        value = s3g::feedbackShiftFrequencyFromControlNorm(
            static_cast<float>(normalized));
    } else {
        ParamRange range;
        if (!paramRange(id, range)) return;
        value = range.minimum + normalized * (range.maximum - range.minimum);
        if (range.stepped) value = std::round(value);
    }
    queueGuiParamValue(*plugin, id, value);
    _factoryPresetIndex = -1;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CUSTOM");
    [self setNeedsDisplay:YES];
}

- (void)selectMenuItem:(uint32_t)item
{
    auto* plugin = static_cast<Plugin*>(_plugin);
    clap_id id = CLAP_INVALID_ID;
    double value = static_cast<double>(item);
    switch (_openMenu) {
    case kFeedbackMenuPreset:
        _factoryPresetIndex = static_cast<int>(std::min(item,
            s3g::kFeedbackShiftPresetCount - 1u));
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            s3g::kFeedbackShiftPresetInfo[
                static_cast<uint32_t>(_factoryPresetIndex)].name);
        queueGuiPatch(*plugin, s3g::feedbackShiftPreset(
            static_cast<uint32_t>(_factoryPresetIndex)));
        return;
    case kFeedbackMenuNodeMode:
        id = nodeParamId(_selectedNode, kNodeModeOffset); break;
    case kFeedbackMenuInsertCategory: {
        const auto category = static_cast<s3g::FeedbackInsertCategory>(
            std::min(item, s3g::kFeedbackInsertCategoryCount - 1u));
        const auto pedal = s3g::feedbackInsertEffectPedal(category, 0u);
        queueGuiParamGesture(*plugin,
            nodeParamId(_selectedNode, kNodePedalOffset),
            static_cast<double>(pedal));
        if (category == s3g::FeedbackInsertCategory::Fracture) {
            queueGuiParamGesture(*plugin,
                feedbackPedalStorageParamId(_selectedNode, 6u), 0.0);
        } else if (category == s3g::FeedbackInsertCategory::Shred) {
            queueGuiParamGesture(*plugin,
                feedbackPedalStorageParamId(_selectedNode, 11u), 0.0);
        }
        _factoryPresetIndex = -1;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "CUSTOM");
        return;
    }
    case kFeedbackMenuInsertEffect: {
        const auto currentPedal = static_cast<s3g::FeedbackPedalType>(
            static_cast<uint32_t>(paramValue(*plugin,
                nodeParamId(_selectedNode, kNodePedalOffset))));
        const auto category = s3g::feedbackInsertCategory(currentPedal);
        const uint32_t count = s3g::feedbackInsertEffectCount(category);
        const uint32_t effect = std::min(item, count - 1u);
        queueGuiParamGesture(*plugin,
            nodeParamId(_selectedNode, kNodePedalOffset),
            static_cast<double>(s3g::feedbackInsertEffectPedal(
                category, effect)));
        if (category == s3g::FeedbackInsertCategory::Fracture) {
            const double normalized = count > 1u
                ? static_cast<double>(effect)
                    / static_cast<double>(count - 1u) : 0.0;
            queueGuiParamGesture(*plugin,
                feedbackPedalStorageParamId(_selectedNode, 6u), normalized);
        } else if (category == s3g::FeedbackInsertCategory::Shred) {
            const double normalized = count > 1u
                ? static_cast<double>(effect)
                    / static_cast<double>(count - 1u) : 0.0;
            queueGuiParamGesture(*plugin,
                feedbackPedalStorageParamId(_selectedNode, 11u), normalized);
        }
        _factoryPresetIndex = -1;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "CUSTOM");
        return;
    }
    case kFeedbackMenuExciterSource:
        id = nodeParamId(_selectedNode, kNodeExciterSourceOffset); break;
    case kFeedbackMenuMorphSource: id = kMorphSourceParamId; break;
    case kFeedbackMenuOutputMode: id = kOutputModeParamId; break;
    case kFeedbackMenuRun: id = kRunParamId; break;
    case kFeedbackMenuMorphHold: id = kMorphHoldParamId; break;
    case kFeedbackMenuMorphSync: id = kMorphSyncParamId; break;
    case kFeedbackMenuMorphDivision: id = kMorphDivisionParamId; break;
    case kFeedbackMenuMorphShape: id = kMorphShapeParamId; break;
    default:
        if (_openMenu >= kFeedbackMenuEffectBase) {
            const auto pedal = static_cast<s3g::FeedbackPedalType>(
                static_cast<uint32_t>(paramValue(*plugin,
                    nodeParamId(_selectedNode, kNodePedalOffset))));
            const auto info = _editingAux
                ? s3g::feedbackAuxControlInfo(_openMenuControl)
                : s3g::feedbackPedalControlInfo(
                    pedal, _openMenuControl);
            id = _editingAux ? auxParamId(info.storageSlot)
                : feedbackPedalStorageParamId(
                    _selectedNode, info.storageSlot);
            const double normalized = info.menuCount > 1u
                ? static_cast<double>(item)
                    / static_cast<double>(info.menuCount - 1u) : 0.0;
            ParamRange range;
            if (paramRange(id, range)) {
                value = range.minimum
                    + normalized * (range.maximum - range.minimum);
            }
        }
        break;
    }
    if (id != CLAP_INVALID_ID) {
        queueGuiParamGesture(*plugin, id, value);
        _factoryPresetIndex = -1;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "CUSTOM");
    }
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    auto* plugin = static_cast<Plugin*>(_plugin);
    if ([self openMenuItemCount] > 0u) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point,
            [self openMenuRect], kFeedbackMenuItemHeight,
            [self openMenuItemCount]);
        if (hit >= 0) [self selectMenuItem:static_cast<uint32_t>(hit)];
        _openMenu = kFeedbackMenuNone;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }

    const auto titleBand = s3g::gui_layout::matrixTitleBand(kFeedbackCanvas);
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.presetMenu))) {
        [self openMenu:kFeedbackMenuPreset control:0u
            rect:s3g::clap_gui::cocoaRect(titleBand.presetMenu)];
        return;
    }
    if (s3g::clap_gui::handleProcessorTitleClick(point,
            &plugin->plugin, @"Feedback Shift", titleBand,
            _titlePresetName, sizeof(_titlePresetName))) {
        _factoryPresetIndex = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    for (uint32_t page = 0u; page < kFeedbackPageCount; ++page) {
        if (!NSPointInRect(point, feedbackPageButtonRect(page))) continue;
        _page = page;
        _editingAux = page == kFeedbackPageAux;
        _openMenu = kFeedbackMenuNone;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, feedbackRandomButtonRect(false))
        || NSPointInRect(point, feedbackRandomButtonRect(true))) {
        const bool randomizeSceneB = NSPointInRect(
            point, feedbackRandomButtonRect(true));
        _randomSeed = _randomSeed * 1664525u + 1013904223u;
        const auto current = paramsSnapshot(*plugin);
        queueGuiPatch(*plugin, s3g::randomFeedbackShiftScene(
            _randomSeed, current, randomizeSceneB));
        _editingSceneB = randomizeSceneB;
        _factoryPresetIndex = -1;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            randomizeSceneB ? "RANDOM B" : "RANDOM A");
        [self setNeedsDisplay:YES];
        return;
    }

    if (_page == kFeedbackPageInsert) {
        for (uint32_t node = 0u;
             node < s3g::kFeedbackShiftChannels; ++node) {
            if (!NSPointInRect(point,
                    feedbackNodeRandomButtonRect(node))) continue;
            _selectedNode = node;
            _randomSeed = _randomSeed * 1664525u + 1013904223u;
            const auto current = paramsSnapshot(*plugin);
            const auto randomized = s3g::randomFeedbackShiftNodeInsert(
                _randomSeed, current.nodes[node]);
            queueGuiParamGesture(*plugin,
                nodeParamId(node, kNodePedalOffset),
                static_cast<double>(randomized.pedal));
            for (uint32_t slot = 0u;
                 slot < 4u + s3g::kFeedbackPedalExtraParameterCount; ++slot) {
                queueGuiParamGesture(*plugin,
                    feedbackPedalStorageParamId(node, slot),
                    s3g::feedbackPedalStorageValue(randomized, slot));
            }
            _factoryPresetIndex = -1;
            std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                "CUSTOM");
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (_page == kFeedbackPageInsert
        || _page == kFeedbackPageEcology) {
    for (uint32_t node = 0u; node < s3g::kFeedbackShiftChannels; ++node) {
        if (NSPointInRect(point, feedbackNodeButtonRect(_page, node))) {
            _selectedNode = node;
            _openMenu = kFeedbackMenuNone;
            [self setNeedsDisplay:YES];
            return;
        }
    }
    }
    if (NSPointInRect(point, feedbackSceneButtonRect(false))
        || NSPointInRect(point, feedbackSceneButtonRect(true))) {
        _editingSceneB = NSPointInRect(point, feedbackSceneButtonRect(true));
        _openMenu = kFeedbackMenuNone;
        [self setNeedsDisplay:YES];
        return;
    }
    const clap_id selectedRouteId = matrixParamId(_editingSceneB,
        _selectedDestination, _selectedSource);
    if (_page == kFeedbackPagePatch) {
    for (uint32_t destination = 0u;
         destination < s3g::kFeedbackShiftChannels; ++destination) {
        for (uint32_t source = 0u;
             source < s3g::kFeedbackShiftChannels; ++source) {
            if (!NSPointInRect(point,
                    feedbackMatrixCellRect(destination, source))) continue;
            const clap_id id = matrixParamId(
                _editingSceneB, destination, source);
            const double current = paramValue(*plugin, id);
            const bool alreadySelected = source == _selectedSource
                && destination == _selectedDestination;
            _selectedSource = source;
            _selectedDestination = destination;
            const bool negative = ([event modifierFlags]
                & NSEventModifierFlagOption) != 0u;
            bool changed = false;
            double next = current;
            if (std::abs(current) <= 0.001) {
                const double created = source == destination ? 0.94 : 0.25;
                next = negative ? -created : created;
                changed = true;
            } else if (alreadySelected) {
                next = 0.0;
                changed = true;
            }
            if (changed) {
                queueGuiParamGesture(*plugin, id, next);
                _factoryPresetIndex = -1;
                std::snprintf(_titlePresetName,
                    sizeof(_titlePresetName), "%s", "CUSTOM");
            }
            [self setNeedsDisplay:YES];
            return;
        }
    }

    if (NSPointInRect(point, feedbackCrosspointActionRect(0u))) {
        const double current = paramValue(*plugin, selectedRouteId);
        queueGuiParamGesture(*plugin, selectedRouteId,
            std::abs(current) < 0.001 ? 0.25 : -current);
        _factoryPresetIndex = -1;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "CUSTOM");
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, feedbackCrosspointActionRect(1u))) {
        queueGuiParamGesture(*plugin, selectedRouteId, 0.0);
        _factoryPresetIndex = -1;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "CUSTOM");
        [self setNeedsDisplay:YES];
        return;
    }
    }

    if (_page == kFeedbackPagePatch
        && (NSPointInRect(point, feedbackSceneCopyButtonRect(true))
            || NSPointInRect(point, feedbackSceneCopyButtonRect(false)))) {
        const bool toSceneB = NSPointInRect(
            point, feedbackSceneCopyButtonRect(true));
        auto patch = paramsSnapshot(*plugin);
        if (toSceneB) {
            patch.sceneBNodes = patch.nodes;
            patch.sceneBMatrix = patch.matrix;
            patch.sceneBAuxSend = patch.auxSend;
        } else {
            for (uint32_t node = 0u;
                 node < s3g::kFeedbackShiftChannels; ++node) {
                patch.nodes[node].frequencyHz
                    = patch.sceneBNodes[node].frequencyHz;
                patch.nodes[node].regeneration
                    = patch.sceneBNodes[node].regeneration;
                patch.nodes[node].color = patch.sceneBNodes[node].color;
                patch.nodes[node].body = patch.sceneBNodes[node].body;
                patch.nodes[node].levelDb = patch.sceneBNodes[node].levelDb;
            }
            patch.matrix = patch.sceneBMatrix;
            patch.auxSend = patch.sceneBAuxSend;
        }
        queueGuiPatch(*plugin, patch);
        _factoryPresetIndex = -1;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "CUSTOM");
        [self setNeedsDisplay:YES];
        return;
    }

    if (_page == kFeedbackPageInsert
        && NSPointInRect(point,
            feedbackProcessorMenuRect(kNodePanel, 0u))) {
        [self openMenu:kFeedbackMenuNodeMode control:0u
            rect:feedbackProcessorMenuRect(kNodePanel, 0u)]; return;
    }
    if (_page == kFeedbackPageInsert
        && NSPointInRect(point,
            feedbackProcessorMenuRect(kInsertSelectorPanel, 0u))) {
        [self openMenu:kFeedbackMenuInsertCategory control:0u
            rect:feedbackProcessorMenuRect(kInsertSelectorPanel, 0u)]; return;
    }
    if (_page == kFeedbackPageInsert
        && NSPointInRect(point,
            feedbackProcessorMenuRect(kInsertSelectorPanel, 1u))) {
        [self openMenu:kFeedbackMenuInsertEffect control:0u
            rect:feedbackProcessorMenuRect(kInsertSelectorPanel, 1u)]; return;
    }
    if (_page == kFeedbackPageInsert && NSPointInRect(point,
            feedbackProcessorMenuRect(kNodePanel, 1u))) {
        [self openMenu:kFeedbackMenuExciterSource control:0u
            rect:feedbackProcessorMenuRect(kNodePanel, 1u)]; return;
    }
    const auto morphSource = static_cast<s3g::FeedbackMorphSource>(
        static_cast<uint32_t>(paramValue(*plugin, kMorphSourceParamId)));
    const bool pulseMorph = morphSource == s3g::FeedbackMorphSource::Pulse;
    if (_page == kFeedbackPageEcology) {
    for (uint32_t bank = 0u; bank < 3u; ++bank) {
        if (!NSPointInRect(point, feedbackEngineBankButtonRect(bank))) continue;
        _engineBank = bank;
        [self setNeedsDisplay:YES];
        return;
    }
    if (_engineBank == 0u && NSPointInRect(point,
            feedbackProcessorMenuRect(kExcitationPanel, 2u))) {
        [self openMenu:kFeedbackMenuRun control:0u
            rect:feedbackProcessorMenuRect(kExcitationPanel, 2u)]; return;
    }
    if (NSPointInRect(point, feedbackProcessorMenuRect(kMorphLeftPanel, 0u))) {
        [self openMenu:kFeedbackMenuMorphSource control:0u
            rect:feedbackProcessorMenuRect(kMorphLeftPanel, 0u)]; return;
    }
    const uint32_t holdRow = pulseMorph ? 1u : 4u;
    if (NSPointInRect(point,
            feedbackProcessorMenuRect(kMorphRightPanel, holdRow))) {
        [self openMenu:kFeedbackMenuMorphHold control:0u
            rect:feedbackProcessorMenuRect(kMorphRightPanel, holdRow)]; return;
    }
    if (pulseMorph && NSPointInRect(point,
            feedbackProcessorMenuRect(kMorphRightPanel, 2u))) {
        [self openMenu:kFeedbackMenuMorphSync control:0u
            rect:feedbackProcessorMenuRect(kMorphRightPanel, 2u)]; return;
    }
    if (pulseMorph && NSPointInRect(point,
            feedbackProcessorMenuRect(kMorphRightPanel, 3u))) {
        [self openMenu:kFeedbackMenuMorphDivision control:0u
            rect:feedbackProcessorMenuRect(kMorphRightPanel, 3u)]; return;
    }
    if (pulseMorph && NSPointInRect(point,
            feedbackProcessorMenuRect(kMorphRightPanel, 4u))) {
        [self openMenu:kFeedbackMenuMorphShape control:0u
            rect:feedbackProcessorMenuRect(kMorphRightPanel, 4u)]; return;
    }
    if (NSPointInRect(point, feedbackProcessorMenuRect(kOutputPanel, 1u))) {
        [self openMenu:kFeedbackMenuOutputMode control:0u
            rect:feedbackProcessorMenuRect(kOutputPanel, 1u)]; return;
    }
    }

    const auto pedal = static_cast<s3g::FeedbackPedalType>(
        static_cast<uint32_t>(paramValue(*plugin,
            nodeParamId(_selectedNode, kNodePedalOffset))));
    const uint32_t effectRows = _editingAux
        ? s3g::kFeedbackAuxControlCount
        : feedbackVisiblePedalControlCount(pedal);
    const auto pedalPanel = feedbackEffectPanel(effectRows);
    if (_page == kFeedbackPageInsert || _page == kFeedbackPageAux) {
    for (uint32_t row = 0u; row < effectRows; ++row) {
        const uint32_t control = _editingAux ? row
            : feedbackVisiblePedalControlIndex(pedal, row);
        const auto info = _editingAux
            ? s3g::feedbackAuxControlInfo(row)
            : s3g::feedbackPedalControlInfo(pedal, control);
        const auto& controlPanel = _editingAux
            ? (row < 6u ? kAuxFeedbackPanel : kPostGrainPanel)
            : pedalPanel;
        const uint32_t controlRow = _editingAux && row >= 6u
            ? row - 6u : row;
        if (info.display == s3g::FeedbackPedalControlDisplay::Menu
            && NSPointInRect(point,
                feedbackProcessorMenuRect(controlPanel, controlRow))) {
            [self openMenu:kFeedbackMenuEffectBase + static_cast<int>(row)
                control:control rect:feedbackProcessorMenuRect(
                    controlPanel, controlRow)];
            return;
        }
    }
    }

    if (_page == kFeedbackPageEcology && _engineBank == 0u
        && NSPointInRect(point, feedbackActionButtonRect(0u))) {
        plugin->strikeMask.fetch_or(1u << _selectedNode,
            std::memory_order_relaxed);
        if (plugin->host && plugin->host->request_process)
            plugin->host->request_process(plugin->host);
        return;
    }
    if (_page == kFeedbackPageEcology && _engineBank == 0u
        && NSPointInRect(point, feedbackActionButtonRect(1u))) {
        plugin->panicRequested.store(true, std::memory_order_release);
        if (plugin->host && plugin->host->request_process)
            plugin->host->request_process(plugin->host);
        return;
    }

    const auto beginSlider = [&](clap_id id,
                                 const s3g::gui_layout::Panel& panel) {
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &plugin->plugin, id, &defaultValue)) {
            queueGuiParamGesture(*plugin, id, defaultValue);
            _dragParam = -1;
        } else if (queueGuiParamGestureBegin(*plugin, id)) {
            _dragParam = static_cast<int>(id);
            _dragPanelX = panel.frame.x;
            _dragPanelWidth = panel.frame.width;
            [self updateSlider:point];
        }
        [self setNeedsDisplay:YES];
    };
    const auto hitSlider = [&](const s3g::gui_layout::Panel& panel,
                               uint32_t row, clap_id id) {
        if (!NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(panel, row)))) return false;
        beginSlider(id, panel);
        return true;
    };
    if (((_page == kFeedbackPageAux) && (hitSlider(kAuxSendLeftPanel, 0u,
                sceneParamId(_editingSceneB, 0u, kSceneAuxSendOffset))
            || hitSlider(kAuxSendLeftPanel, 1u,
                sceneParamId(_editingSceneB, 1u, kSceneAuxSendOffset))
            || hitSlider(kAuxSendLeftPanel, 2u,
                sceneParamId(_editingSceneB, 2u, kSceneAuxSendOffset))
            || hitSlider(kAuxSendLeftPanel, 3u,
                sceneParamId(_editingSceneB, 3u, kSceneAuxSendOffset))
            || hitSlider(kAuxSendRightPanel, 0u,
                sceneParamId(_editingSceneB, 4u, kSceneAuxSendOffset))
            || hitSlider(kAuxSendRightPanel, 1u,
                sceneParamId(_editingSceneB, 5u, kSceneAuxSendOffset))
            || hitSlider(kAuxSendRightPanel, 2u,
                sceneParamId(_editingSceneB, 6u, kSceneAuxSendOffset))
            || hitSlider(kAuxSendRightPanel, 3u,
                sceneParamId(_editingSceneB, 7u, kSceneAuxSendOffset))))
        || ((_page == kFeedbackPageInsert) && (hitSlider(kNodePanel, 3u,
                sceneParamId(_editingSceneB, _selectedNode,
                    kSceneFrequencyOffset))
            || hitSlider(kNodePanel, 4u,
                sceneParamId(_editingSceneB, _selectedNode,
                    kSceneRegenerationOffset))
            || hitSlider(kNodePanel, 5u,
                sceneParamId(_editingSceneB, _selectedNode,
                    kSceneColorOffset))
            || hitSlider(kNodePanel, 6u,
                sceneParamId(_editingSceneB, _selectedNode,
                    kSceneBodyOffset))
            || hitSlider(kNodePanel, 2u,
                nodeParamId(_selectedNode, kNodeExciterGainOffset))
            || hitSlider(kNodePanel, 7u,
                sceneParamId(_editingSceneB, _selectedNode,
                    kSceneLevelOffset))
            || hitSlider(kNodePanel, 8u,
                sceneParamId(_editingSceneB, _selectedNode,
                    kSceneAuxSendOffset))))
        || ((_page == kFeedbackPageEcology) && (_engineBank == 0u
            ? (hitSlider(kExcitationPanel, 0u, kExciteParamId)
                || hitSlider(kExcitationPanel, 1u, kDriftParamId)
                || hitSlider(kExcitationPanel, 3u, kGovernorRestParamId))
            : _engineBank == 1u
            ? (hitSlider(kExcitationPanel, 0u, kSpliceAmountParamId)
                || hitSlider(kExcitationPanel, 1u, kSpliceRateParamId)
                || hitSlider(kExcitationPanel, 2u,
                    kSpliceRateFineParamId)
                || hitSlider(kExcitationPanel, 3u,
                    kSpliceContrastParamId)
                || hitSlider(kExcitationPanel, 4u, kSpliceSpaceParamId))
            : (hitSlider(kExcitationPanel, 0u, kSubBassTuneParamId)
                || hitSlider(kExcitationPanel, 1u, kSubBassShapeParamId)
                || hitSlider(kExcitationPanel, 2u, kSubBassDriveParamId)
                || hitSlider(kExcitationPanel, 3u, kSubBassDecayParamId)
                || hitSlider(kExcitationPanel, 4u, kSubBassSustainParamId))))
        || ((_page == kFeedbackPageEcology) && (
            hitSlider(kMorphLeftPanel, 1u, kMorphParamId)
        || hitSlider(kMorphLeftPanel, 2u, kMorphDepthParamId)
        || hitSlider(kMorphLeftPanel, 3u, kMorphInertiaParamId)
        || (pulseMorph
            ? hitSlider(kMorphRightPanel, 0u, kMorphRateParamId)
            : (hitSlider(kMorphRightPanel, 0u, kGovernorReflexParamId)
                || hitSlider(kMorphRightPanel, 1u,
                    kGovernorSensitivityParamId)
                || hitSlider(kMorphRightPanel, 2u,
                    kGovernorRecoveryParamId)
                || hitSlider(kMorphRightPanel, 3u, kMorphRateParamId)))
        || hitSlider(kOutputPanel, 0u, kOutputParamId)
        || hitSlider(kOutputPanel, 2u, kOutputRotationParamId)))
        || ((_page == kFeedbackPagePatch)
            && hitSlider(kCrosspointPanel, 0u, selectedRouteId))) return;
    if (_page == kFeedbackPageInsert || _page == kFeedbackPageAux) {
    for (uint32_t row = 0u; row < effectRows; ++row) {
        const uint32_t control = _editingAux ? row
            : feedbackVisiblePedalControlIndex(pedal, row);
        const auto info = _editingAux
            ? s3g::feedbackAuxControlInfo(row)
            : s3g::feedbackPedalControlInfo(pedal, control);
        const auto& controlPanel = _editingAux
            ? (row < 6u ? kAuxFeedbackPanel : kPostGrainPanel)
            : pedalPanel;
        const uint32_t controlRow = _editingAux && row >= 6u
            ? row - 6u : row;
        if (info.display != s3g::FeedbackPedalControlDisplay::Menu
            && hitSlider(controlPanel, controlRow,
                _editingAux ? auxParamId(info.storageSlot)
                : feedbackPedalStorageParamId(
                    _selectedNode, info.storageSlot))) return;
    }
    }
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu == kFeedbackMenuNone) return;
    [self updateMenuHover:[self convertPoint:[event locationInWindow]
        fromView:nil]];
}

- (void)mouseDragged:(NSEvent*)event
{
    [self updateSlider:[self convertPoint:[event locationInWindow]
        fromView:nil]];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (_dragParam >= 0) {
        queueGuiParamGestureEnd(*static_cast<Plugin*>(_plugin),
            static_cast<clap_id>(_dragParam));
    }
    _dragParam = -1;
}

@end
