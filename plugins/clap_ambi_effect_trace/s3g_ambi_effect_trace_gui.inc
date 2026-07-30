namespace {

constexpr s3g::gui_layout::Canvas kCanvas { 920.0, 820.0 };
constexpr s3g::gui_layout::Panel kFieldPanel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Engine,
    { 18.0, 42.0, 612.0, 760.0 }, 36.0, 26.0, 0u,
};
constexpr NSRect kPlot = { { 34.0, 76.0 }, { 580.0, 710.0 } };
constexpr s3g::gui_layout::Panel kOutputPanel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Output,
    { 648.0, 42.0, 258.0, 106.0 }, 36.0, 26.0, 3u,
};
#if S3G_PARTIAL_TRACE
constexpr s3g::gui_layout::Panel kEnginePanel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Engine,
    { 648.0, 160.0, 258.0, 292.0 }, 34.0, 24.0, 11u,
};
constexpr s3g::gui_layout::Panel kTopologyPanel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Topology,
    { 648.0, 464.0, 258.0, 106.0 }, 36.0, 26.0, 3u,
};
constexpr s3g::gui_layout::Panel kMaskPanel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Utility,
    { 648.0, 582.0, 258.0, 172.0 }, 34.0, 24.0, 6u,
};
#else
constexpr s3g::gui_layout::Panel kEnginePanel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Engine,
    { 648.0, 160.0, 258.0, 148.0 }, 34.0, 24.0, 5u,
};
constexpr s3g::gui_layout::Panel kTopologyPanel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Topology,
    { 648.0, 320.0, 258.0, 106.0 }, 36.0, 26.0, 3u,
};
constexpr s3g::gui_layout::Panel kMaskPanel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Utility,
    { 648.0, 438.0, 258.0, 172.0 }, 34.0, 24.0, 6u,
};
#endif

struct ProjectedBody {
    std::array<NSPoint, kPickups> points {};
    std::array<float, kPickups> depths {};
};

NSPoint projectTraceDirection(s3g::Vec3 direction, float azimuthDeg,
    float elevationDeg)
{
    const float yaw = -azimuthDeg * s3g::kPi / 180.0f;
    const float elevation = elevationDeg * s3g::kPi / 180.0f;
    const float x = direction.x * std::cos(yaw)
        - direction.y * std::sin(yaw);
    const float y = direction.x * std::sin(yaw)
        + direction.y * std::cos(yaw);
    const float projectedY = y * std::cos(elevation)
        + direction.z * std::sin(elevation);
    return NSMakePoint(NSMidX(kPlot) + x * 188.0f,
        NSMidY(kPlot) - projectedY * 188.0f);
}

ProjectedBody projectTraceBody(s3g::AmbiEffectBody body, float azimuthDeg,
    float elevationDeg)
{
    ProjectedBody result {};
    const auto directions = s3g::ambiEffectBodyDirections(body);
    const uint32_t count = s3g::ambiEffectBodyPickupCount(body);
    const float yaw = -azimuthDeg * s3g::kPi / 180.0f;
    const float elevation = elevationDeg * s3g::kPi / 180.0f;
    for (uint32_t node = 0u; node < count; ++node) {
        const auto direction = directions[node];
        const float y = direction.x * std::sin(yaw)
            + direction.y * std::cos(yaw);
        result.depths[node] = -y * std::sin(elevation)
            + direction.z * std::cos(elevation);
        result.points[node] = projectTraceDirection(direction,
            azimuthDeg, elevationDeg);
    }
    return result;
}

struct GuiSlider {
    clap_id id;
    const char* label;
    double minimum;
    double maximum;
    double defaultValue;
    bool logarithmic;
    bool reversed = false;
};

#if S3G_PARTIAL_TRACE
constexpr GuiSlider engineSliders[] {
    { kParamPartialCount, "PARTIALS", 1, 16, 10, false },
    { kParamSensitivity, "SENS", 0, 1, 0.62, false },
    { kParamMinimumFrequency, "LOW", 20, 4000, 55, true },
    { kParamMaximumFrequency, "HIGH", 200, 20000, 12000, true },
    { kParamTracking, "TRACK", 20, 1000, 120, true },
    { kParamRelease, "RELEASE", 40, 4000, 420, true },
    { kParamSmear, "SMEAR", 0, 1, 0, false },
    { kParamTranspose, "TRANS", -24, 24, 0, false },
    { kParamEngineGain, "TRACE", -36, 12, -3, false },
};
#else
constexpr GuiSlider engineSliders[] {
    { kParamCaptureSeconds, "TIME", 0.05, 1.5, 0.5, false },
    { kParamEngineGain, "RESPONSE", -36, 12, -6, false },
    { kParamTone, "TONE", 0, 1, 0.72, false },
};
#endif

constexpr GuiSlider topologySliders[] {
    { kParamTopologyAmount, "AMOUNT", 0, 1, 0.65, false },
    { kParamRoamingRate, "ROAM RATE", 0.005, 2, 0.08, true },
};

constexpr GuiSlider outputSlider {
    kParamOutput, "OUT", -60, 12, 0, false,
};
constexpr GuiSlider mixSlider {
    kParamMix, "MIX", 0, 1, kPartial ? 0.55 : 0.50, false,
};

constexpr GuiSlider maskSliders[] {
    { kParamMaskAmount, "MASK", 0, 1, 0, false },
    { kParamMaskAzimuth, "AZIMUTH", -180, 180, 0, false },
    { kParamMaskElevation, "ELEVATION", -90, 90, 0, false, true },
    { kParamMaskWidth, "WIDTH", 0, 1, 0.35, false },
    { kParamMaskCurve, "CURVE", 0, 1, 0.5, false },
    { kParamMaskDry, "MASK DRY", -1, 0, 0, false },
};

double sliderNorm(const GuiSlider& slider, double value)
{
    double norm = 0.0;
    if (slider.logarithmic) {
        norm = std::log(std::max(slider.minimum, value) / slider.minimum)
            / std::log(slider.maximum / slider.minimum);
    } else {
        norm = (value - slider.minimum) / (slider.maximum - slider.minimum);
    }
    return slider.reversed ? 1.0 - norm : norm;
}

double sliderValue(const GuiSlider& slider, double norm)
{
    norm = std::clamp(norm, 0.0, 1.0);
    if (slider.reversed) norm = 1.0 - norm;
    if (slider.logarithmic) {
        return slider.minimum * std::pow(
            slider.maximum / slider.minimum, norm);
    }
    return slider.minimum + (slider.maximum - slider.minimum) * norm;
}

NSRect actionRect(uint32_t index)
{
    const CGFloat x = static_cast<CGFloat>(kEnginePanel.frame.x + 16.0);
    const CGFloat width = static_cast<CGFloat>(kEnginePanel.frame.width - 32.0);
    const CGFloat y = static_cast<CGFloat>(
        s3g::gui_layout::rowY(kEnginePanel, 1u) - 4.0);
#if S3G_PARTIAL_TRACE
    constexpr CGFloat gap = 8.0;
    const CGFloat buttonWidth = (width - gap) * 0.5;
    return NSMakeRect(x + index * (buttonWidth + gap), y, buttonWidth, 20.0);
#else
    constexpr CGFloat gap = 6.0;
    const CGFloat buttonWidth = (width - gap * 2.0) / 3.0;
    return NSMakeRect(x + index * (buttonWidth + gap), y, buttonWidth, 20.0);
#endif
}

bool sliderLocation(clap_id id, const s3g::gui_layout::Panel*& panel,
    uint32_t& row)
{
    if (id == kParamOutput) {
        panel = &kOutputPanel; row = 0u; return true;
    }
    if (id == kParamMix) {
        panel = &kOutputPanel; row = 1u; return true;
    }
    for (uint32_t index = 0u; index < std::size(engineSliders); ++index) {
        if (engineSliders[index].id != id) continue;
        panel = &kEnginePanel; row = index + 2u; return true;
    }
    for (uint32_t index = 0u; index < std::size(topologySliders); ++index) {
        if (topologySliders[index].id != id) continue;
        panel = &kTopologyPanel; row = index + 1u; return true;
    }
    for (uint32_t index = 0u; index < std::size(maskSliders); ++index) {
        if (maskSliders[index].id != id) continue;
        panel = &kMaskPanel; row = index; return true;
    }
    return false;
}

} // namespace

@interface S3G_AMBI_EFFECT_TRACE_VIEW_CLASS : NSView {
    void* _plugin;
    int _dragParam;
    int _openMenu;
    int _hoverMenuItem;
    NSPoint _menuOrigin;
    uint32_t _menuItems;
    int _viewMode;
    CGFloat _viewAzDeg;
    CGFloat _viewElDeg;
    BOOL _dragView;
    NSPoint _lastDragPoint;
    NSTimer* _refreshTimer;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)setParam:(clap_id)param value:(double)value;
- (void)storeViewState;
@end

@implementation S3G_AMBI_EFFECT_TRACE_VIEW_CLASS

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragParam = -1;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuOrigin = NSZeroPoint;
        _menuItems = 0u;
        auto* instance = static_cast<Plugin*>(plugin);
        _viewMode = instance ? instance->guiViewMode : 2;
        _viewAzDeg = instance ? instance->guiViewAzDeg : 35.0;
        _viewElDeg = instance ? instance->guiViewElDeg : 34.0;
        _dragView = NO;
        _lastDragPoint = NSZeroPoint;
        _refreshTimer = nil;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)dealloc
{
    [self storeViewState];
    [self stopRefreshTimer];
    [super dealloc];
}

- (void)startRefreshTimer
{
    if (_refreshTimer) return;
    _refreshTimer = [NSTimer timerWithTimeInterval:1.0 / 24.0 target:self
        selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_refreshTimer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (!_refreshTimer) return;
    [_refreshTimer invalidate];
    _refreshTimer = nil;
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if (_plugin && ![self isHidden] && s3g::clap_support::hostAppIsActive()) {
        [self setNeedsDisplay:YES];
    }
}

- (void)storeViewState
{
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return;
    instance->guiViewMode = _viewMode;
    instance->guiViewAzDeg = static_cast<float>(_viewAzDeg);
    instance->guiViewElDeg = static_cast<float>(_viewElDeg);
}

- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (mode == 0) { _viewAzDeg = 90.0; _viewElDeg = 0.0; }
    else if (mode == 1) { _viewAzDeg = 90.0; _viewElDeg = 90.0; }
    else { _viewAzDeg = 35.0; _viewElDeg = 34.0; }
    [self storeViewState];
    [self setNeedsDisplay:YES];
}

- (void)setParam:(clap_id)param value:(double)value
{
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return;
    applyParam(*instance, param, value);
    [self setNeedsDisplay:YES];
}

- (void)drawPanel:(const s3g::gui_layout::Panel&)panel title:(NSString*)title
    style:(const s3g::clap_gui::Style&)style attrs:(NSDictionary*)attrs
{
    s3g::clap_gui::drawPanelFrame(panel, style);
    s3g::clap_gui::drawPanelHeader(title, true, panel, attrs, style);
}

- (void)drawMenuRow:(NSString*)label text:(NSString*)text
    panel:(const s3g::gui_layout::Panel&)panel row:(uint32_t)row
    style:(const s3g::clap_gui::Style&)style attrs:(NSDictionary*)attrs
    valueAttrs:(NSDictionary*)valueAttrs
{
    s3g::clap_gui::drawProcessorMenu(label, text,
        static_cast<CGFloat>(s3g::gui_layout::rowY(panel, row)),
        static_cast<CGFloat>(panel.frame.x),
        static_cast<CGFloat>(panel.frame.width), attrs, valueAttrs, style);
}

- (void)drawSlider:(const GuiSlider&)slider
    panel:(const s3g::gui_layout::Panel&)panel row:(uint32_t)row
    style:(const s3g::clap_gui::Style&)style attrs:(NSDictionary*)attrs
    valueAttrs:(NSDictionary*)valueAttrs
{
    auto* instance = static_cast<Plugin*>(_plugin);
    const double raw = getParam(*instance, slider.id);
    char valueText[64] {};
    paramsValueToText(&instance->plugin, slider.id, raw,
        valueText, sizeof(valueText));
    const CGFloat norm = std::clamp(sliderNorm(slider, raw), 0.0, 1.0);
    s3g::clap_gui::drawProcessorSlider(
        [NSString stringWithUTF8String:slider.label],
        [NSString stringWithUTF8String:valueText], norm,
        static_cast<CGFloat>(s3g::gui_layout::rowY(panel, row)),
        static_cast<CGFloat>(panel.frame.x),
        static_cast<CGFloat>(panel.frame.width), attrs, valueAttrs, style);
}

- (void)drawButton:(NSRect)rect text:(NSString*)text active:(BOOL)active
    style:(const s3g::clap_gui::Style&)style attrs:(NSDictionary*)attrs
{
    [s3g::clap_gui::color(active ? 0x4b554f : 0x1b1f1d) setFill];
    NSRectFill(rect); [style.grid setStroke]; NSFrameRect(rect);
    const NSSize size = [text sizeWithAttributes:attrs];
    [text drawAtPoint:NSMakePoint(NSMidX(rect) - size.width * 0.5,
        NSMidY(rect) - size.height * 0.5) withAttributes:attrs];
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* instance = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSDictionary* label = s3g::clap_gui::softLabelAttrs();
    NSDictionary* value = s3g::clap_gui::softValueAttrs();
    const float peak = instance->outputPeak.exchange(
        instance->outputPeak.load(std::memory_order_relaxed) * 0.92f,
        std::memory_order_relaxed);
    const float safety = instance->safetyGain.load(std::memory_order_relaxed);
    const auto body = static_cast<s3g::AmbiEffectBody>(
        instance->resolvedBody.load(std::memory_order_relaxed));
    const float governor = instance->excitationGain.load(
        std::memory_order_relaxed);
    NSString* governorText = governor < 0.999f
        ? [NSString stringWithFormat:@" · GOV %.1f dB",
            20.0f * std::log10(std::max(0.000001f, governor))]
        : @"";
    NSString* status = [NSString stringWithFormat:@"%uOA · %s · %@%@%@",
        instance->params.order, s3g::ambiEffectBodyName(body),
        s3g::clap_gui::peakDbText(peak), governorText, safety < 0.999f
            ? [NSString stringWithFormat:@" · SAFE %.1f dB",
                20.0f * std::log10(std::max(0.000001f, safety))] : @""];
    s3g::clap_gui::drawAmbiEffectTitleBand(kPartial
        ? @"s3g AMBI EFFECT PARTIAL TRACE 64CH"
        : @"s3g AMBI EFFECT RESPONSE TRACE 64CH",
        [NSString stringWithUTF8String:instance->presetName], status,
        s3g::gui_layout::transformTitleBand(kCanvas), style);

    [self drawPanel:kFieldPanel title:kPartial
        ? @"LISTENER / SINUSOIDAL FIELD" : @"LISTENER / RESPONSE FIELD"
        style:style attrs:label];
    const NSRect fieldPanel = s3g::clap_gui::cocoaRect(kFieldPanel.frame);
    s3g::clap_gui::drawTopologyProcessorCameraButtons(
        fieldPanel, _viewMode, value, style);
    [s3g::clap_gui::color(0x101412) setFill]; NSRectFill(kPlot);
    [style.grid setStroke]; NSFrameRect(kPlot);

    NSString* fieldStatus = nil;
#if S3G_PARTIAL_TRACE
    fieldStatus = [NSString stringWithFormat:@"%@%u ACTIVE PARTIALS · STRONGEST %.1f Hz",
        instance->params.freeze ? @"FROZEN · " : @"",
        instance->activePartials.load(std::memory_order_relaxed),
        instance->strongestFrequency.load(std::memory_order_relaxed)];
#else
    const bool capturingNow = instance->capturing.load(std::memory_order_relaxed);
    const bool preparingNow = instance->preparingResponse.load(
        std::memory_order_relaxed);
    const bool ready = instance->hasResponse.load(std::memory_order_relaxed);
    const bool requested = instance->params.responseEnabled != 0u;
    const bool audible = instance->responseAudible.load(
        std::memory_order_relaxed);
    const uint32_t capturedCount = instance->capturedPickupCount.load(
        std::memory_order_relaxed);
    fieldStatus = capturingNow
        ? [NSString stringWithFormat:@"CAPTURING RESPONSE · %.0f%%",
            instance->captureProgress.load(std::memory_order_relaxed) * 100.0f]
        : (preparingNow
            ? [NSString stringWithFormat:@"PREPARING RESPONSE · %.0f%%%@",
                instance->preparationProgress.load(
                    std::memory_order_relaxed) * 100.0f,
                requested ? @" · APPLY QUEUED" : @""]
        : (ready ? [NSString stringWithFormat:@"%u DIRECTIONAL KERNELS · %@",
                capturedCount, audible ? @"APPLIED"
                    : (requested ? @"APPLYING" : @"READY · BYPASSED")]
            : @"NO RESPONSE · PRESS CAPTURE"));
#endif
    [fieldStatus drawAtPoint:NSMakePoint(kPlot.origin.x + 12.0,
        kPlot.origin.y + 12.0) withAttributes:value];

    const auto projected = projectTraceBody(body,
        static_cast<float>(_viewAzDeg), static_cast<float>(_viewElDeg));
    const uint32_t count = s3g::ambiEffectBodyPickupCount(body);
    for (uint32_t first = 0u; first < count; ++first) {
        for (uint32_t second = first + 1u; second < count; ++second) {
            const CGFloat dx = projected.points[first].x
                - projected.points[second].x;
            const CGFloat dy = projected.points[first].y
                - projected.points[second].y;
            if (dx * dx + dy * dy > 166.0 * 166.0) continue;
            [s3g::clap_gui::color(0x29302c) setStroke];
            NSBezierPath* edge = [NSBezierPath bezierPath];
            [edge moveToPoint:projected.points[first]];
            [edge lineToPoint:projected.points[second]];
            [edge stroke];
        }
    }
    if (instance->params.maskAmount > 0.001f) {
        const float azimuth = instance->params.maskAzimuthDeg
            * s3g::kPi / 180.0f;
        const float elevation = instance->params.maskElevationDeg
            * s3g::kPi / 180.0f;
        const float cosElevation = std::cos(elevation);
        const s3g::Vec3 direction { cosElevation * std::cos(azimuth),
            cosElevation * std::sin(azimuth), std::sin(elevation) };
        const NSPoint target = projectTraceDirection(direction,
            static_cast<float>(_viewAzDeg), static_cast<float>(_viewElDeg));
        const CGFloat radius = 15.0 + instance->params.maskWidth * 38.0;
        NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(target.x - radius, target.y - radius,
                radius * 2.0, radius * 2.0)];
        CGFloat dash[] { 4.0, 3.0 };
        [ring setLineDash:dash count:2 phase:0.0];
        [s3g::clap_gui::color(0xcbd0cd, 0.65) setStroke]; [ring stroke];
    }
    std::array<uint32_t, kPickups> order {};
    for (uint32_t node = 0u; node < count; ++node) order[node] = node;
    std::sort(order.begin(), order.begin() + count,
        [&](uint32_t a, uint32_t b) {
            return projected.depths[a] < projected.depths[b];
        });
    for (uint32_t rank = 0u; rank < count; ++rank) {
        const uint32_t node = order[rank];
        const float inputLevel = instance->nodeLevel[node].load(
            std::memory_order_relaxed);
        const float effectLevel = instance->nodeEffectLevel[node].load(
            std::memory_order_relaxed);
        const float wet = instance->nodeWetMask[node].load(
            std::memory_order_relaxed);
        const CGFloat radius = 5.0 + std::min(8.0f,
            inputLevel * 26.0f + effectLevel * 34.0f);
        const CGFloat brightness = std::clamp(0.24f + inputLevel * 0.8f
            + effectLevel * 1.4f, 0.24f, 0.95f);
        [[NSColor colorWithCalibratedRed:brightness * wet
            green:brightness blue:brightness * 0.82 alpha:1.0] setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            projected.points[node].x - radius,
            projected.points[node].y - radius, radius * 2.0, radius * 2.0)] fill];
    }

    [self drawPanel:kOutputPanel title:@"OUTPUT" style:style attrs:label];
    [self drawPanel:kEnginePanel title:kPartial
        ? @"PARTIAL TRACE" : @"RESPONSE TRACE"
        style:style attrs:label];
    [self drawPanel:kTopologyPanel title:@"TOPOLOGY"
        style:style attrs:label];
    [self drawPanel:kMaskPanel title:@"DIRECTIONAL WET MASK"
        style:style attrs:label];

    [self drawSlider:outputSlider panel:kOutputPanel row:0u
        style:style attrs:label valueAttrs:value];
    [self drawSlider:mixSlider panel:kOutputPanel row:1u
        style:style attrs:label valueAttrs:value];
    [self drawMenuRow:@"ORDER" text:[NSString stringWithFormat:@"%uOA",
        instance->params.order] panel:kOutputPanel row:2u
        style:style attrs:label valueAttrs:value];
    NSString* bodyName = instance->params.body == s3g::AmbiEffectBody::Auto
        ? [NSString stringWithFormat:@"AUTO → %s", s3g::ambiEffectBodyName(body)]
        : [NSString stringWithUTF8String:s3g::ambiEffectBodyName(
            instance->params.body)];
    [self drawMenuRow:@"BODY" text:bodyName panel:kEnginePanel row:0u
        style:style attrs:label valueAttrs:value];
    [self drawMenuRow:@"MODE" text:[NSString stringWithUTF8String:
        s3g::ambiEffectTopologyName(instance->params.topology)]
        panel:kTopologyPanel row:0u style:style attrs:label valueAttrs:value];

#if S3G_PARTIAL_TRACE
    const bool enabled = instance->params.enabled != 0u;
    [self drawButton:actionRect(0u) text:enabled ? @"BYPASS TRACE" : @"APPLY TRACE"
        active:enabled style:style attrs:value];
    const bool frozen = instance->params.freeze != 0u;
    [self drawButton:actionRect(1u) text:frozen ? @"UNFREEZE" : @"FREEZE"
        active:frozen style:style attrs:value];
#else
    const bool captureButtonActive = instance->capturing.load(
        std::memory_order_relaxed);
    const bool responseApplied = instance->params.responseEnabled != 0u;
    [self drawButton:actionRect(0u) text:captureButtonActive ? @"LISTEN" : @"CAPTURE"
        active:captureButtonActive style:style attrs:value];
    [self drawButton:actionRect(1u) text:responseApplied ? @"BYPASS" : @"APPLY"
        active:responseApplied style:style attrs:value];
    [self drawButton:actionRect(2u) text:@"CLEAR" active:NO
        style:style attrs:value];
#endif
    for (uint32_t index = 0u; index < std::size(engineSliders); ++index) {
        [self drawSlider:engineSliders[index] panel:kEnginePanel row:index + 2u
            style:style attrs:label valueAttrs:value];
    }
    for (uint32_t index = 0u; index < std::size(topologySliders); ++index) {
        [self drawSlider:topologySliders[index] panel:kTopologyPanel
            row:index + 1u style:style attrs:label valueAttrs:value];
    }
    for (uint32_t index = 0u; index < std::size(maskSliders); ++index) {
        [self drawSlider:maskSliders[index] panel:kMaskPanel row:index
            style:style attrs:label valueAttrs:value];
    }

    if (_openMenu > 0) {
        NSString* orderItems[] { @"1OA", @"2OA", @"3OA", @"4OA",
            @"5OA", @"6OA", @"7OA" };
#if S3G_PARTIAL_TRACE
        NSString* bodyItems[] { @"AUTO", @"ICOSA 12", @"DODECA 20",
            @"SPHERE 24" };
#else
        NSString* bodyItems[] { @"AUTO", @"TETRA 4", @"CUBE 8",
            @"ICOSA 12", @"DODECA 20", @"SPHERE 24" };
#endif
        NSString* topologyItems[] { @"LOCAL", @"CROSS", @"DIFFUSE",
            @"ROAMING" };
        NSString** items = _openMenu == 1 ? orderItems
            : (_openMenu == 2 ? bodyItems : topologyItems);
        int selected = 0;
        if (_openMenu == 1) {
            selected = static_cast<int>(instance->params.order) - 1;
        } else if (_openMenu == 2) {
#if S3G_PARTIAL_TRACE
            const uint32_t bodyValue = static_cast<uint32_t>(
                instance->params.body);
            selected = bodyValue == 0u ? 0 : static_cast<int>(bodyValue) - 2;
#else
            selected = static_cast<int>(instance->params.body);
#endif
        } else {
            selected = static_cast<int>(instance->params.topology);
        }
        const CGFloat width = static_cast<CGFloat>(
            s3g::gui_layout::processorMenuWidth(kOutputPanel.frame.width));
        s3g::clap_gui::drawDropdownMenu(
            NSMakeRect(_menuOrigin.x, _menuOrigin.y,
                width, 18.0 * _menuItems), 18.0, items, _menuItems,
            selected, _hoverMenuItem, value, style);
    }
}

- (const GuiSlider*)sliderForParam:(clap_id)param
{
    if (param == kParamOutput) return &outputSlider;
    if (param == kParamMix) return &mixSlider;
    for (const auto& slider : engineSliders) if (slider.id == param) return &slider;
    for (const auto& slider : topologySliders) if (slider.id == param) return &slider;
    for (const auto& slider : maskSliders) if (slider.id == param) return &slider;
    return nullptr;
}

- (void)updateSliderAtPoint:(NSPoint)point
{
    const GuiSlider* slider = [self sliderForParam:static_cast<clap_id>(_dragParam)];
    if (!slider) return;
    const s3g::gui_layout::Panel* panel = nullptr;
    uint32_t row = 0u;
    if (!sliderLocation(slider->id, panel, row) || !panel) return;
    const double norm = (point.x
        - s3g::gui_layout::processorControlX(panel->frame.x))
        / s3g::gui_layout::processorTrackWidth(panel->frame.width);
    double value = sliderValue(*slider, norm);
    if (slider->id == kParamPartialCount) value = std::round(value);
    [self setParam:slider->id value:value];
}

- (bool)hitSliderRows:(NSPoint)point event:(NSEvent*)event
{
    std::array<const GuiSlider*, 20u> sliders {};
    uint32_t count = 0u;
    sliders[count++] = &outputSlider;
    sliders[count++] = &mixSlider;
    for (const auto& slider : engineSliders) sliders[count++] = &slider;
    for (const auto& slider : topologySliders) sliders[count++] = &slider;
    for (const auto& slider : maskSliders) sliders[count++] = &slider;
    auto* instance = static_cast<Plugin*>(_plugin);
    for (uint32_t index = 0u; index < count; ++index) {
        const auto* slider = sliders[index];
        const s3g::gui_layout::Panel* panel = nullptr;
        uint32_t row = 0u;
        if (!sliderLocation(slider->id, panel, row) || !panel) continue;
        const NSRect hit = s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(*panel, row));
        if (!NSPointInRect(point, hit)) continue;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
            event, &instance->plugin, slider->id, &defaultValue)) {
            [self setParam:slider->id value:defaultValue];
        } else {
            _dragParam = static_cast<int>(slider->id);
            [self updateSliderAtPoint:point];
        }
        return true;
    }
    return false;
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* instance = static_cast<Plugin*>(_plugin);
    if (s3g::clap_gui::handleProcessorTitleClick(point, &instance->plugin,
        kPartial ? @"Ambi Effect Partial Trace" : @"Ambi Effect Response Trace",
        s3g::gui_layout::transformTitleBand(kCanvas), instance->presetName,
        sizeof(instance->presetName))) {
        [self setNeedsDisplay:YES]; return;
    }
    if (_openMenu > 0) {
        const CGFloat width = static_cast<CGFloat>(
            s3g::gui_layout::processorMenuWidth(kOutputPanel.frame.width));
        const int hit = s3g::clap_gui::dropdownHitIndex(point,
            NSMakeRect(_menuOrigin.x, _menuOrigin.y,
                width, 18.0 * _menuItems), 18.0, _menuItems);
        if (hit >= 0) {
            if (_openMenu == 1) {
                [self setParam:kParamOrder value:hit + 1.0];
            } else if (_openMenu == 2) {
#if S3G_PARTIAL_TRACE
                [self setParam:kParamBody value:hit == 0 ? 0.0 : hit + 2.0];
#else
                [self setParam:kParamBody value:hit];
#endif
            } else {
                [self setParam:kParamTopology value:hit];
            }
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    const NSRect fieldPanel = s3g::clap_gui::cocoaRect(kFieldPanel.frame);
    for (uint32_t camera = 0u; camera < 3u; ++camera) {
        if (NSPointInRect(point,
            s3g::clap_gui::topologyProcessorCameraButtonRect(
                fieldPanel, camera))) {
            [self setViewPreset:static_cast<int>(camera)]; return;
        }
    }
    struct MenuHit {
        const s3g::gui_layout::Panel* panel;
        uint32_t row;
        int menu;
        uint32_t items;
    };
    const MenuHit menus[] {
        { &kOutputPanel, 2u, 1, 7u },
#if S3G_PARTIAL_TRACE
        { &kEnginePanel, 0u, 2, 4u },
#else
        { &kEnginePanel, 0u, 2, 6u },
#endif
        { &kTopologyPanel, 0u, 3, 4u },
    };
    for (const auto& menu : menus) {
        const NSRect hit = s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(*menu.panel, menu.row));
        if (!NSPointInRect(point, hit)) continue;
        _openMenu = menu.menu;
        _menuItems = menu.items;
        _menuOrigin = NSMakePoint(static_cast<CGFloat>(
            s3g::gui_layout::processorControlX(menu.panel->frame.x)),
            hit.origin.y + 24.0);
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
#if S3G_PARTIAL_TRACE
    if (NSPointInRect(point, actionRect(0u))) {
        [self setParam:kParamEnabled value:
            getParam(*static_cast<Plugin*>(_plugin), kParamEnabled) > 0.5 ? 0.0 : 1.0];
        return;
    }
    if (NSPointInRect(point, actionRect(1u))) {
        [self setParam:kParamFreeze value:
            getParam(*static_cast<Plugin*>(_plugin), kParamFreeze) > 0.5
                ? 0.0 : 1.0];
        return;
    }
#else
    if (NSPointInRect(point, actionRect(0u))) {
        [self setParam:kParamCapture value:1.0];
        [self setParam:kParamCapture value:0.0]; return;
    }
    if (NSPointInRect(point, actionRect(1u))) {
        [self setParam:kParamEnabled value:
            getParam(*static_cast<Plugin*>(_plugin), kParamEnabled) > 0.5 ? 0.0 : 1.0];
        return;
    }
    if (NSPointInRect(point, actionRect(2u))) {
        [self setParam:kParamClear value:1.0];
        [self setParam:kParamClear value:0.0]; return;
    }
#endif
    if ([self hitSliderRows:point event:event]) return;
    if (NSPointInRect(point, kPlot)) {
        _dragView = YES;
        _lastDragPoint = point;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragView) {
        _viewAzDeg += (point.x - _lastDragPoint.x) * 0.35;
        _viewElDeg = std::clamp(_viewElDeg
            + (point.y - _lastDragPoint.y) * 0.35, -85.0, 85.0);
        _viewMode = -1;
        _lastDragPoint = point;
        [self storeViewState];
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragParam < 0) return;
    [self updateSliderAtPoint:point];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu <= 0) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    const CGFloat width = static_cast<CGFloat>(
        s3g::gui_layout::processorMenuWidth(kOutputPanel.frame.width));
    const int hover = s3g::clap_gui::dropdownHitIndex(point,
        NSMakeRect(_menuOrigin.x, _menuOrigin.y,
            width, 18.0 * _menuItems), 18.0, _menuItems);
    if (hover == _hoverMenuItem) return;
    _hoverMenuItem = hover;
    [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = -1;
    _dragView = NO;
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating)
{
    return !floating && api && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* floating)
{
    if (!api || !floating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *floating = false;
    return true;
}
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool floating)
{
    if (!guiIsApiSupported(plugin, api, floating)) return false;
    auto* instance = self(plugin);
    if (instance->guiView) return true;
    instance->guiView = [[S3G_AMBI_EFFECT_TRACE_VIEW_CLASS alloc] initWithPlugin:instance];
    if (!instance->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(instance->guiViewport,
        static_cast<NSView*>(instance->guiView), kGuiWidth, kGuiHeight)) {
        [static_cast<NSView*>(instance->guiView) release];
        instance->guiView = nullptr;
        return false;
    }
    return true;
}
void guiDestroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance || !instance->guiView) return;
    instance->guiVisible.store(false, std::memory_order_relaxed);
    [static_cast<S3G_AMBI_EFFECT_TRACE_VIEW_CLASS*>(instance->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(instance->guiViewport,
        instance->guiView);
}
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
}
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::getResponsiveResizeHints(hints);
}
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
}
bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(
        self(plugin)->guiViewport, width, height);
}
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || !window->api
        || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) return false;
    auto* instance = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(instance->guiViewport,
        static_cast<NSView*>(window->cocoa), instance->host);
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView || !s3g::clap_gui::setResponsiveViewportHidden(
        instance->guiViewport, false)) return false;
    instance->guiVisible.store(true, std::memory_order_relaxed);
    [static_cast<S3G_AMBI_EFFECT_TRACE_VIEW_CLASS*>(instance->guiView) startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return false;
    instance->guiVisible.store(false, std::memory_order_relaxed);
    [static_cast<S3G_AMBI_EFFECT_TRACE_VIEW_CLASS*>(instance->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        instance->guiViewport, true);
}
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi,
    guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize,
    guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent,
    guiSetTransient, guiSuggestTitle, guiShow, guiHide };

} // namespace
