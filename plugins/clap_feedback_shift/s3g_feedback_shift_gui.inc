// The owning translation unit hosts this canvas in the shared
// ResponsiveViewport; the controls below follow the Processor-family contract.
constexpr s3g::gui_layout::Canvas kFeedbackCanvas {
    static_cast<double>(kGuiWidth), static_cast<double>(kGuiHeight) };
constexpr s3g::gui_layout::Panel kMatrixPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Routing,
    { 18.0, 42.0, 530.0, 530.0 }, 36.0, 26.0, 0u };
constexpr s3g::gui_layout::Panel kNodePanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 566.0, 42.0, 516.0, 216.0 }, 64.0, 24.0, 6u };
constexpr s3g::gui_layout::Panel kNodeLeftPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 566.0, 42.0, 258.0, 216.0 }, 64.0, 28.0, 6u };
constexpr s3g::gui_layout::Panel kNodeRightPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 824.0, 42.0, 258.0, 216.0 }, 64.0, 28.0, 6u };
constexpr s3g::gui_layout::Panel kAuxSendPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 566.0, 42.0, 516.0, 216.0 }, 0.0, 0.0, 0u };
constexpr s3g::gui_layout::Panel kAuxSendLeftPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 566.0, 42.0, 258.0, 216.0 }, 76.0, 36.0, 4u };
constexpr s3g::gui_layout::Panel kAuxSendRightPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 824.0, 42.0, 258.0, 216.0 }, 76.0, 36.0, 4u };
constexpr s3g::gui_layout::Panel kAuxFeedbackPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::ToneShape,
    { 566.0, 270.0, 252.0, 314.0 }, 50.0, 42.0, 6u };
constexpr s3g::gui_layout::Panel kPostGrainPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::ToneShape,
    { 830.0, 270.0, 252.0, 314.0 }, 43.0, 36.0, 8u };
constexpr s3g::gui_layout::Panel kExcitationPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Engine,
    { 18.0, 584.0, 254.0, 158.0 }, 36.0, 26.0, 5u };
constexpr s3g::gui_layout::Panel kRhythmPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Modulation,
    { 284.0, 584.0, 264.0, 158.0 }, 36.0, 26.0, 5u };
constexpr s3g::gui_layout::Panel kOutputPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Output,
    { 566.0, 596.0, 516.0, 148.0 }, 36.0, 26.0, 4u };
constexpr s3g::gui_layout::Panel kCrosspointPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Routing,
    { 34.0, 472.0, 498.0, 88.0 }, 34.0, 26.0, 2u };

constexpr CGFloat kFeedbackMatrixX = 142.0;
constexpr CGFloat kFeedbackMatrixY = 105.0;
constexpr CGFloat kFeedbackMatrixCell = 46.0;
constexpr CGFloat kFeedbackMenuItemHeight = 18.0;

enum FeedbackMenu : int {
    kFeedbackMenuNone = 0,
    kFeedbackMenuPreset,
    kFeedbackMenuNodeMode,
    kFeedbackMenuPedal,
    kFeedbackMenuExciterSource,
    kFeedbackMenuMotionSource,
    kFeedbackMenuMotionTarget,
    kFeedbackMenuOutputMode,
    kFeedbackMenuRun,
    kFeedbackMenuPulseSync,
    kFeedbackMenuPulseDivision,
    kFeedbackMenuPulseShape,
    kFeedbackMenuEffectBase = 100,
};

s3g::gui_layout::Panel feedbackEffectPanel(uint32_t rows)
{
    const double rowPitch = s3g::gui_layout::kStandardMetrics.rowPitch;
    const double height = rows == 0u ? 62.0
        : s3g::gui_layout::toolboxHeightForRows(rows,
            s3g::gui_layout::kStandardMetrics.toolboxBottomClearance,
            rowPitch);
    return {
        s3g::gui_layout::PluginClass::MixerMatrixLane,
        s3g::gui_layout::PanelRole::ToneShape,
        { 566.0, 270.0, 516.0, height },
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

NSRect feedbackNodeButtonRect(uint32_t node)
{
    return NSMakeRect(582.0 + static_cast<CGFloat>(node) * 51.0,
        68.0, 43.0, 22.0);
}

NSRect feedbackAuxButtonRect()
{
    return NSMakeRect(998.0, 68.0, 68.0, 22.0);
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
    return NSMakeRect(34.0 + static_cast<CGFloat>(index) * 112.0,
        y, 102.0, 18.0);
}

NSRect feedbackCrosspointActionRect(uint32_t index)
{
    const CGFloat x = s3g::gui_layout::processorControlX(
        kCrosspointPanel.frame.x);
    const CGFloat y = s3g::gui_layout::rowY(kCrosspointPanel, 1u) - 2.0;
    return NSMakeRect(x + static_cast<CGFloat>(index) * 112.0,
        y, 102.0, 18.0);
}

clap_id feedbackPedalStorageParamId(uint32_t node, uint32_t storageSlot)
{
    if (storageSlot < 4u) {
        return nodeParamId(node, kNodePedalAmountOffset + storageSlot);
    }
    return nodeParamId(node,
        kNodePedalExtra0Offset + storageSlot - 4u);
}

CGFloat feedbackNormalizedParam(const Plugin& plugin, clap_id id)
{
    uint32_t node = 0u;
    clap_id offset = 0u;
    const double value = paramValue(plugin, id);
    if (decodeNodeParam(id, node, offset)
        && offset == kNodeFrequencyOffset) {
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
    bool _editingAux;
    uint32_t _selectedSource;
    uint32_t _selectedDestination;
    int _factoryPresetIndex;
    uint32_t _randomSeed;
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
        _editingAux = false;
        _selectedSource = 0u;
        _selectedDestination = 0u;
        _factoryPresetIndex = 0;
        _randomSeed = 0x53334642u;
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
    case kFeedbackMenuPedal: return s3g::kFeedbackPedalTypeCount;
    case kFeedbackMenuExciterSource:
        return s3g::kFeedbackExciterSourceCount;
    case kFeedbackMenuMotionSource:
        return s3g::kFeedbackMotionSourceCount;
    case kFeedbackMenuMotionTarget:
        return s3g::kFeedbackMotionTargetCount;
    case kFeedbackMenuOutputMode: return s3g::kFeedbackShiftOutputModeCount;
    case kFeedbackMenuRun:
    case kFeedbackMenuPulseSync: return 2u;
    case kFeedbackMenuPulseDivision:
        return static_cast<uint32_t>(s3g::kFeedbackPulseDivisionBeats.size());
    case kFeedbackMenuPulseShape: return s3g::kFeedbackPulseShapeCount;
    default:
        if (_openMenu >= kFeedbackMenuEffectBase) {
            const auto pedal = static_cast<s3g::FeedbackPedalType>(
                static_cast<uint32_t>(paramValue(
                    *static_cast<Plugin*>(_plugin),
                    nodeParamId(_selectedNode, kNodePedalOffset))));
            return s3g::feedbackPedalControlInfo(
                pedal, _openMenuControl).menuCount;
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
    case kFeedbackMenuPedal:
        return [NSString stringWithUTF8String:s3g::feedbackPedalName(
            static_cast<s3g::FeedbackPedalType>(std::min(item,
                s3g::kFeedbackPedalTypeCount - 1u)))];
    case kFeedbackMenuExciterSource:
        return [NSString stringWithUTF8String:s3g::feedbackExciterSourceName(
            static_cast<s3g::FeedbackExciterSource>(std::min(item,
                s3g::kFeedbackExciterSourceCount - 1u)))];
    case kFeedbackMenuMotionSource:
        return [NSString stringWithUTF8String:s3g::feedbackMotionSourceName(
            static_cast<s3g::FeedbackMotionSource>(std::min(item,
                s3g::kFeedbackMotionSourceCount - 1u)))];
    case kFeedbackMenuMotionTarget:
        return [NSString stringWithUTF8String:s3g::feedbackMotionTargetName(
            static_cast<s3g::FeedbackMotionTarget>(std::min(item,
                s3g::kFeedbackMotionTargetCount - 1u)))];
    case kFeedbackMenuOutputMode:
        return [NSString stringWithUTF8String:s3g::feedbackShiftOutputModeName(
            static_cast<s3g::FeedbackShiftOutputMode>(std::min(item,
                s3g::kFeedbackShiftOutputModeCount - 1u)))];
    case kFeedbackMenuRun:
    case kFeedbackMenuPulseSync: return item == 0u ? @"OFF" : @"ON";
    case kFeedbackMenuPulseDivision:
        return [NSString stringWithUTF8String:
            s3g::feedbackPulseDivisionName(item)];
    case kFeedbackMenuPulseShape:
        return [NSString stringWithUTF8String:s3g::feedbackPulseShapeName(
            static_cast<s3g::FeedbackPulseShape>(std::min(item,
                s3g::kFeedbackPulseShapeCount - 1u)))];
    default:
        if (_openMenu >= kFeedbackMenuEffectBase) {
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
    case kFeedbackMenuPedal: return static_cast<int>(paramValue(*plugin,
        nodeParamId(_selectedNode, kNodePedalOffset)));
    case kFeedbackMenuExciterSource:
        return static_cast<int>(paramValue(*plugin,
            exciterParamId(_selectedNode, kExciterSourceOffset)));
    case kFeedbackMenuMotionSource:
        return static_cast<int>(paramValue(*plugin,
            motionParamId(_selectedNode, kMotionSourceOffset)));
    case kFeedbackMenuMotionTarget:
        return static_cast<int>(paramValue(*plugin,
            motionParamId(_selectedNode, kMotionTargetOffset)));
    case kFeedbackMenuOutputMode:
        return static_cast<int>(paramValue(*plugin, kOutputModeParamId));
    case kFeedbackMenuRun:
        return static_cast<int>(paramValue(*plugin, kRunParamId));
    case kFeedbackMenuPulseSync:
        return static_cast<int>(paramValue(*plugin, kPulseSyncParamId));
    case kFeedbackMenuPulseDivision:
        return static_cast<int>(paramValue(*plugin, kPulseDivisionParamId));
    case kFeedbackMenuPulseShape:
        return static_cast<int>(paramValue(*plugin, kPulseShapeParamId));
    default:
        if (_openMenu >= kFeedbackMenuEffectBase) {
            const auto pedal = static_cast<s3g::FeedbackPedalType>(
                static_cast<uint32_t>(paramValue(*plugin,
                    nodeParamId(_selectedNode, kNodePedalOffset))));
            const auto info = s3g::feedbackPedalControlInfo(
                pedal, _openMenuControl);
            const clap_id id = feedbackPedalStorageParamId(
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
    NSString* status = [NSString stringWithFormat:@"%s  //  %@",
        s3g::feedbackShiftOutputModeName(outputMode),
        s3g::clap_gui::peakDbText(peak)];
    const auto titleBand = s3g::gui_layout::matrixTitleBand(kFeedbackCanvas);
    s3g::clap_gui::drawProcessorTitleBand(
        @"s3g PROCESSOR FEEDBACK SHIFT 8CH",
        [NSString stringWithUTF8String:_titlePresetName], status,
        titleBand, s3g::clap_gui::softTitleAttrs(), labels, values, style);
    s3g::clap_gui::drawHeaderActionButton(
        s3g::clap_gui::cocoaRect(titleBand.randomButton),
        s3g::clap_gui::cocoaRect(titleBand.randomButton),
        @"RANDOM", labels, style);

    const auto pedal = static_cast<s3g::FeedbackPedalType>(
        static_cast<uint32_t>(paramValue(*plugin,
            nodeParamId(_selectedNode, kNodePedalOffset))));
    const uint32_t effectRows = _editingAux
        ? s3g::kFeedbackAuxControlCount
        : s3g::feedbackPedalControlCount(pedal);
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
    [self drawPanel:@"PATCH MATRIX // SOURCE > DESTINATION"
        panel:kMatrixPanel style:style labels:labels];
    [self drawPanel:(_editingAux ? @"AUX SENDS // NODE > BUS"
                                  : @"SELECTED NODE")
        panel:(_editingAux ? kAuxSendPanel : kNodePanel)
        style:style labels:labels];
    if (_editingAux) {
        [self drawPanel:[NSString stringWithFormat:
                @"FEEDBACK AUX // %.1f dB GR",
                plugin->auxGainReductionDb.load(std::memory_order_relaxed)]
            panel:kAuxFeedbackPanel style:style labels:labels];
        [self drawPanel:[NSString stringWithFormat:
                @"POST GRANULATOR // %.1f GRAINS",
                plugin->auxGrainActivity.load(
                    std::memory_order_relaxed) * 10.0f]
            panel:kPostGrainPanel style:style labels:labels];
    } else {
        [self drawPanel:(temporalPedal
                ? [NSString stringWithFormat:@"INSERT // %s // %@",
                    s3g::feedbackPedalName(pedal), temporalState]
                : [NSString stringWithFormat:@"INSERT // %s",
                    s3g::feedbackPedalName(pedal)]) panel:pedalPanel
            style:style labels:labels];
    }
    [self drawPanel:@"EXCITATION" panel:kExcitationPanel
        style:style labels:labels];
    [self drawPanel:@"RHYTHM" panel:kRhythmPanel
        style:style labels:labels];
    [self drawPanel:@"OUTPUT TOPOLOGY" panel:kOutputPanel
        style:style labels:labels];
    if (temporalPedal && temporalPhase != 0u) {
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

    [@"SOURCE" drawAtPoint:NSMakePoint(296.0, 72.0)
        withAttributes:labels];
    [@"DEST" drawAtPoint:NSMakePoint(56.0, 276.0)
        withAttributes:labels];
    for (uint32_t index = 0u; index < s3g::kFeedbackShiftChannels; ++index) {
        NSString* number = [NSString stringWithFormat:@"%u", index + 1u];
        [number drawAtPoint:NSMakePoint(kFeedbackMatrixX
            + static_cast<CGFloat>(index) * kFeedbackMatrixCell + 13.0,
            86.0) withAttributes:values];
        [number drawAtPoint:NSMakePoint(111.0, kFeedbackMatrixY
            + static_cast<CGFloat>(index) * kFeedbackMatrixCell + 8.0)
            withAttributes:values];
    }
    for (uint32_t destination = 0u;
         destination < s3g::kFeedbackShiftChannels; ++destination) {
        for (uint32_t source = 0u;
             source < s3g::kFeedbackShiftChannels; ++source) {
            const uint32_t index = destination
                * s3g::kFeedbackShiftChannels + source;
            const double route = paramValue(*plugin,
                matrixParamId(destination, source));
            const float activity = plugin->routeActivity[index].load(
                std::memory_order_relaxed);
            const NSRect cell = feedbackMatrixCellRect(destination, source);
            [style.strip setFill];
            NSRectFill(cell);
            const bool active = std::abs(route) > 0.001;
            const bool selected = source == _selectedSource
                && destination == _selectedDestination;
            NSColor* routeColor = route >= 0.0
                ? s3g::clap_gui::color(0x86a48c)
                : s3g::clap_gui::color(0xb28a72);
            [(selected ? s3g::clap_gui::color(0xc9b86f)
                       : active ? routeColor : style.grid) setStroke];
            NSFrameRect(cell);
            if (selected) {
                [s3g::clap_gui::color(0xc9b86f) setStroke];
                NSFrameRect(NSInsetRect(cell, 2.0, 2.0));
            }
            if (active) {
                const CGFloat radius = 3.5 + std::min<CGFloat>(7.0,
                    std::abs(route) * 4.0 + activity * 3.0);
                [routeColor setFill];
                [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
                    NSMidX(cell) - radius, NSMidY(cell) - radius,
                    radius * 2.0, radius * 2.0)] fill];
                [[NSString stringWithFormat:@"%+.1f", route]
                    drawAtPoint:NSMakePoint(NSMinX(cell) + 3.0,
                        NSMaxY(cell) - 11.0) withAttributes:values];
            }
        }
    }
    [style.grid setFill];
    NSRectFill(NSMakeRect(kCrosspointPanel.frame.x,
        kCrosspointPanel.frame.y, kCrosspointPanel.frame.width, 1.0));
    [[NSString stringWithFormat:@"CROSSPOINT // S%u > D%u",
        _selectedSource + 1u, _selectedDestination + 1u]
        drawAtPoint:NSMakePoint(kCrosspointPanel.frame.x + 10.0,
            kCrosspointPanel.frame.y + 8.0) withAttributes:labels];
    const clap_id selectedRouteId = matrixParamId(
        _selectedDestination, _selectedSource);
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

    for (uint32_t node = 0u; node < s3g::kFeedbackShiftChannels; ++node) {
        const bool selected = !_editingAux && node == _selectedNode;
        drawFeedbackActionButton(feedbackNodeButtonRect(node),
            [NSString stringWithFormat:@"N%u", node + 1u], selected, style);
        const CGFloat activity = plugin->nodeActivity[node].load(
            std::memory_order_relaxed);
        if (activity > 0.001) {
            NSRect meter = feedbackNodeButtonRect(node);
            meter.origin.y = NSMaxY(meter) - 2.0;
            meter.size.height = 2.0;
            meter.size.width *= std::min<CGFloat>(1.0, activity);
            [s3g::clap_gui::color(0x86a48c) setFill];
            NSRectFill(meter);
        }
    }
    const CGFloat auxActivity = plugin->auxActivity.load(
        std::memory_order_relaxed);
    drawFeedbackActionButton(feedbackAuxButtonRect(), @"AUX", _editingAux,
        style);
    if (auxActivity > 0.001f) {
        NSRect meter = feedbackAuxButtonRect();
        meter.origin.y = NSMaxY(meter) - 2.0;
        meter.size.height = 2.0;
        meter.size.width *= std::min<CGFloat>(1.0, auxActivity * 2.0);
        [s3g::clap_gui::color(0xc9b86f) setFill];
        NSRectFill(meter);
    }
    if (_editingAux) {
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
                id:auxSendParamId(node) row:node % 4u panel:panel
                style:style labels:labels values:values];
        }
    } else {
        [style.grid setFill];
        NSRectFill(NSMakeRect(kNodeRightPanel.frame.x,
            kNodePanel.frame.y
                + s3g::gui_layout::kStandardMetrics.headerHeight,
            1.0, kNodePanel.frame.height
                - s3g::gui_layout::kStandardMetrics.headerHeight));
        [self drawMenu:@"MODE" id:nodeParamId(_selectedNode, kNodeModeOffset)
            row:0u panel:kNodeLeftPanel style:style labels:labels values:values];
        [self drawMenu:@"INSERT" id:nodeParamId(_selectedNode, kNodePedalOffset)
            row:1u panel:kNodeLeftPanel style:style labels:labels values:values];
        [self drawSlider:@"FREQ" id:nodeParamId(_selectedNode,
            kNodeFrequencyOffset) row:2u panel:kNodeLeftPanel style:style
            labels:labels values:values];
        [self drawSlider:@"REGEN" id:nodeParamId(_selectedNode,
            kNodeRegenerationOffset) row:3u panel:kNodeLeftPanel style:style
            labels:labels values:values];
        [self drawSlider:@"COLOR" id:nodeParamId(_selectedNode,
            kNodeColorOffset) row:4u panel:kNodeLeftPanel style:style
            labels:labels values:values];
        [self drawSlider:@"LEVEL" id:nodeParamId(_selectedNode,
            kNodeLevelOffset) row:5u panel:kNodeLeftPanel style:style
            labels:labels values:values];
        [self drawMenu:@"SOURCE" id:exciterParamId(_selectedNode,
            kExciterSourceOffset) row:0u panel:kNodeRightPanel
            style:style labels:labels values:values];
        [self drawSlider:@"INPUT" id:exciterParamId(_selectedNode,
            kExciterGainOffset) row:1u panel:kNodeRightPanel
            style:style labels:labels values:values];
        [self drawMenu:@"MOTION" id:motionParamId(_selectedNode,
            kMotionSourceOffset) row:2u panel:kNodeRightPanel
            style:style labels:labels values:values];
        [self drawMenu:@"TARGET" id:motionParamId(_selectedNode,
            kMotionTargetOffset) row:3u panel:kNodeRightPanel
            style:style labels:labels values:values];
        [self drawSlider:@"DEPTH" id:motionParamId(_selectedNode,
            kMotionDepthOffset) row:4u panel:kNodeRightPanel
            style:style labels:labels values:values];
        [self drawSlider:@"SLEW" id:motionParamId(_selectedNode,
            kMotionSlewOffset) row:5u panel:kNodeRightPanel
            style:style labels:labels values:values];
    }

    if (effectRows == 0u) {
        [@"SIGNAL PASSES DIRECTLY TO THE MATRIX"
            drawAtPoint:NSMakePoint(pedalPanel.frame.x + 16.0,
                pedalPanel.frame.y + 37.0) withAttributes:values];
    }
    for (uint32_t row = 0u; row < effectRows; ++row) {
        const auto info = _editingAux
            ? s3g::feedbackAuxControlInfo(row)
            : s3g::feedbackPedalControlInfo(pedal, row);
        const clap_id id = _editingAux ? auxParamId(info.storageSlot)
            : feedbackPedalStorageParamId(_selectedNode, info.storageSlot);
        const CGFloat normalized = feedbackNormalizedParam(*plugin, id);
        NSString* value = _editingAux
            ? feedbackAuxControlText(info, normalized, row)
            : feedbackPedalControlText(info, normalized, pedal, row);
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

    [self drawSlider:@"EXCITE" id:kExciteParamId row:0u
        panel:kExcitationPanel style:style labels:labels values:values];
    [self drawSlider:@"DRIFT" id:kDriftParamId row:1u
        panel:kExcitationPanel style:style labels:labels values:values];
    [self drawSlider:@"MOTION" id:kMotionRateParamId row:2u
        panel:kExcitationPanel style:style labels:labels values:values];
    [self drawMenu:@"RUN" id:kRunParamId row:3u panel:kExcitationPanel
        style:style labels:labels values:values];
    drawFeedbackActionButton(feedbackActionButtonRect(0u),
        [NSString stringWithFormat:@"HIT N%u", _selectedNode + 1u],
        false, style);
    drawFeedbackActionButton(feedbackActionButtonRect(1u), @"PANIC",
        false, style);

    [self drawSlider:@"DEPTH" id:kPulseDepthParamId row:0u panel:kRhythmPanel
        style:style labels:labels values:values];
    [self drawSlider:@"RATE" id:kPulseRateParamId row:1u panel:kRhythmPanel
        style:style labels:labels values:values];
    [self drawMenu:@"SYNC" id:kPulseSyncParamId row:2u panel:kRhythmPanel
        style:style labels:labels values:values];
    [self drawMenu:@"DIV" id:kPulseDivisionParamId row:3u panel:kRhythmPanel
        style:style labels:labels values:values];
    [self drawMenu:@"SHAPE" id:kPulseShapeParamId row:4u panel:kRhythmPanel
        style:style labels:labels values:values];
    const CGFloat phase = plugin->pulsePhase.load(std::memory_order_relaxed);
    [s3g::clap_gui::color(0xc9b86f) setFill];
    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
        kRhythmPanel.frame.x + kRhythmPanel.frame.width - 31.0
            + phase * 12.0,
        kRhythmPanel.frame.y + 7.5, 4.0, 4.0)] fill];

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
    double value = 0.0;
    if (decodeNodeParam(id, node, offset)
        && offset == kNodeFrequencyOffset) {
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
    case kFeedbackMenuPedal:
        id = nodeParamId(_selectedNode, kNodePedalOffset); break;
    case kFeedbackMenuExciterSource:
        id = exciterParamId(_selectedNode, kExciterSourceOffset); break;
    case kFeedbackMenuMotionSource:
        id = motionParamId(_selectedNode, kMotionSourceOffset); break;
    case kFeedbackMenuMotionTarget:
        id = motionParamId(_selectedNode, kMotionTargetOffset); break;
    case kFeedbackMenuOutputMode: id = kOutputModeParamId; break;
    case kFeedbackMenuRun: id = kRunParamId; break;
    case kFeedbackMenuPulseSync: id = kPulseSyncParamId; break;
    case kFeedbackMenuPulseDivision: id = kPulseDivisionParamId; break;
    case kFeedbackMenuPulseShape: id = kPulseShapeParamId; break;
    default:
        if (_openMenu >= kFeedbackMenuEffectBase) {
            const auto pedal = static_cast<s3g::FeedbackPedalType>(
                static_cast<uint32_t>(paramValue(*plugin,
                    nodeParamId(_selectedNode, kNodePedalOffset))));
            const auto info = s3g::feedbackPedalControlInfo(
                pedal, _openMenuControl);
            id = feedbackPedalStorageParamId(_selectedNode, info.storageSlot);
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
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.randomButton))) {
        _randomSeed = _randomSeed * 1664525u + 1013904223u;
        const auto current = paramsSnapshot(*plugin);
        queueGuiPatch(*plugin,
            s3g::randomFeedbackShiftParams(_randomSeed, current));
        _factoryPresetIndex = -1;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "GENERATED");
        [self setNeedsDisplay:YES];
        return;
    }

    for (uint32_t node = 0u; node < s3g::kFeedbackShiftChannels; ++node) {
        if (NSPointInRect(point, feedbackNodeButtonRect(node))) {
            _selectedNode = node;
            _editingAux = false;
            _openMenu = kFeedbackMenuNone;
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (NSPointInRect(point, feedbackAuxButtonRect())) {
        _editingAux = true;
        _openMenu = kFeedbackMenuNone;
        [self setNeedsDisplay:YES];
        return;
    }
    for (uint32_t destination = 0u;
         destination < s3g::kFeedbackShiftChannels; ++destination) {
        for (uint32_t source = 0u;
             source < s3g::kFeedbackShiftChannels; ++source) {
            if (!NSPointInRect(point,
                    feedbackMatrixCellRect(destination, source))) continue;
            const clap_id id = matrixParamId(destination, source);
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

    const clap_id selectedRouteId = matrixParamId(
        _selectedDestination, _selectedSource);
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

    if (!_editingAux
        && NSPointInRect(point,
            feedbackProcessorMenuRect(kNodeLeftPanel, 0u))) {
        [self openMenu:kFeedbackMenuNodeMode control:0u
            rect:feedbackProcessorMenuRect(kNodeLeftPanel, 0u)]; return;
    }
    if (!_editingAux
        && NSPointInRect(point,
            feedbackProcessorMenuRect(kNodeLeftPanel, 1u))) {
        [self openMenu:kFeedbackMenuPedal control:0u
            rect:feedbackProcessorMenuRect(kNodeLeftPanel, 1u)]; return;
    }
    if (!_editingAux && NSPointInRect(point,
            feedbackProcessorMenuRect(kNodeRightPanel, 0u))) {
        [self openMenu:kFeedbackMenuExciterSource control:0u
            rect:feedbackProcessorMenuRect(kNodeRightPanel, 0u)]; return;
    }
    if (!_editingAux && NSPointInRect(point,
            feedbackProcessorMenuRect(kNodeRightPanel, 2u))) {
        [self openMenu:kFeedbackMenuMotionSource control:0u
            rect:feedbackProcessorMenuRect(kNodeRightPanel, 2u)]; return;
    }
    if (!_editingAux && NSPointInRect(point,
            feedbackProcessorMenuRect(kNodeRightPanel, 3u))) {
        [self openMenu:kFeedbackMenuMotionTarget control:0u
            rect:feedbackProcessorMenuRect(kNodeRightPanel, 3u)]; return;
    }
    if (NSPointInRect(point,
            feedbackProcessorMenuRect(kExcitationPanel, 3u))) {
        [self openMenu:kFeedbackMenuRun control:0u
            rect:feedbackProcessorMenuRect(kExcitationPanel, 3u)]; return;
    }
    if (NSPointInRect(point, feedbackProcessorMenuRect(kRhythmPanel, 2u))) {
        [self openMenu:kFeedbackMenuPulseSync control:0u
            rect:feedbackProcessorMenuRect(kRhythmPanel, 2u)]; return;
    }
    if (NSPointInRect(point, feedbackProcessorMenuRect(kRhythmPanel, 3u))) {
        [self openMenu:kFeedbackMenuPulseDivision control:0u
            rect:feedbackProcessorMenuRect(kRhythmPanel, 3u)]; return;
    }
    if (NSPointInRect(point, feedbackProcessorMenuRect(kRhythmPanel, 4u))) {
        [self openMenu:kFeedbackMenuPulseShape control:0u
            rect:feedbackProcessorMenuRect(kRhythmPanel, 4u)]; return;
    }
    if (NSPointInRect(point, feedbackProcessorMenuRect(kOutputPanel, 1u))) {
        [self openMenu:kFeedbackMenuOutputMode control:0u
            rect:feedbackProcessorMenuRect(kOutputPanel, 1u)]; return;
    }

    const auto pedal = static_cast<s3g::FeedbackPedalType>(
        static_cast<uint32_t>(paramValue(*plugin,
            nodeParamId(_selectedNode, kNodePedalOffset))));
    const uint32_t effectRows = _editingAux
        ? s3g::kFeedbackAuxControlCount
        : s3g::feedbackPedalControlCount(pedal);
    const auto pedalPanel = feedbackEffectPanel(effectRows);
    for (uint32_t row = 0u; row < effectRows; ++row) {
        const auto info = _editingAux
            ? s3g::feedbackAuxControlInfo(row)
            : s3g::feedbackPedalControlInfo(pedal, row);
        const auto& controlPanel = _editingAux
            ? (row < 6u ? kAuxFeedbackPanel : kPostGrainPanel)
            : pedalPanel;
        const uint32_t controlRow = _editingAux && row >= 6u
            ? row - 6u : row;
        if (info.display == s3g::FeedbackPedalControlDisplay::Menu
            && NSPointInRect(point,
                feedbackProcessorMenuRect(controlPanel, controlRow))) {
            [self openMenu:kFeedbackMenuEffectBase + static_cast<int>(row)
                control:row rect:feedbackProcessorMenuRect(
                    controlPanel, controlRow)];
            return;
        }
    }

    if (NSPointInRect(point, feedbackActionButtonRect(0u))) {
        plugin->strikeMask.fetch_or(1u << _selectedNode,
            std::memory_order_relaxed);
        if (plugin->host && plugin->host->request_process)
            plugin->host->request_process(plugin->host);
        return;
    }
    if (NSPointInRect(point, feedbackActionButtonRect(1u))) {
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
    if ((_editingAux && (hitSlider(kAuxSendLeftPanel, 0u,
                auxSendParamId(0u))
            || hitSlider(kAuxSendLeftPanel, 1u, auxSendParamId(1u))
            || hitSlider(kAuxSendLeftPanel, 2u, auxSendParamId(2u))
            || hitSlider(kAuxSendLeftPanel, 3u, auxSendParamId(3u))
            || hitSlider(kAuxSendRightPanel, 0u, auxSendParamId(4u))
            || hitSlider(kAuxSendRightPanel, 1u, auxSendParamId(5u))
            || hitSlider(kAuxSendRightPanel, 2u, auxSendParamId(6u))
            || hitSlider(kAuxSendRightPanel, 3u, auxSendParamId(7u))))
        || (!_editingAux && (hitSlider(kNodeLeftPanel, 2u,
                nodeParamId(_selectedNode, kNodeFrequencyOffset))
            || hitSlider(kNodeLeftPanel, 3u,
                nodeParamId(_selectedNode, kNodeRegenerationOffset))
            || hitSlider(kNodeLeftPanel, 4u,
                nodeParamId(_selectedNode, kNodeColorOffset))
            || hitSlider(kNodeLeftPanel, 5u,
                nodeParamId(_selectedNode, kNodeLevelOffset))
            || hitSlider(kNodeRightPanel, 1u,
                exciterParamId(_selectedNode, kExciterGainOffset))
            || hitSlider(kNodeRightPanel, 4u,
                motionParamId(_selectedNode, kMotionDepthOffset))
            || hitSlider(kNodeRightPanel, 5u,
                motionParamId(_selectedNode, kMotionSlewOffset))))
        || hitSlider(kExcitationPanel, 0u, kExciteParamId)
        || hitSlider(kExcitationPanel, 1u, kDriftParamId)
        || hitSlider(kExcitationPanel, 2u, kMotionRateParamId)
        || hitSlider(kRhythmPanel, 0u, kPulseDepthParamId)
        || hitSlider(kRhythmPanel, 1u, kPulseRateParamId)
        || hitSlider(kOutputPanel, 0u, kOutputParamId)
        || hitSlider(kOutputPanel, 2u, kOutputRotationParamId)
        || hitSlider(kCrosspointPanel, 0u, selectedRouteId)) return;
    for (uint32_t row = 0u; row < effectRows; ++row) {
        const auto info = _editingAux
            ? s3g::feedbackAuxControlInfo(row)
            : s3g::feedbackPedalControlInfo(pedal, row);
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
