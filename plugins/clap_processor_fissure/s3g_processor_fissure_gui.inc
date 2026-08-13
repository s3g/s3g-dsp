namespace {

constexpr auto kMatrixPanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Routing,
    { 12.0, 42.0, 620.0, 716.0 }, 32.0, 24.0, 0u };
constexpr auto kOutputPanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Output,
    { 644.0, 42.0, 344.0, 80.0 }, 32.0, 24.0, 2u };
constexpr auto kInputPanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Source,
    { 644.0, 134.0, 344.0, 132.0 }, 32.0, 27.0, 3u };
constexpr auto kExciterPanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Engine,
    { 644.0, 278.0, 344.0, 106.0 }, 32.0, 27.0, 3u };
constexpr auto kMaterialPanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::ToneShape,
    { 644.0, 396.0, 344.0, 132.0 }, 32.0, 24.0, 4u };
constexpr auto kScenePanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Modulation,
    { 1000.0, 42.0, 344.0, 142.0 }, 118.0, 24.0, 1u };
constexpr auto kObjectPanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 1000.0, 196.0, 344.0, 246.0 }, 80.0, 29.0, 6u };
constexpr auto kFormPanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::EventTiming,
    { 1000.0, 454.0, 344.0, 156.0 }, 32.0, 24.0, 5u };
constexpr auto kGesturePanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Modulation,
    { 1000.0, 622.0, 344.0, 136.0 }, 32.0, 24.0, 1u };
constexpr auto kFracturePanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Modulation,
    { 644.0, 540.0, 344.0, 218.0 }, 32.0, 24.0, 0u };

constexpr CGFloat kMatrixX = 142.0;
constexpr CGFloat kMatrixY = 184.0;
constexpr CGFloat kMatrixCell = 44.0;
constexpr CGFloat kPresetMenuItemHeight = 20.0;
constexpr uint32_t kPresetMenuColumns = 2u;

struct FissureUiRow {
    clap_id id;
    const char* label;
    const s3g::gui_layout::Panel* panel;
    uint32_t row;
};

constexpr std::array<FissureUiRow, 17u> kUiRows {{
    { kOutputParamId, "OUT", &kOutputPanel, 0u },
    { kInputParamId, "MIC IN", &kInputPanel, 0u },
    { kContactParamId, "CONTACT", &kInputPanel, 1u },
    { kVoiceParamId, "INPUT COUPLE", &kInputPanel, 2u },
    { kShakerParamId, "BOX IMPACT", &kExciterPanel, 0u },
    { kRattleParamId, "LOOSE RATTLE", &kExciterPanel, 1u },
    { kSpringParamId, "SPRING WIRE", &kExciterPanel, 2u },
    { kPressureParamId, "PRESSURE", &kMaterialPanel, 0u },
    { kMassParamId, "MASS", &kMaterialPanel, 1u },
    { kBodyParamId, "BODY", &kMaterialPanel, 2u },
    { kMemoryParamId, "MEMORY / FRAG", &kMaterialPanel, 3u },
    { kEdgeParamId, "EDGE", &kFormPanel, 0u },
    { kRateParamId, "RATE", &kFormPanel, 1u },
    { kVoidParamId, "VOID PROBABILITY", &kFormPanel, 2u },
    { kSpaceParamId, "SPACE", &kFormPanel, 3u },
    { kMotionParamId, "MOTION", &kFormPanel, 4u },
    { kSeedParamId, "SEED", &kScenePanel, 0u },
}};

// The left puck controls realized cut density; the right shapes each rupture.
constexpr std::array<clap_id, 2u> kFractureXParamIds {{
    kFractureForceParamId, kFractureDistanceParamId,
}};
constexpr std::array<clap_id, 2u> kFractureYParamIds {{
    kFractureVoidParamId, kFractureSpaceParamId,
}};
constexpr std::array<clap_id, 2u> kFractureLatchParamIds {{
    kFractureDensityLatchParamId, kFractureShapeLatchParamId,
}};
constexpr std::array<clap_id, 2u> kFractureMotionXParamIds {{
    kFractureDensityMotionXParamId, kFractureShapeMotionXParamId,
}};
constexpr std::array<clap_id, 2u> kFractureMotionYParamIds {{
    kFractureDensityMotionYParamId, kFractureShapeMotionYParamId,
}};
constexpr std::array<clap_id, 2u> kFractureMotionEnergyParamIds {{
    kFractureDensityEnergyParamId, kFractureShapeEnergyParamId,
}};
constexpr uint32_t kFractureTrailCapacity = 18u;

struct ObjectUiRow {
    clap_id base;
    const char* label;
    uint32_t row;
};

constexpr std::array<ObjectUiRow, 6u> kObjectRows {{
    { kObjectSizeParamBase, "SIZE", 0u },
    { kObjectDecayParamBase, "DECAY", 1u },
    { kObjectHardnessParamBase, "HARDNESS", 2u },
    { kObjectSensitivityParamBase, "SENSITIVITY", 3u },
    { kObjectDriveParamBase, "DRIVE", 4u },
    { kCellLevelParamBase, "LEVEL", 5u },
}};

double uiNormalizedValue(clap_id id, double value)
{
    if (const auto* def = baseParamDef(id)) {
        return std::clamp((value - def->minimum)
            / std::max(1.0e-12, def->maximum - def->minimum), 0.0, 1.0);
    }
    if (isMatrixParam(id)) return std::clamp((value + 1.0) * 0.5, 0.0, 1.0);
    return std::clamp(value, 0.0, 1.0);
}

NSString* fissureUiValueText(clap_id id, double value,
    const char* fallback)
{
    if (id == kPressureParamId || id == kMassParamId
        || id == kMemoryParamId || id == kBodyParamId) {
        const float energy = s3g::processorFissureLowEnergyCurve(
            static_cast<float>(value));
        return [NSString stringWithFormat:@"E %.1f%%", energy * 100.0f];
    }
    if (id == kEdgeParamId) {
        return [NSString stringWithFormat:@"HARD %.0f%%", value * 100.0];
    }
    if (id == kRateParamId) {
        const float rate = s3g::processorFissureEventRateHz(
            static_cast<float>(value));
        if (rate < 0.01f) {
            return [NSString stringWithFormat:@"DRONE %.0fs",
                1.0f / std::max(rate, 1.0e-6f)];
        }
        if (rate < 1.0f) {
            return [NSString stringWithFormat:@"%.2f/s ×8", rate];
        }
        return [NSString stringWithFormat:@"%.0f/s ×8", rate];
    }
    if (id == kVoidParamId) {
        const float chance = s3g::processorFissureFormCurve(
            static_cast<float>(value));
        return [NSString stringWithFormat:@"%.0f%% / EVENT",
            chance * 100.0f];
    }
    if (id == kSpaceParamId) {
        const float seconds = s3g::processorFissureSpaceSeconds(
            static_cast<float>(value));
        if (seconds < 1.0f) {
            return [NSString stringWithFormat:@"GAP %.0fms",
                seconds * 1000.0f];
        }
        return [NSString stringWithFormat:@"GAP %.1fs", seconds];
    }
    return [NSString stringWithUTF8String:fallback];
}

double uiValueFromNormalized(clap_id id, double normalized)
{
    normalized = std::clamp(normalized, 0.0, 1.0);
    if (const auto* def = baseParamDef(id)) {
        return clampParamValue(id,
            def->minimum + (def->maximum - def->minimum) * normalized);
    }
    if (isMatrixParam(id)) return normalized * 2.0 - 1.0;
    return normalized;
}

NSRect presetDropdownRect()
{
    const auto band = s3g::clap_gui::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    const uint32_t rows = s3g::clap_gui::multiColumnMenuRows(
        kFactoryPresetCount, kPresetMenuColumns);
    return NSMakeRect(band.presetMenu.x, 32.0, 344.0,
        rows * kPresetMenuItemHeight);
}

NSRect matrixCellRect(uint32_t destination, uint32_t source)
{
    return NSMakeRect(kMatrixX + source * kMatrixCell,
        kMatrixY + destination * kMatrixCell,
        kMatrixCell - 3.0, kMatrixCell - 3.0);
}

NSRect cutMaskButtonRect(uint32_t cell)
{
    return NSMakeRect(kMatrixX + cell * kMatrixCell,
        538.0, kMatrixCell - 3.0, 20.0);
}

NSRect topologyButtonRect(uint32_t index)
{
    return NSMakeRect(kMatrixPanel.frame.x + 16.0 + index * 98.0,
        562.0, 90.0, 25.0);
}

NSRect topologyMixRect()
{
    return NSMakeRect(kMatrixPanel.frame.x + 112.0, 602.0, 282.0, 24.0);
}

NSRect routeSliderRect()
{
    return NSMakeRect(kMatrixPanel.frame.x + 112.0, 641.0, 284.0, 26.0);
}

NSRect routePolarityRect()
{
    return NSMakeRect(420.0, 641.0, 82.0, 26.0);
}

NSRect routeClearRect()
{
    return NSMakeRect(514.0, 641.0, 82.0, 26.0);
}

NSRect outputModeButtonRect(uint32_t index)
{
    static constexpr std::array<CGFloat, 3u> widths {{ 62.0, 62.0, 84.0 }};
    CGFloat x = kOutputPanel.frame.x + 112.0;
    for (uint32_t mode = 0u; mode < index; ++mode) x += widths[mode] + 7.0;
    return NSMakeRect(x, 96.0, widths[index], 20.0);
}

NSRect sceneButtonRect(uint32_t index)
{
    return NSMakeRect(kScenePanel.frame.x + 14.0 + index * 40.0,
        76.0, 34.0, 25.0);
}

NSRect storeButtonRect() { return NSMakeRect(1176.0, 76.0, 54.0, 25.0); }
NSRect holdButtonRect() { return NSMakeRect(1238.0, 76.0, 45.0, 25.0); }
NSRect runButtonRect() { return NSMakeRect(1291.0, 76.0, 39.0, 25.0); }

NSRect sceneMorphRect()
{
    return NSMakeRect(kScenePanel.frame.x + 76.0, 121.0, 250.0, 24.0);
}

NSRect cellButtonRect(uint32_t index)
{
    return NSMakeRect(kObjectPanel.frame.x + 14.0 + index * 40.0,
        230.0, 33.0, 28.0);
}

NSRect actionButtonRect(uint32_t index)
{
    static constexpr std::array<NSRect, 5u> rects {{
        {{ 1014.0, 680.0 }, { 145.0, 27.0 }},
        {{ 1167.0, 680.0 }, { 163.0, 27.0 }},
        {{ 1014.0, 716.0 }, { 96.0, 27.0 }},
        {{ 1118.0, 716.0 }, { 132.0, 27.0 }},
        {{ 1258.0, 716.0 }, { 72.0, 27.0 }},
    }};
    return rects[index];
}

NSRect fracturePadRect(uint32_t index)
{
    return NSMakeRect(660.0 + static_cast<CGFloat>(index) * 108.0,
        580.0, 100.0, 132.0);
}

NSRect fractureLatchButtonRect(uint32_t index)
{
    return NSMakeRect(886.0,
        714.0 + static_cast<CGFloat>(index) * 24.0, 86.0, 20.0);
}

NSRect grabButtonRect() { return NSMakeRect(886.0, 580.0, 86.0, 30.0); }
NSRect repeatButtonRect() { return NSMakeRect(886.0, 620.0, 86.0, 42.0); }

float meterNormalized(float peak)
{
    const float db = 20.0f * std::log10(std::max(1.0e-6f, peak));
    return std::clamp((db + 60.0f) / 66.0f, 0.0f, 1.0f);
}

void drawMeterBar(NSRect rect, float amount, NSColor* fill)
{
    amount = std::clamp(amount, 0.0f, 1.0f);
    [s3g::clap_gui::color(0x171717) setFill];
    NSRectFill(rect);
    [s3g::clap_gui::color(0x4a4a4a) setStroke];
    NSFrameRect(rect);
    [fill setFill];
    NSRectFill(NSMakeRect(rect.origin.x + 2.0, rect.origin.y + 2.0,
        std::max<CGFloat>(0.0, (rect.size.width - 4.0) * amount),
        rect.size.height - 4.0));
}

} // namespace

@interface S3GProcessorFissureView : NSView {
    void* _plugin;
    int _dragParam;
    uint32_t _selectedSource;
    uint32_t _selectedDestination;
    bool _matrixPainting;
    double _matrixPaintValue;
    int _lastPaintedRoute;
    bool _cutMaskPainting;
    double _cutMaskPaintValue;
    int _lastPaintedMask;
    int _fracturePad;
    NSPoint _fractureTrail[2][kFractureTrailCapacity];
    float _fractureTrailAlpha[2][kFractureTrailCapacity];
    uint32_t _fractureTrailCount[2];
    double _fractureLastX[2];
    double _fractureLastY[2];
    double _fractureLastTime[2];
    bool _fractureMotionReady[2];
    bool _repeatPressed;
    bool _presetMenuOpen;
    int _hoverPreset;
    NSTrackingArea* _trackingArea;
    NSTimer* _timer;
    char _presetName[64];
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)updateDraggedParam:(NSPoint)point;
- (void)paintMatrixAt:(NSPoint)point;
- (void)paintCutMaskAt:(NSPoint)point;
- (void)updateFracturePad:(NSPoint)point;
@end

@implementation S3GProcessorFissureView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragParam = -1;
        _selectedSource = 0u;
        _selectedDestination = 0u;
        _matrixPainting = false;
        _matrixPaintValue = 0.0;
        _lastPaintedRoute = -1;
        _cutMaskPainting = false;
        _cutMaskPaintValue = 1.0;
        _lastPaintedMask = -1;
        _fracturePad = -1;
        for (uint32_t pad = 0u; pad < 2u; ++pad) {
            _fractureTrailCount[pad] = 0u;
            _fractureLastX[pad] = 0.0;
            _fractureLastY[pad] = 0.0;
            _fractureLastTime[pad] = 0.0;
            _fractureMotionReady[pad] = false;
            for (uint32_t point = 0u; point < kFractureTrailCapacity;
                 ++point) {
                _fractureTrail[pad][point] = NSZeroPoint;
                _fractureTrailAlpha[pad][point] = 0.0f;
            }
        }
        _repeatPressed = false;
        _presetMenuOpen = false;
        _hoverPreset = -1;
        _trackingArea = nil;
        _timer = nil;
        auto* p = static_cast<Plugin*>(plugin);
        const uint32_t preset = p ? p->presetIndex : 0u;
        std::snprintf(_presetName, sizeof(_presetName), "%s",
            factoryPresetName(preset));
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)dealloc
{
    [self stopRefreshTimer];
    if (_trackingArea) [self removeTrackingArea:_trackingArea];
    [super dealloc];
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (_trackingArea) [self removeTrackingArea:_trackingArea];
    _trackingArea = [[NSTrackingArea alloc] initWithRect:[self bounds]
        options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited
            | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
        owner:self userInfo:nil];
    [self addTrackingArea:_trackingArea];
    [_trackingArea release];
}

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 24.0
        target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (!_timer) return;
    [_timer invalidate];
    _timer = nil;
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    for (uint32_t pad = 0u; pad < 2u; ++pad) {
        for (uint32_t point = 0u; point < _fractureTrailCount[pad];
             ++point) {
            _fractureTrailAlpha[pad][point] *= 0.88f;
        }
    }
    if (![self isHidden] && _plugin
        && s3g::clap_support::hostAppIsActive()) {
        [self setNeedsDisplay:YES];
    }
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    NSDictionary* tiny = s3g::clap_gui::textAttrs(
        s3g::clap_gui::color(0x969696), 8.0);
    const uint32_t presetIndex = static_cast<uint32_t>(std::clamp(
        std::round(paramValue(*p, kPresetParamId)), 0.0,
        static_cast<double>(kCustomPresetIndex)));
    if (presetIndex < kFactoryPresetCount) {
        std::snprintf(_presetName, sizeof(_presetName), "%s",
            factoryPresetName(presetIndex));
    } else if (std::strcmp(_presetName, "INIT / PLATE RING") == 0) {
        std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
    }
    const auto titleBand = s3g::clap_gui::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    s3g::clap_gui::drawProcessorTitleBand(
        @"s3g PROCESSOR FISSURE",
        [NSString stringWithUTF8String:_presetName],
        s3g::clap_gui::peakDbText(
            p->outputPeak.load(std::memory_order_relaxed)),
        titleBand, s3g::clap_gui::softTitleAttrs(), labels, values, style);

    const auto drawPanel = [&](NSString* name,
                               const s3g::gui_layout::Panel& panel) {
        s3g::clap_gui::drawPanelFrame(panel, style);
        s3g::clap_gui::drawPanelHeader(name, true, panel, labels, style);
    };
    drawPanel(@"MATRIX FIELD  ·  MIC → ENERGY / PITCH → OBJECTS → ROUTES",
        kMatrixPanel);
    drawPanel(@"OUTPUT", kOutputPanel);
    drawPanel(@"EXTERNAL MIC / CONTACT", kInputPanel);
    drawPanel(@"ATTACHED OBJECT IDENTITIES", kExciterPanel);
    drawPanel(@"FIELD ENERGY  ·  EXPANDED LOW RANGE", kMaterialPanel);
    drawPanel(@"SCENES", kScenePanel);
    drawPanel(@"PHYSICAL OBJECT  ·  SELECT + STRIKE", kObjectPanel);
    drawPanel(@"FORM / INTERRUPTION  ·  FOUR AXES", kFormPanel);
    drawPanel(@"PERFORMANCE GESTURES", kGesturePanel);
    drawPanel(@"FRACTURE  ·  CUT DENSITY / RUPTURE SHAPE", kFracturePanel);

    for (const auto& row : kUiRows) {
        const double value = paramValue(*p, row.id);
        char text[64] {};
        paramsValueToText(&p->plugin, row.id, value, text, sizeof(text));
        s3g::clap_gui::drawProcessorSlider(
            [NSString stringWithUTF8String:row.label],
            fissureUiValueText(row.id, value, text),
            uiNormalizedValue(row.id, value),
            s3g::gui_layout::rowY(*row.panel, row.row),
            row.panel->frame.x, row.panel->frame.width,
            labels, values, style);
    }

    [@"MODE" drawAtPoint:NSMakePoint(kOutputPanel.frame.x + 16.0, 99.0)
        withAttributes:labels];
    static constexpr const char* modeLabels[] {
        "STEREO", "QUAD", "8 DIRECT"
    };
    const uint32_t outputMode = static_cast<uint32_t>(std::clamp(
        std::round(paramValue(*p, kOutputModeParamId)), 0.0, 2.0));
    for (uint32_t mode = 0u; mode < 3u; ++mode) {
        s3g::clap_gui::drawHeaderButton(outputModeButtonRect(mode),
            s3g::clap_gui::cocoaRect(kOutputPanel.frame),
            [NSString stringWithUTF8String:modeLabels[mode]],
            outputMode == mode, values, style);
    }

    const float inputPeak = p->inputActivity.load(std::memory_order_relaxed);
    const float inputPeakMarker = p->inputPeakActivity.load(
        std::memory_order_relaxed);
    const float transfer = p->inputTransferActivity.load(
        std::memory_order_relaxed);
    const float pitchHz = p->detectedPitchHz.load(
        std::memory_order_relaxed);
    const float pitchConfidence = p->pitchConfidence.load(
        std::memory_order_relaxed);
    const float sustainedPitch = p->sustainedPitchDrive.load(
        std::memory_order_relaxed);
    const float governor = std::clamp(
        p->minimumGovernor.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const NSRect micMeter = NSMakeRect(114.0, 76.0, 482.0, 14.0);
    const NSRect contactMeter = NSMakeRect(114.0, 108.0, 214.0, 14.0);
    [@"MIC POST IN · RMS" drawAtPoint:NSMakePoint(28.0, 76.0)
        withAttributes:labels];
    drawMeterBar(micMeter, meterNormalized(inputPeak),
        inputPeak > .50f
            ? [NSColor colorWithCalibratedRed:.76 green:.34 blue:.16 alpha:.92]
            : s3g::clap_gui::color(0x8a8a8a));
    const CGFloat peakMarkerX = micMeter.origin.x + 2.0
        + (micMeter.size.width - 4.0) * meterNormalized(inputPeakMarker);
    [s3g::clap_gui::color(0xd4d4d4) setFill];
    NSRectFill(NSMakeRect(peakMarkerX, micMeter.origin.y + 1.0,
        1.0, micMeter.size.height - 2.0));
    [[NSString stringWithFormat:@"%+.1f dB",
        20.0f * std::log10(std::max(1.0e-6f, inputPeak))]
        drawAtPoint:NSMakePoint(526.0, 77.0) withAttributes:tiny];
    [@"ECOLOGY XFER" drawAtPoint:NSMakePoint(28.0, 108.0)
        withAttributes:labels];
    drawMeterBar(contactMeter, transfer,
        [NSColor colorWithCalibratedRed:.20 green:.62 blue:.72 alpha:.92]);
    [[NSString stringWithFormat:@"%.0f%%", transfer * 100.0f]
        drawAtPoint:NSMakePoint(294.0, 109.0) withAttributes:tiny];
    [s3g::clap_gui::color(0x4a4a4a) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(332.0, 115.0)
        toPoint:NSMakePoint(352.0, 115.0)];
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        const float cellActivity = std::clamp(
            p->activity[cell].load(std::memory_order_relaxed), 0.0f, 1.0f);
        const float sensitivity = static_cast<float>(paramValue(*p,
            kObjectSensitivityParamBase + cell));
        const float objectTransfer = std::clamp(transfer * sensitivity
            * (.48f + cellActivity * .82f), 0.0f, 1.0f);
        const NSRect response = NSMakeRect(356.0 + cell * 30.0,
            103.0, 23.0, 24.0);
        [[NSColor colorWithCalibratedRed:.08 + objectTransfer * .14
            green:.10 + objectTransfer * .42 blue:.11 + objectTransfer * .51
            alpha:1.0] setFill];
        NSRectFill(response);
        [s3g::clap_gui::color(0x515151) setStroke];
        NSFrameRect(response);
        [[NSString stringWithFormat:@"%u", cell + 1u]
            drawAtPoint:NSMakePoint(response.origin.x + 8.0,
                response.origin.y + 7.0) withAttributes:tiny];
    }
    [@"INPUT PRESENT" drawAtPoint:NSMakePoint(28.0, 136.0)
        withAttributes:tiny];
    [@"ENERGY / HELD PITCH" drawAtPoint:NSMakePoint(114.0, 136.0)
        withAttributes:tiny];
    NSString* pitchText = pitchConfidence > .08f
        ? [NSString stringWithFormat:@"F0 %.0f · LOCK %.0f · HOLD %.0f",
            pitchHz, pitchConfidence * 100.0f, sustainedPitch * 100.0f]
        : [NSString stringWithFormat:@"F0 — · LOCK %.0f · HOLD %.0f",
            pitchConfidence * 100.0f, sustainedPitch * 100.0f];
    [pitchText drawAtPoint:NSMakePoint(248.0, 136.0)
        withAttributes:tiny];
    [@"OBJECT RESPONSE 1—8" drawAtPoint:NSMakePoint(428.0, 136.0)
        withAttributes:tiny];

    [@"SOURCE" drawAtPoint:NSMakePoint(kMatrixX + 142.0,
        kMatrixY - 37.0) withAttributes:labels];
    [@"DEST" drawAtPoint:NSMakePoint(kMatrixX - 58.0,
        kMatrixY + 163.0) withAttributes:labels];
    for (uint32_t source = 0u; source < 8u; ++source) {
        [[NSString stringWithFormat:@"S%u", source + 1u]
            drawAtPoint:NSMakePoint(kMatrixX + source * kMatrixCell + 13.0,
                kMatrixY - 18.0) withAttributes:tiny];
    }
    for (uint32_t destination = 0u; destination < 8u; ++destination) {
        [[NSString stringWithFormat:@"D%u", destination + 1u]
            drawAtPoint:NSMakePoint(kMatrixX - 28.0,
                kMatrixY + destination * kMatrixCell + 14.0)
            withAttributes:tiny];
        const float destinationActivity = std::clamp(
            p->activity[destination].load(std::memory_order_relaxed),
            0.0f, 1.0f);
        for (uint32_t source = 0u; source < 8u; ++source) {
            const double route = paramValue(*p,
                kMatrixParamBase + destination * 8u + source);
            const double magnitude = std::abs(route);
            const NSRect rect = matrixCellRect(destination, source);
            [[NSColor colorWithCalibratedWhite:
                .070 + magnitude * .055 + destinationActivity * .035
                alpha:1.0] setFill];
            NSRectFill(rect);
            if (magnitude > .002) {
                NSColor* routeColor = route >= 0.0
                    ? [NSColor colorWithCalibratedRed:.79 green:.36
                        blue:.17 alpha:.26 + magnitude * .70]
                    : [NSColor colorWithCalibratedRed:.15 green:.55
                        blue:.69 alpha:.26 + magnitude * .70];
                [routeColor setFill];
                const CGFloat inset = 4.0 + (1.0 - magnitude) * 9.0;
                NSRectFill(NSInsetRect(rect, inset, inset));
            }
            const bool selected = source == _selectedSource
                && destination == _selectedDestination;
            [s3g::clap_gui::color(selected ? 0xd8d8d8 : 0x343434)
                setStroke];
            NSFrameRectWithWidth(rect, selected ? 2.0 : .7);
            [[NSString stringWithFormat:@"%c%02.0f",
                route < 0.0 ? '-' : '+', magnitude * 99.0]
                drawAtPoint:NSMakePoint(rect.origin.x + 9.0,
                    rect.origin.y + 15.0) withAttributes:tiny];
        }
        const float cutPulse = std::clamp(
            p->cutActivity[destination].load(std::memory_order_relaxed),
            0.0f, 1.0f);
        if (cutPulse > .015f) {
            const bool negative = p->cutPolarity[destination].load(
                std::memory_order_relaxed) < 0.0f;
            NSColor* cutColor = negative
                ? [NSColor colorWithCalibratedRed:.15 green:.63 blue:.82
                    alpha:.18 + cutPulse * .76]
                : [NSColor colorWithCalibratedRed:.92 green:.37 blue:.12
                    alpha:.18 + cutPulse * .76];
            [cutColor setStroke];
            NSFrameRectWithWidth(NSMakeRect(kMatrixX - 4.0,
                kMatrixY + destination * kMatrixCell - 4.0,
                kMatrixCell * 8.0 + 5.0, kMatrixCell + 4.0),
                1.0 + cutPulse * 2.0);
        }
    }

    [@"CUT MASK" drawAtPoint:NSMakePoint(78.0, 543.0)
        withAttributes:tiny];
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        const NSRect maskRect = cutMaskButtonRect(cell);
        const bool enabled = paramValue(*p, kCutMaskParamBase + cell) >= .5;
        const float pulse = std::clamp(
            p->cutActivity[cell].load(std::memory_order_relaxed), 0.0f, 1.0f);
        const bool negative = p->cutPolarity[cell].load(
            std::memory_order_relaxed) < 0.0f;
        const float age = std::clamp(p->cutFragmentAge[cell].load(
            std::memory_order_relaxed), 0.0f, 1.0f);
        [[NSColor colorWithCalibratedRed:
                enabled ? .18 + pulse * .48 : .07
            green:enabled ? .13 + pulse * (negative ? .31 : .12) : .07
            blue:enabled ? .11 + pulse * (negative ? .48 : .06) : .07
            alpha:1.0] setFill];
        NSRectFill(maskRect);
        [s3g::clap_gui::color(enabled ? 0x858585 : 0x303030) setStroke];
        NSFrameRect(maskRect);
        [s3g::clap_gui::color(negative ? 0x45a9c7 : 0xd06a35) setFill];
        NSRectFill(NSMakeRect(maskRect.origin.x + 2.0,
            NSMaxY(maskRect) - 3.0,
            (maskRect.size.width - 4.0) * age, 2.0));
        [[NSString stringWithFormat:@"%u%c", cell + 1u,
            pulse > .08f ? (negative ? '-' : '+') : ' ']
            drawAtPoint:NSMakePoint(maskRect.origin.x + 12.0,
                maskRect.origin.y + 5.0) withAttributes:tiny];
    }

    static constexpr const char* topologyLabels[] {
        "ISLANDS", "RING", "PAIRS", "HUB", "CLUSTERS", "SCATTER"
    };
    const uint32_t topology = static_cast<uint32_t>(std::clamp(
        std::round(paramValue(*p, kTopologyShapeParamId)), 0.0, 5.0));
    for (uint32_t shape = 0u; shape < 6u; ++shape) {
        s3g::clap_gui::drawHeaderButton(topologyButtonRect(shape),
            s3g::clap_gui::cocoaRect(kMatrixPanel.frame),
            [NSString stringWithUTF8String:topologyLabels[shape]],
            topology == shape, values, style);
    }
    [@"STRUCTURE" drawAtPoint:NSMakePoint(28.0, 608.0)
        withAttributes:labels];
    const NSRect topologyTrack = topologyMixRect();
    drawMeterBar(topologyTrack,
        static_cast<float>(paramValue(*p, kTopologyMixParamId)),
        s3g::clap_gui::color(0x686868));
    [[NSString stringWithFormat:@"BLEND %.0f%% · CHOOSE SHAPE TO APPLY",
        paramValue(*p, kTopologyMixParamId) * 100.0]
        drawAtPoint:NSMakePoint(410.0, 608.0) withAttributes:tiny];

    const clap_id routeId = kMatrixParamBase
        + _selectedDestination * 8u + _selectedSource;
    const double routeValue = paramValue(*p, routeId);
    [@"ROUTE" drawAtPoint:NSMakePoint(28.0, 648.0)
        withAttributes:labels];
    [[NSString stringWithFormat:@"D%u ← S%u",
        _selectedDestination + 1u, _selectedSource + 1u]
        drawAtPoint:NSMakePoint(68.0, 648.0) withAttributes:tiny];
    const NSRect routeTrack = routeSliderRect();
    [s3g::clap_gui::color(0x171717) setFill];
    NSRectFill(routeTrack);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(routeTrack);
    const CGFloat routeCenter = NSMidX(routeTrack);
    [s3g::clap_gui::color(0x707070) setStroke];
    NSFrameRect(NSMakeRect(routeCenter, routeTrack.origin.y, 1.0,
        routeTrack.size.height));
    const CGFloat routeX = routeTrack.origin.x
        + uiNormalizedValue(routeId, routeValue) * routeTrack.size.width;
    [s3g::clap_gui::color(routeValue >= 0.0 ? 0xc26335 : 0x398da8)
        setFill];
    NSRectFill(NSMakeRect(routeX - 3.0, routeTrack.origin.y + 3.0,
        6.0, routeTrack.size.height - 6.0));
    s3g::clap_gui::drawHeaderButton(routePolarityRect(),
        s3g::clap_gui::cocoaRect(kMatrixPanel.frame), @"FLIP ±",
        false, values, style);
    s3g::clap_gui::drawHeaderButton(routeClearRect(),
        s3g::clap_gui::cocoaRect(kMatrixPanel.frame), @"CLEAR",
        false, values, style);
    [@"CELL CLICK CYCLES OFF → +34 → −34 · DRAG PAINTS · ROUTE BELOW IS PRECISE"
        drawAtPoint:NSMakePoint(74.0, 686.0) withAttributes:tiny];
    [[NSString stringWithFormat:@"SAFETY GOVERNOR %.0f%%",
        governor * 100.0f] drawAtPoint:NSMakePoint(238.0, 716.0)
        withAttributes:labels];

    const uint32_t selectedScene = static_cast<uint32_t>(std::clamp(
        std::round(paramValue(*p, kSceneParamId)), 1.0, 4.0));
    for (uint32_t scene = 0u; scene < 4u; ++scene) {
        s3g::clap_gui::drawHeaderButton(sceneButtonRect(scene),
            s3g::clap_gui::cocoaRect(kScenePanel.frame),
            [NSString stringWithFormat:@"%c", 'A' + scene],
            selectedScene == scene + 1u, values, style);
    }
    s3g::clap_gui::drawHeaderButton(storeButtonRect(),
        s3g::clap_gui::cocoaRect(kScenePanel.frame), @"STORE",
        false, values, style);
    s3g::clap_gui::drawHeaderButton(holdButtonRect(),
        s3g::clap_gui::cocoaRect(kScenePanel.frame), @"HOLD",
        paramValue(*p, kHoldParamId) >= .5, values, style);
    s3g::clap_gui::drawHeaderButton(runButtonRect(),
        s3g::clap_gui::cocoaRect(kScenePanel.frame), @"RUN",
        paramValue(*p, kRunParamId) >= .5, values, style);
    [@"MORPH" drawAtPoint:NSMakePoint(kScenePanel.frame.x + 14.0, 127.0)
        withAttributes:labels];
    const NSRect morphRect = sceneMorphRect();
    drawMeterBar(morphRect,
        static_cast<float>(uiNormalizedValue(kSceneMorphParamId,
            paramValue(*p, kSceneMorphParamId))),
        s3g::clap_gui::color(0x686868));
    for (uint32_t scene = 0u; scene < 4u; ++scene) {
        const CGFloat x = morphRect.origin.x
            + morphRect.size.width * scene / 3.0;
        [s3g::clap_gui::color(0x8a8a8a) setStroke];
        NSFrameRect(NSMakeRect(x, morphRect.origin.y, 1.0,
            morphRect.size.height));
    }

    const uint32_t selectedCell = static_cast<uint32_t>(std::clamp(
        std::round(paramValue(*p, kSelectedCellParamId)), 1.0, 8.0)) - 1u;
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        const float activity = std::clamp(
            p->activity[cell].load(std::memory_order_relaxed), 0.0f, 1.0f);
        const NSRect rect = cellButtonRect(cell);
        [[NSColor colorWithCalibratedRed:.12 + activity * .50
            green:.11 + activity * .26 blue:.13 + activity * .08
            alpha:1.0] setFill];
        NSRectFill(rect);
        [s3g::clap_gui::color(selectedCell == cell ? 0xd0d0d0 : 0x555555)
            setStroke];
        NSFrameRectWithWidth(rect, selectedCell == cell ? 2.0 : 1.0);
        [[NSString stringWithFormat:@"%u", cell + 1u]
            drawAtPoint:NSMakePoint(rect.origin.x + 13.0,
                rect.origin.y + 8.0) withAttributes:tiny];
    }
    for (const auto& row : kObjectRows) {
        const clap_id id = row.base + selectedCell;
        const double value = paramValue(*p, id);
        char text[64] {};
        paramsValueToText(&p->plugin, id, value, text, sizeof(text));
        s3g::clap_gui::drawProcessorSlider(
            [NSString stringWithUTF8String:row.label],
            [NSString stringWithUTF8String:text], value,
            s3g::gui_layout::rowY(kObjectPanel, row.row),
            kObjectPanel.frame.x, kObjectPanel.frame.width,
            labels, values, style);
    }

    const double variation = paramValue(*p, kVariationParamId);
    const float performedVariation = s3g::processorFissureFormCurve(
        static_cast<float>(variation));
    s3g::clap_gui::drawProcessorSlider(@"CUT CONTRAST",
        [NSString stringWithFormat:@"Δ %.0f%%",
            performedVariation * 100.0f],
        variation, s3g::gui_layout::rowY(kGesturePanel, 0u),
        kGesturePanel.frame.x, kGesturePanel.frame.width,
        labels, values, style);
    static constexpr const char* actionLabels[] {
        "CUT + LINKS", "IMPACT ALL", "DROP OUT", "MUTATE OBJECT", "PANIC"
    };
    for (uint32_t action = 0u; action < 5u; ++action) {
        s3g::clap_gui::drawHeaderButton(actionButtonRect(action),
            s3g::clap_gui::cocoaRect(kGesturePanel.frame),
            [NSString stringWithUTF8String:actionLabels[action]],
            paramValue(*p, kCutParamId + action) >= .5, values, style);
    }

    for (uint32_t index = 0u; index < 2u; ++index) {
        const NSRect pad = fracturePadRect(index);
        [s3g::clap_gui::color(0x111111) setFill];
        NSRectFill(pad);
        [s3g::clap_gui::color(index == 0u ? 0x6f4a3a : 0x355b68)
            setStroke];
        NSFrameRect(pad);
        for (uint32_t division = 1u; division < 4u; ++division) {
            const CGFloat x = pad.origin.x
                + pad.size.width * division / 4.0;
            const CGFloat y = pad.origin.y
                + pad.size.height * division / 4.0;
            [s3g::clap_gui::color(0x292929) setStroke];
            [NSBezierPath strokeLineFromPoint:NSMakePoint(x, pad.origin.y)
                toPoint:NSMakePoint(x, NSMaxY(pad))];
            [NSBezierPath strokeLineFromPoint:NSMakePoint(pad.origin.x, y)
                toPoint:NSMakePoint(NSMaxX(pad), y)];
        }
        if (_fractureTrailCount[index] > 1u) {
            for (uint32_t item = 1u;
                 item < _fractureTrailCount[index]; ++item) {
                const NSPoint previous = _fractureTrail[index][item - 1u];
                const NSPoint current = _fractureTrail[index][item];
                const NSPoint from = NSMakePoint(
                    pad.origin.x + 6.0
                        + previous.x * (pad.size.width - 12.0),
                    NSMaxY(pad) - 6.0
                        - previous.y * (pad.size.height - 12.0));
                const NSPoint to = NSMakePoint(
                    pad.origin.x + 6.0
                        + current.x * (pad.size.width - 12.0),
                    NSMaxY(pad) - 6.0
                        - current.y * (pad.size.height - 12.0));
                const float trailAlpha = std::clamp(std::min(
                    _fractureTrailAlpha[index][item - 1u],
                    _fractureTrailAlpha[index][item]), 0.0f, 1.0f);
                [[NSColor colorWithCalibratedRed:(index == 0u ? .92 : .28)
                    green:(index == 0u ? .42 : .70)
                    blue:(index == 0u ? .16 : .88)
                    alpha:trailAlpha * .76] setStroke];
                [NSBezierPath strokeLineFromPoint:from toPoint:to];
            }
        }

        // Eight small telemetry cells show causal traversal (outline),
        // realized splices (bright cap), and current hole occupancy (fill).
        for (uint32_t cell = 0u; cell < 8u; ++cell) {
            const float affected = p->fractureGestureActivity[index][cell]
                .load(std::memory_order_relaxed);
            const float gap = p->gapActivity[cell].load(
                std::memory_order_relaxed);
            const float cut = p->cutActivity[cell].load(
                std::memory_order_relaxed);
            const NSRect marker = NSMakeRect(pad.origin.x + 4.0
                    + static_cast<CGFloat>(cell) * 11.5,
                pad.origin.y + 4.0, 9.0, 7.0);
            [[NSColor colorWithCalibratedRed:.08 + gap * .10
                green:.08 + gap * .23 blue:.08 + gap * .30 alpha:1.0]
                setFill];
            NSRectFill(marker);
            [[NSColor colorWithCalibratedRed:(index == 0u ? .86 : .22)
                green:(index == 0u ? .34 : .62)
                blue:(index == 0u ? .12 : .82)
                alpha:.16 + affected * .84] setStroke];
            NSFrameRect(marker);
            if (cut > .04f) {
                [[NSColor colorWithCalibratedWhite:.88
                    alpha:std::min(1.0f, cut)] setFill];
                NSRectFill(NSMakeRect(marker.origin.x + 1.0,
                    marker.origin.y + 1.0,
                    (marker.size.width - 2.0) * cut, 1.5));
            }
        }
        const double xValue = paramValue(*p, kFractureXParamIds[index]);
        const double yValue = paramValue(*p, kFractureYParamIds[index]);
        const NSPoint point = NSMakePoint(
            pad.origin.x + 6.0 + xValue * (pad.size.width - 12.0),
            NSMaxY(pad) - 6.0 - yValue * (pad.size.height - 12.0));
        const float gestureEnergy = p->fractureGestureEnergy[index].load(
            std::memory_order_relaxed);
        const CGFloat halo = 10.0 + gestureEnergy * 7.0;
        [[NSColor colorWithCalibratedRed:(index == 0u ? .86 : .20)
            green:(index == 0u ? .31 : .62)
            blue:(index == 0u ? .12 : .78)
            alpha:.26 + gestureEnergy * .54] setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            point.x - halo, point.y - halo, halo * 2.0, halo * 2.0)] fill];
        [s3g::clap_gui::color(index == 0u ? 0xe0a07d : 0x8ac9dc)
            setStroke];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            point.x - 4.0, point.y - 4.0, 8.0, 8.0)] stroke];
    }
    [@"CUT · RATE →" drawAtPoint:NSMakePoint(664.0, 716.0)
        withAttributes:tiny];
    [@"VOID ↑" drawAtPoint:NSMakePoint(662.0, 566.0)
        withAttributes:tiny];
    [@"SHAPE · EDGE →" drawAtPoint:NSMakePoint(772.0, 716.0)
        withAttributes:tiny];
    [@"SPACE ↑" drawAtPoint:NSMakePoint(770.0, 566.0)
        withAttributes:tiny];
    static constexpr const char* latchLabels[] {
        "CUT LATCH", "SHAPE LATCH"
    };
    for (uint32_t index = 0u; index < 2u; ++index) {
        s3g::clap_gui::drawHeaderButton(fractureLatchButtonRect(index),
            s3g::clap_gui::cocoaRect(kFracturePanel.frame),
            [NSString stringWithUTF8String:latchLabels[index]],
            paramValue(*p, kFractureLatchParamIds[index]) >= .5,
            values, style);
    }
    const bool grabbing = p->grabbingStatus.load(std::memory_order_relaxed);
    const bool grabbed = p->grabbed.load(std::memory_order_relaxed);
    const float grabDuration = p->grabDuration.load(
        std::memory_order_relaxed);
    s3g::clap_gui::drawHeaderButton(grabButtonRect(),
        s3g::clap_gui::cocoaRect(kFracturePanel.frame),
        grabbing ? @"STOP GRAB" : @"GRAB", grabbing, values, style);
    s3g::clap_gui::drawHeaderButton(repeatButtonRect(),
        s3g::clap_gui::cocoaRect(kFracturePanel.frame), @"REPEAT",
        p->repeatMix.load(std::memory_order_relaxed) > .08f,
        values, style);
    NSString* loopStatus = grabbing
        ? [NSString stringWithFormat:@"REC %.1fs", grabDuration]
        : (grabbed ? [NSString stringWithFormat:@"LOOP %.1fs", grabDuration]
                   : @"LOOP EMPTY");
    [loopStatus
        drawAtPoint:NSMakePoint(888.0, 674.0) withAttributes:tiny];
    [[NSString stringWithFormat:@"RPT %02.0f%%",
        p->repeatPhase.load(std::memory_order_relaxed) * 100.0f]
        drawAtPoint:NSMakePoint(888.0, 690.0) withAttributes:tiny];
    uint32_t occupiedGaps = 0u;
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        if (p->gapActivity[cell].load(std::memory_order_relaxed) > .55f) {
            ++occupiedGaps;
        }
    }
    NSDictionary* gapAttrs = occupiedGaps >= 7u
        ? s3g::clap_gui::textAttrs(
            [NSColor colorWithCalibratedRed:.95 green:.31 blue:.16 alpha:1.0],
            8.0)
        : tiny;
    [[NSString stringWithFormat:occupiedGaps >= 7u
            ? @"SILENCE GUARD %u/8" : @"REALIZED GAPS %u/8",
        occupiedGaps] drawAtPoint:NSMakePoint(888.0, 700.0)
        withAttributes:gapAttrs];

    if (_presetMenuOpen) {
        NSString* presetItems[kFactoryPresetCount] {};
        for (uint32_t index = 0u; index < kFactoryPresetCount; ++index) {
            presetItems[index] = [NSString stringWithUTF8String:
                factoryPresetName(index)];
        }
        s3g::clap_gui::drawMultiColumnDropdownMenu(
            presetDropdownRect(), kPresetMenuItemHeight,
            presetItems, kFactoryPresetCount, kPresetMenuColumns,
            presetIndex < kFactoryPresetCount
                ? static_cast<int>(presetIndex) : -1,
            _hoverPreset, values, style);
    }
}

- (void)updateDraggedParam:(NSPoint)point
{
    if (_dragParam <= 0) return;
    const auto id = static_cast<clap_id>(_dragParam);
    CGFloat x = 0.0;
    CGFloat width = 0.0;
    const auto row = std::find_if(kUiRows.begin(), kUiRows.end(),
        [=](const FissureUiRow& candidate) { return candidate.id == id; });
    if (row != kUiRows.end()) {
        x = s3g::gui_layout::processorControlX(row->panel->frame.x);
        width = s3g::gui_layout::processorTrackWidth(row->panel->frame.width);
    } else if (id == kVariationParamId) {
        x = s3g::gui_layout::processorControlX(kGesturePanel.frame.x);
        width = s3g::gui_layout::processorTrackWidth(kGesturePanel.frame.width);
    } else if (id == kSceneMorphParamId) {
        const NSRect rect = sceneMorphRect();
        x = rect.origin.x;
        width = rect.size.width;
    } else if (id == kTopologyMixParamId) {
        const NSRect rect = topologyMixRect();
        x = rect.origin.x;
        width = rect.size.width;
    } else if (isMatrixParam(id)) {
        const NSRect rect = routeSliderRect();
        x = rect.origin.x;
        width = rect.size.width;
    } else if (isCellLevelParam(id) || isObjectParam(id)) {
        x = s3g::gui_layout::processorControlX(kObjectPanel.frame.x);
        width = s3g::gui_layout::processorTrackWidth(kObjectPanel.frame.width);
    } else {
        return;
    }
    const double normalized = std::clamp((point.x - x) / width, 0.0, 1.0);
    auto* p = static_cast<Plugin*>(_plugin);
    queueGuiParamValue(*p, id, uiValueFromNormalized(id, normalized));
    std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
    [self setNeedsDisplay:YES];
}

- (void)paintMatrixAt:(NSPoint)point
{
    if (!_matrixPainting) return;
    auto* p = static_cast<Plugin*>(_plugin);
    for (uint32_t destination = 0u; destination < 8u; ++destination) {
        for (uint32_t source = 0u; source < 8u; ++source) {
            if (!NSPointInRect(point, matrixCellRect(destination, source))) {
                continue;
            }
            const clap_id id = kMatrixParamBase + destination * 8u + source;
            if (_lastPaintedRoute == static_cast<int>(id)) return;
            _lastPaintedRoute = static_cast<int>(id);
            _selectedSource = source;
            _selectedDestination = destination;
            queueGuiParamGesture(*p, id, _matrixPaintValue);
            std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
            [self setNeedsDisplay:YES];
            return;
        }
    }
}

- (void)paintCutMaskAt:(NSPoint)point
{
    if (!_cutMaskPainting) return;
    auto* p = static_cast<Plugin*>(_plugin);
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        if (!NSPointInRect(point, cutMaskButtonRect(cell))) continue;
        const clap_id id = kCutMaskParamBase + cell;
        if (_lastPaintedMask == static_cast<int>(id)) return;
        _lastPaintedMask = static_cast<int>(id);
        queueGuiParamGesture(*p, id, _cutMaskPaintValue);
        std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
        [self setNeedsDisplay:YES];
        return;
    }
}

- (void)updateFracturePad:(NSPoint)point
{
    if (_fracturePad < 0 || _fracturePad > 1) return;
    auto* p = static_cast<Plugin*>(_plugin);
    const uint32_t index = static_cast<uint32_t>(_fracturePad);
    const NSRect rect = fracturePadRect(index);
    const double x = std::clamp((point.x - rect.origin.x)
        / rect.size.width, 0.0, 1.0);
    const double y = std::clamp((NSMaxY(rect) - point.y)
        / rect.size.height, 0.0, 1.0);
    const clap_id xId = kFractureXParamIds[index];
    const clap_id yId = kFractureYParamIds[index];
    queueGuiParamValue(*p, xId, x);
    queueGuiParamValue(*p, yId, y);

    const double now = [NSDate timeIntervalSinceReferenceDate];
    if (_fractureMotionReady[index]) {
        const double deltaX = x - _fractureLastX[index];
        const double deltaY = y - _fractureLastY[index];
        const double distance = std::sqrt(
            deltaX * deltaX + deltaY * deltaY);
        const double elapsed = std::clamp(
            now - _fractureLastTime[index], 1.0 / 240.0, 0.12);
        if (distance > 0.001) {
            if (_fractureTrailCount[index] == kFractureTrailCapacity) {
                for (uint32_t item = 1u;
                     item < kFractureTrailCapacity; ++item) {
                    _fractureTrail[index][item - 1u]
                        = _fractureTrail[index][item];
                    _fractureTrailAlpha[index][item - 1u]
                        = _fractureTrailAlpha[index][item];
                }
                --_fractureTrailCount[index];
            }
            const uint32_t trailIndex = _fractureTrailCount[index]++;
            _fractureTrail[index][trailIndex] = NSMakePoint(x, y);
            _fractureTrailAlpha[index][trailIndex] = 1.0f;

            const double speed = distance / elapsed;
            const double energy = std::clamp(
                (speed - 0.18) / 5.2, 0.0, 1.0);
            if (energy > 0.005) {
                queueGuiParamValue(*p, kFractureMotionXParamIds[index],
                    deltaX / distance);
                queueGuiParamValue(*p, kFractureMotionYParamIds[index],
                    deltaY / distance);
                // Energy is queued last: it commits the already-published
                // vector as one audio-thread gesture impulse.
                queueGuiParamValue(*p,
                    kFractureMotionEnergyParamIds[index], energy);
            }
        }
    }
    _fractureLastX[index] = x;
    _fractureLastY[index] = y;
    _fractureLastTime[index] = now;
    _fractureMotionReady[index] = true;
    [self setNeedsDisplay:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (!_presetMenuOpen) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    const int hover = s3g::clap_gui::multiColumnDropdownHitIndex(
        point, presetDropdownRect(), kPresetMenuItemHeight,
        kFactoryPresetCount, kPresetMenuColumns);
    if (hover != _hoverPreset) {
        _hoverPreset = hover;
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseExited:(NSEvent*)event
{
    (void)event;
    if (_hoverPreset != -1) {
        _hoverPreset = -1;
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:
        [event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    const auto titleBand = s3g::clap_gui::encoderTitleBand(
        kGuiWidth, kGuiHeight);

    if (_presetMenuOpen) {
        const int preset = s3g::clap_gui::multiColumnDropdownHitIndex(
            point, presetDropdownRect(), kPresetMenuItemHeight,
            kFactoryPresetCount, kPresetMenuColumns);
        _presetMenuOpen = false;
        _hoverPreset = -1;
        if (preset >= 0) {
            queueGuiParamGesture(*p, kPresetParamId, preset);
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                factoryPresetName(static_cast<uint32_t>(preset)));
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
            titleBand.presetMenu))) {
        _presetMenuOpen = !_presetMenuOpen;
        [self setNeedsDisplay:YES];
        return;
    }
    if (s3g::clap_gui::handleProcessorTitleClick(point, &p->plugin,
            @"Processor Fissure", titleBand,
            _presetName, sizeof(_presetName), kOutputParamId)) {
        [self setNeedsDisplay:YES];
        return;
    }

    for (uint32_t mode = 0u; mode < 3u; ++mode) {
        if (NSPointInRect(point, outputModeButtonRect(mode))) {
            queueGuiParamGesture(*p, kOutputModeParamId, mode);
            std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
            return;
        }
    }
    for (uint32_t scene = 0u; scene < 4u; ++scene) {
        if (NSPointInRect(point, sceneButtonRect(scene))) {
            queueGuiParamGesture(*p, kSceneParamId, scene + 1u);
            return;
        }
    }
    if (NSPointInRect(point, storeButtonRect())) {
        queueGuiPulse(*p, kCaptureParamId);
        return;
    }
    if (NSPointInRect(point, holdButtonRect())) {
        queueGuiParamGesture(*p, kHoldParamId,
            paramValue(*p, kHoldParamId) >= .5 ? 0.0 : 1.0);
        return;
    }
    if (NSPointInRect(point, runButtonRect())) {
        queueGuiParamGesture(*p, kRunParamId,
            paramValue(*p, kRunParamId) >= .5 ? 0.0 : 1.0);
        return;
    }
    if (NSPointInRect(point, sceneMorphRect())) {
        _dragParam = static_cast<int>(kSceneMorphParamId);
        queueGuiParamGestureBegin(*p, kSceneMorphParamId);
        [self updateDraggedParam:point];
        return;
    }
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        if (NSPointInRect(point, cutMaskButtonRect(cell))) {
            const clap_id id = kCutMaskParamBase + cell;
            _cutMaskPaintValue = paramValue(*p, id) >= .5 ? 0.0 : 1.0;
            _cutMaskPainting = true;
            _lastPaintedMask = -1;
            [self paintCutMaskAt:point];
            return;
        }
    }
    for (uint32_t index = 0u; index < 2u; ++index) {
        if (!NSPointInRect(point, fractureLatchButtonRect(index))) continue;
        const clap_id id = kFractureLatchParamIds[index];
        queueGuiParamGesture(*p, id,
            paramValue(*p, id) >= .5 ? 0.0 : 1.0);
        return;
    }
    for (uint32_t index = 0u; index < 2u; ++index) {
        if (!NSPointInRect(point, fracturePadRect(index))) continue;
        _fracturePad = static_cast<int>(index);
        const clap_id xId = kFractureXParamIds[index];
        const clap_id yId = kFractureYParamIds[index];
        _fractureLastX[index] = paramValue(*p, xId);
        _fractureLastY[index] = paramValue(*p, yId);
        _fractureLastTime[index]
            = [NSDate timeIntervalSinceReferenceDate] - 1.0 / 60.0;
        _fractureMotionReady[index] = true;
        queueGuiParamGestureBegin(*p, xId);
        queueGuiParamGestureBegin(*p, yId);
        [self updateFracturePad:point];
        return;
    }
    if (NSPointInRect(point, grabButtonRect())) {
        queueGuiParamGesture(*p, kGrabParamId,
            paramValue(*p, kGrabParamId) >= .5 ? 0.0 : 1.0);
        return;
    }
    if (NSPointInRect(point, repeatButtonRect())) {
        _repeatPressed = true;
        queueGuiParamGestureBegin(*p, kRepeatParamId);
        queueGuiParamValue(*p, kRepeatParamId, 1.0);
        return;
    }
    for (uint32_t shape = 0u; shape < 6u; ++shape) {
        if (NSPointInRect(point, topologyButtonRect(shape))) {
            queueGuiParamGesture(*p, kTopologyShapeParamId, shape);
            queueGuiPulse(*p, kApplyTopologyParamId);
            std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
            return;
        }
    }
    if (NSPointInRect(point, topologyMixRect())) {
        _dragParam = static_cast<int>(kTopologyMixParamId);
        queueGuiParamGestureBegin(*p, kTopologyMixParamId);
        [self updateDraggedParam:point];
        return;
    }
    for (uint32_t action = 0u; action < 5u; ++action) {
        if (NSPointInRect(point, actionButtonRect(action))) {
            queueGuiPulse(*p, kCutParamId + action);
            return;
        }
    }
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        if (NSPointInRect(point, cellButtonRect(cell))) {
            queueGuiParamGesture(*p, kSelectedCellParamId, cell + 1u);
            queueGuiPulse(*p, kStrikeParamId);
            return;
        }
    }
    for (uint32_t destination = 0u; destination < 8u; ++destination) {
        for (uint32_t source = 0u; source < 8u; ++source) {
            if (!NSPointInRect(point, matrixCellRect(destination, source))) {
                continue;
            }
            _selectedSource = source;
            _selectedDestination = destination;
            const clap_id id = kMatrixParamBase + destination * 8u + source;
            const double current = paramValue(*p, id);
            _matrixPaintValue = std::abs(current) < .01
                ? .34 : (current > 0.0 ? -.34 : 0.0);
            _matrixPainting = true;
            _lastPaintedRoute = -1;
            [self paintMatrixAt:point];
            return;
        }
    }
    const clap_id routeId = kMatrixParamBase
        + _selectedDestination * 8u + _selectedSource;
    if (NSPointInRect(point, routePolarityRect())) {
        queueGuiParamGesture(*p, routeId, -paramValue(*p, routeId));
        std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
        return;
    }
    if (NSPointInRect(point, routeClearRect())) {
        queueGuiParamGesture(*p, routeId, 0.0);
        std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
        return;
    }
    if (NSPointInRect(point, routeSliderRect())) {
        _dragParam = static_cast<int>(routeId);
        queueGuiParamGestureBegin(*p, routeId);
        [self updateDraggedParam:point];
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(kGesturePanel, 0u)))) {
        _dragParam = static_cast<int>(kVariationParamId);
        queueGuiParamGestureBegin(*p, kVariationParamId);
        [self updateDraggedParam:point];
        return;
    }
    const uint32_t selectedCell = static_cast<uint32_t>(std::clamp(
        std::round(paramValue(*p, kSelectedCellParamId)), 1.0, 8.0)) - 1u;
    for (const auto& row : kObjectRows) {
        if (!NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(kObjectPanel, row.row)))) {
            continue;
        }
        _dragParam = static_cast<int>(row.base + selectedCell);
        queueGuiParamGestureBegin(*p, static_cast<clap_id>(_dragParam));
        [self updateDraggedParam:point];
        return;
    }
    for (const auto& row : kUiRows) {
        if (!NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(*row.panel, row.row)))) {
            continue;
        }
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &p->plugin, row.id, &defaultValue)) {
            queueGuiParamGesture(*p, row.id, defaultValue);
            _dragParam = -1;
        } else {
            _dragParam = static_cast<int>(row.id);
            queueGuiParamGestureBegin(*p, row.id);
            [self updateDraggedParam:point];
        }
        return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:
        [event locationInWindow] fromView:nil];
    if (_matrixPainting) [self paintMatrixAt:point];
    else if (_cutMaskPainting) [self paintCutMaskAt:point];
    else if (_fracturePad >= 0) [self updateFracturePad:point];
    else if (_dragParam > 0) [self updateDraggedParam:point];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (_dragParam > 0) {
        queueGuiParamGestureEnd(*static_cast<Plugin*>(_plugin),
            static_cast<clap_id>(_dragParam));
    }
    auto* p = static_cast<Plugin*>(_plugin);
    if (_fracturePad >= 0 && p) {
        const uint32_t index = static_cast<uint32_t>(_fracturePad);
        const clap_id xId = kFractureXParamIds[index];
        const clap_id yId = kFractureYParamIds[index];
        if (paramValue(*p, kFractureLatchParamIds[index]) < .5) {
            queueGuiParamValue(*p, xId, 0.0);
            queueGuiParamValue(*p, yId, 0.0);
        }
        queueGuiParamGestureEnd(*p, xId);
        queueGuiParamGestureEnd(*p, yId);
        _fractureMotionReady[index] = false;
    }
    if (_repeatPressed && p) {
        queueGuiParamValue(*p, kRepeatParamId, 0.0);
        queueGuiParamGestureEnd(*p, kRepeatParamId);
    }
    _dragParam = -1;
    _matrixPainting = false;
    _cutMaskPainting = false;
    _fracturePad = -1;
    _repeatPressed = false;
    _lastPaintedRoute = -1;
    _lastPaintedMask = -1;
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating)
{
    return !floating && api
        && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api,
    bool* floating)
{
    if (!api || !floating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *floating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool floating)
{
    if (!guiIsApiSupported(plugin, api, floating)) return false;
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView = [[S3GProcessorFissureView alloc] initWithPlugin:p];
    if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport,
            static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight)) {
        [static_cast<NSView*>(p->guiView) release];
        p->guiView = nullptr;
        return false;
    }
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return;
    p->guiVisible = false;
    [static_cast<S3GProcessorFissureView*>(p->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(
        p->guiViewport, p->guiView);
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width,
    uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
}

bool guiCanResize(const clap_plugin_t*) { return true; }

bool guiGetResizeHints(const clap_plugin_t*,
    clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::getResponsiveResizeHints(hints);
}

bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width,
    uint32_t* height)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
}

bool guiSetSize(const clap_plugin_t* plugin, uint32_t width,
    uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(
        self(plugin)->guiViewport, width, height);
}

bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || !window->api
        || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) return false;
    auto* p = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport,
        static_cast<NSView*>(window->cocoa), p->host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*)
{
    return false;
}

void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            p->guiViewport, false)) return false;
    p->guiVisible = true;
    [static_cast<S3GProcessorFissureView*>(p->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GProcessorFissureView*>(p->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        p->guiViewport, true);
}

const clap_plugin_gui_t guiExt {
    guiIsApiSupported,
    guiGetPreferredApi,
    guiCreate,
    guiDestroy,
    guiSetScale,
    guiGetSize,
    guiCanResize,
    guiGetResizeHints,
    guiAdjustSize,
    guiSetSize,
    guiSetParent,
    guiSetTransient,
    guiSuggestTitle,
    guiShow,
    guiHide,
};

} // namespace
