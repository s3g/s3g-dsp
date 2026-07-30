namespace {

constexpr s3g::gui_layout::Canvas kCanvas { 920.0, 820.0 };
constexpr s3g::gui_layout::Panel kFieldPanel {
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Engine,
    { 18.0, 42.0, 506.0, 774.0 }, 34.0, 24.0, 0u,
};
constexpr s3g::gui_layout::Rect kFieldPlot { 34.0, 78.0, 474.0, 520.0 };
constexpr s3g::gui_layout::Panel kOutputPanel {
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Output,
    { 536.0, 42.0, 366.0, 78.0 }, 34.0, 24.0, 2u,
};
constexpr s3g::gui_layout::Panel kCapturePanel {
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Engine,
    { 536.0, 132.0, 366.0, 150.0 }, 34.0, 24.0, 5u,
};
constexpr s3g::gui_layout::Panel kResynthesisPanel {
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Engine,
    { 536.0, 294.0, 366.0, 246.0 }, 34.0, 24.0, 9u,
};
constexpr s3g::gui_layout::Panel kTopologyPanel {
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Topology,
    { 536.0, 552.0, 366.0, 102.0 }, 34.0, 24.0, 3u,
};
constexpr s3g::gui_layout::Panel kMaskPanel {
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Utility,
    { 536.0, 666.0, 366.0, 150.0 }, 34.0, 24.0, 5u,
};

struct ProjectedBody {
    std::array<NSPoint, kPickups> points {};
    std::array<float, kPickups> depths {};
};

NSPoint projectDirection(s3g::Vec3 direction, float azimuthDeg,
    float elevationDeg)
{
    const float yaw = -azimuthDeg * s3g::kPi / 180.0f;
    const float elevation = elevationDeg * s3g::kPi / 180.0f;
    const float rotatedX = direction.x * std::cos(yaw)
        - direction.y * std::sin(yaw);
    const float rotatedY = direction.x * std::sin(yaw)
        + direction.y * std::cos(yaw);
    const float projectedY = rotatedY * std::cos(elevation)
        + direction.z * std::sin(elevation);
    const NSRect plot = s3g::clap_gui::cocoaRect(kFieldPlot);
    return NSMakePoint(NSMidX(plot) + rotatedX * 176.0f,
        plot.origin.y + 254.0 - projectedY * 176.0f);
}

ProjectedBody projectBody(s3g::AmbiEffectBody body, float azimuthDeg,
    float elevationDeg)
{
    ProjectedBody result {};
    const auto directions = s3g::ambiEffectBodyDirections(body);
    const uint32_t count = s3g::ambiEffectBodyPickupCount(body);
    const float yaw = -azimuthDeg * s3g::kPi / 180.0f;
    const float elevation = elevationDeg * s3g::kPi / 180.0f;
    for (uint32_t node = 0u; node < count; ++node) {
        const auto direction = directions[node];
        const float y = direction.x * std::sin(yaw) + direction.y * std::cos(yaw);
        result.depths[node] = -y * std::sin(elevation)
            + direction.z * std::cos(elevation);
        result.points[node] = projectDirection(direction,
            azimuthDeg, elevationDeg);
    }
    return result;
}

NSRect tuneAxisRect()
{
    return NSMakeRect(kFieldPlot.x + 44.0, 680.0,
        kFieldPlot.width - 88.0, 16.0);
}

NSRect decayAxisRect()
{
    return NSMakeRect(kFieldPlot.x + 44.0, 748.0,
        kFieldPlot.width - 88.0, 16.0);
}

NSRect captureButtonRect()
{
    return NSMakeRect(550.0, 253.0, 100.0, 20.0);
}

NSRect applyButtonRect()
{
    return NSMakeRect(658.0, 253.0, 108.0, 20.0);
}

NSRect clearButtonRect()
{
    return NSMakeRect(774.0, 253.0, 114.0, 20.0);
}

} // namespace

@interface S3GAmbiEffectResonancePrintView : NSView {
    void* _plugin;
    int _dragSlider;
    int _openMenu;
    int _hoverMenuItem;
    NSPoint _menuOrigin;
    uint32_t _menuItems;
    uint32_t _selectedPickup;
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
- (void)setParamOnce:(clap_id)param value:(double)value;
- (void)updateSliderAtPoint:(NSPoint)point;
- (void)storeViewState;
- (void)setViewPreset:(int)mode;
@end

@implementation S3GAmbiEffectResonancePrintView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuOrigin = NSZeroPoint;
        _menuItems = 0u;
        _selectedPickup = 0u;
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
    _refreshTimer = [NSTimer timerWithTimeInterval:1.0 / 24.0
        target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_refreshTimer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (_refreshTimer) {
        [_refreshTimer invalidate];
        _refreshTimer = nil;
    }
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
    queueGuiParamValue(*static_cast<Plugin*>(_plugin), param, value);
    [self setNeedsDisplay:YES];
}

- (void)setParamOnce:(clap_id)param value:(double)value
{
    auto* instance = static_cast<Plugin*>(_plugin);
    queueGuiParamGestureBegin(*instance, param);
    queueGuiParamValue(*instance, param, value);
    queueGuiParamGestureEnd(*instance, param);
    [self setNeedsDisplay:YES];
}

- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm
    panel:(const s3g::gui_layout::Panel&)panel row:(uint32_t)row
    attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawProcessorSlider(name, value,
        std::clamp(norm, 0.0, 1.0), s3g::gui_layout::rowY(panel, row),
        panel.frame.x, panel.frame.width, attrs, attrs, style);
}

- (void)drawMenu:(NSString*)name value:(NSString*)value
    panel:(const s3g::gui_layout::Panel&)panel row:(uint32_t)row
    attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawProcessorMenu(name, value,
        s3g::gui_layout::rowY(panel, row), panel.frame.x, panel.frame.width,
        attrs, attrs, style);
}

- (void)drawAxis:(NSRect)axis value:(float)value left:(NSString*)left
    center:(NSString*)center right:(NSString*)right
    attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    [style.strip setFill];
    NSRectFill(axis);
    [style.grid setStroke];
    NSFrameRect(axis);
    [left drawAtPoint:NSMakePoint(axis.origin.x, axis.origin.y - 19.0)
        withAttributes:attrs];
    [center drawAtPoint:NSMakePoint(NSMidX(axis) - 8.0, axis.origin.y - 19.0)
        withAttributes:attrs];
    [right drawAtPoint:NSMakePoint(NSMaxX(axis) - 42.0, axis.origin.y - 19.0)
        withAttributes:attrs];
    const CGFloat position = axis.origin.x
        + std::clamp((value + 1.0f) * 0.5f, 0.0f, 1.0f) * axis.size.width;
    [s3g::clap_gui::color(0xbfc7c2) setFill];
    NSRectFill(NSMakeRect(position - 2.0, axis.origin.y - 3.0,
        4.0, axis.size.height + 6.0));
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* instance = static_cast<Plugin*>(_plugin);
    const auto params = publishedParamsSnapshot(*instance);
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* label = s3g::clap_gui::softLabelAttrs();
    NSDictionary* value = s3g::clap_gui::softValueAttrs();
    const float peak = instance->outputPeak.exchange(
        instance->outputPeak.load(std::memory_order_relaxed) * 0.92f,
        std::memory_order_relaxed);
    const auto body = static_cast<s3g::AmbiEffectBody>(
        instance->resolvedBody.load(std::memory_order_relaxed));
    const float safety = instance->safetyGain.load(std::memory_order_relaxed);
    const float governor = instance->excitationGovernorGain.load(
        std::memory_order_relaxed);
    NSString* governorText = governor < 0.999f
        ? [NSString stringWithFormat:@" · GOV %.1f dB",
            20.0f * std::log10(std::max(0.000001f, governor))]
        : @"";
    NSString* safetyText = safety < 0.999f
        ? [NSString stringWithFormat:@" · SAFE %.1f dB",
            20.0f * std::log10(std::max(0.000001f, safety))]
        : @"";
    NSString* status = [NSString stringWithFormat:@"%uOA · %s · %@%@%@",
        params.order, s3g::ambiEffectBodyName(body),
        s3g::clap_gui::peakDbText(peak), governorText, safetyText];
    s3g::clap_gui::drawTransformTitleBand(
        @"s3g AMBI EFFECT RESONANCE PRINT 64CH",
        [NSString stringWithUTF8String:instance->presetName], status,
        s3g::gui_layout::transformTitleBand(kCanvas), style);

    s3g::clap_gui::drawPanelFrame(kFieldPanel, style);
    s3g::clap_gui::drawPanelHeader(@"DIRECTIONAL RESONANCE PRINT", true,
        kFieldPanel, label, style);
    const NSRect fieldPanel = s3g::clap_gui::cocoaRect(kFieldPanel.frame);
    s3g::clap_gui::drawTopologyProcessorCameraButtons(
        fieldPanel, _viewMode, label, style);
    const NSRect plot = s3g::clap_gui::cocoaRect(kFieldPlot);
    [s3g::clap_gui::color(0x111513) setFill];
    NSRectFill(plot);
    [style.grid setStroke];
    NSFrameRect(plot);

    const bool capturingNow = instance->capturing.load(std::memory_order_relaxed);
    const bool printed = instance->hasPrint.load(std::memory_order_relaxed);
    const bool printApplied = printed && params.printEnabled != 0u;
    const float progress = instance->captureProgress.load(std::memory_order_relaxed);
    NSString* printStatus = capturingNow
        ? [NSString stringWithFormat:@"LISTENING  %.0f%%", progress * 100.0f]
        : (printed
            ? (printApplied ? @"PRINT APPLIED" : @"PRINT READY · BYPASSED")
            : @"NO PRINT — PRESS CAPTURE");
    [printStatus drawAtPoint:NSMakePoint(plot.origin.x + 12.0, plot.origin.y + 10.0)
        withAttributes:value];
    if (capturingNow) {
        const NSRect progressTrack = NSMakeRect(plot.origin.x + 12.0,
            plot.origin.y + 32.0, plot.size.width - 24.0, 4.0);
        [style.grid setFill]; NSRectFill(progressTrack);
        [s3g::clap_gui::color(0x9cae9f) setFill];
        NSRectFill(NSMakeRect(progressTrack.origin.x, progressTrack.origin.y,
            progressTrack.size.width * progress, progressTrack.size.height));
    }

    const auto projected = projectBody(body,
        static_cast<float>(_viewAzDeg), static_cast<float>(_viewElDeg));
    const uint32_t count = s3g::ambiEffectBodyPickupCount(body);
    for (uint32_t first = 0u; first < count; ++first) {
        for (uint32_t second = first + 1u; second < count; ++second) {
            const CGFloat dx = projected.points[first].x - projected.points[second].x;
            const CGFloat dy = projected.points[first].y - projected.points[second].y;
            if (dx * dx + dy * dy > 155.0 * 155.0) continue;
            [s3g::clap_gui::color(0x29302c) setStroke];
            NSBezierPath* edge = [NSBezierPath bezierPath];
            [edge moveToPoint:projected.points[first]];
            [edge lineToPoint:projected.points[second]];
            [edge stroke];
        }
    }
    if (params.maskAmount > 0.001f) {
        const float azimuth = params.maskAzimuthDeg
            * s3g::kPi / 180.0f;
        const float elevation = params.maskElevationDeg
            * s3g::kPi / 180.0f;
        const float cosElevation = std::cos(elevation);
        const s3g::Vec3 direction {
            cosElevation * std::cos(azimuth),
            cosElevation * std::sin(azimuth),
            std::sin(elevation),
        };
        const NSPoint target = projectDirection(direction,
            static_cast<float>(_viewAzDeg), static_cast<float>(_viewElDeg));
        const CGFloat radius = 15.0 + params.maskWidth * 34.0;
        [s3g::clap_gui::color(0xcbd0cd,
            0.24 + params.maskAmount * 0.54) setStroke];
        NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(target.x - radius, target.y - radius,
                radius * 2.0, radius * 2.0)];
        CGFloat dash[] { 4.0, 3.0 };
        [ring setLineDash:dash count:2 phase:0.0];
        [ring setLineWidth:1.0];
        [ring stroke];
        NSBezierPath* cross = [NSBezierPath bezierPath];
        [cross moveToPoint:NSMakePoint(target.x - 5.0, target.y)];
        [cross lineToPoint:NSMakePoint(target.x + 5.0, target.y)];
        [cross moveToPoint:NSMakePoint(target.x, target.y - 5.0)];
        [cross lineToPoint:NSMakePoint(target.x, target.y + 5.0)];
        [cross stroke];
    }
    std::array<uint32_t, kPickups> depthOrder {};
    for (uint32_t node = 0u; node < count; ++node) depthOrder[node] = node;
    std::sort(depthOrder.begin(), depthOrder.begin() + count,
        [&](uint32_t a, uint32_t b) { return projected.depths[a] < projected.depths[b]; });
    for (uint32_t rank = 0u; rank < count; ++rank) {
        const uint32_t node = depthOrder[rank];
        const float strength = instance->nodeStrength[node].load(std::memory_order_relaxed);
        const float level = instance->nodeLevel[node].load(std::memory_order_relaxed);
        const float wet = instance->nodeWetMask[node].load(std::memory_order_relaxed);
        const CGFloat radius = 7.0 + strength * 9.0 + std::min(5.0f, level * 30.0f);
        const NSRect dot = NSMakeRect(projected.points[node].x - radius,
            projected.points[node].y - radius, radius * 2.0, radius * 2.0);
        const CGFloat brightness = std::clamp(0.25 + strength * 0.55
            + level * 0.5, 0.25, 0.95);
        [[NSColor colorWithCalibratedRed:brightness * wet
            green:brightness blue:brightness * 0.82 alpha:1.0] setFill];
        [[NSBezierPath bezierPathWithOvalInRect:dot] fill];
        if (node == _selectedPickup) {
            [s3g::clap_gui::color(0xd5dad7) setStroke];
            [[NSBezierPath bezierPathWithOvalInRect:NSInsetRect(dot, -4.0, -4.0)] stroke];
        }
        [[NSString stringWithFormat:@"%02u", node + 1u]
            drawAtPoint:NSMakePoint(projected.points[node].x - 7.0,
                projected.points[node].y + radius + 2.0)
            withAttributes:label];
    }

    const float f0 = instance->fundamentalHz.load(std::memory_order_relaxed);
    const float confidence = instance->pitchConfidence.load(std::memory_order_relaxed);
    const uint32_t modeCount = instance->printedModes.load(std::memory_order_relaxed);
    [[NSString stringWithFormat:@"PICKUP %02u  ·  %u MODES  ·  F0 %.1f Hz  ·  CONF %.0f%%",
        _selectedPickup + 1u, modeCount, f0, confidence * 100.0f]
        drawAtPoint:NSMakePoint(plot.origin.x + 12.0, NSMaxY(plot) + 20.0)
        withAttributes:value];
    [self drawAxis:tuneAxisRect()
        value:params.pickupTuneTrim[_selectedPickup]
        left:@"TUNE -12 ST" center:@"0" right:@"+12 ST"
        attrs:label style:style];
    [self drawAxis:decayAxisRect()
        value:params.pickupDecayTrim[_selectedPickup]
        left:@"DECAY -" center:@"0" right:@"DECAY +"
        attrs:label style:style];

    s3g::clap_gui::drawPanelFrame(kOutputPanel, style);
    s3g::clap_gui::drawPanelHeader(@"FIELD", true, kOutputPanel, label, style);
    s3g::clap_gui::drawPanelFrame(kCapturePanel, style);
    s3g::clap_gui::drawPanelHeader(@"ANALYZE / CAPTURE", true,
        kCapturePanel, label, style);
    s3g::clap_gui::drawPanelFrame(kResynthesisPanel, style);
    s3g::clap_gui::drawPanelHeader(@"MODAL RESYNTHESIS", true,
        kResynthesisPanel, label, style);
    s3g::clap_gui::drawPanelFrame(kTopologyPanel, style);
    s3g::clap_gui::drawPanelHeader(@"TOPOLOGY", true,
        kTopologyPanel, label, style);
    s3g::clap_gui::drawPanelFrame(kMaskPanel, style);
    s3g::clap_gui::drawPanelHeader(@"DIRECTIONAL WET MASK", true,
        kMaskPanel, label, style);

    [self drawSlider:@"OUT" value:[NSString stringWithFormat:@"%+.1f dB",
        params.outputGainDb]
        norm:(params.outputGainDb + 60.0f) / 72.0f
        panel:kOutputPanel row:0u attrs:value style:style];
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA",
        params.order] panel:kOutputPanel row:1u attrs:value style:style];
    NSString* bodyText = params.body == s3g::AmbiEffectBody::Auto
        ? [NSString stringWithFormat:@"AUTO → %s", s3g::ambiEffectBodyName(body)]
        : [NSString stringWithUTF8String:s3g::ambiEffectBodyName(params.body)];
    [self drawMenu:@"BODY" value:bodyText panel:kCapturePanel row:0u
        attrs:value style:style];
    [self drawSlider:@"TIME" value:[NSString stringWithFormat:@"%.2f s",
        params.captureSeconds]
        norm:(params.captureSeconds - 0.25f) / 2.75f
        panel:kCapturePanel row:1u attrs:value style:style];
    [self drawSlider:@"SENS" value:[NSString stringWithFormat:@"%.0f%%",
        params.sensitivity * 100.0f] norm:params.sensitivity
        panel:kCapturePanel row:2u attrs:value style:style];
    [self drawMenu:@"MODES" value:[NSString stringWithFormat:@"%u / PICKUP",
        params.modalCount] panel:kCapturePanel row:3u attrs:value style:style];

    const auto drawButton = [&](NSRect rect, NSString* text, bool active) {
        [s3g::clap_gui::color(active ? 0x46564d : 0x202522) setFill];
        NSRectFill(rect); [style.grid setStroke]; NSFrameRect(rect);
        const NSSize size = [text sizeWithAttributes:value];
        [text drawAtPoint:NSMakePoint(NSMidX(rect) - size.width * 0.5,
            NSMidY(rect) - size.height * 0.5) withAttributes:value];
    };
    drawButton(captureButtonRect(), capturingNow ? @"CANCEL" : @"CAPTURE",
        capturingNow);
    drawButton(applyButtonRect(), printApplied ? @"BYPASS PRINT" : @"APPLY PRINT",
        printApplied);
    drawButton(clearButtonRect(), @"CLEAR PRINT", false);

    [self drawSlider:@"TRANS" value:[NSString stringWithFormat:@"%+.1f st",
        params.transposeSemitones]
        norm:(params.transposeSemitones + 24.0f) / 48.0f
        panel:kResynthesisPanel row:0u attrs:value style:style];
    [self drawSlider:@"PULL" value:[NSString stringWithFormat:@"%.0f%%",
        params.harmonicPull * 100.0f] norm:params.harmonicPull
        panel:kResynthesisPanel row:1u attrs:value style:style];
    [self drawSlider:@"STRETCH" value:[NSString stringWithFormat:@"%+.0f%%",
        params.harmonicStretch * 100.0f]
        norm:(params.harmonicStretch + 1.0f) * 0.5f
        panel:kResynthesisPanel row:2u attrs:value style:style];
    const double decayNorm = std::log(params.decaySeconds / 0.08f)
        / std::log(8.0 / 0.08);
    [self drawSlider:@"DECAY" value:[NSString stringWithFormat:@"%.2f s",
        params.decaySeconds] norm:decayNorm
        panel:kResynthesisPanel row:3u attrs:value style:style];
    [self drawSlider:@"TILT" value:[NSString stringWithFormat:@"%+.0f%%",
        params.decayTilt * 100.0f]
        norm:(params.decayTilt + 1.0f) * 0.5f
        panel:kResynthesisPanel row:4u attrs:value style:style];
    [self drawSlider:@"DRIVE" value:[NSString stringWithFormat:@"%.0f%%",
        params.drive * 100.0f] norm:params.drive
        panel:kResynthesisPanel row:5u attrs:value style:style];
    [self drawSlider:@"SPREAD" value:[NSString stringWithFormat:@"%.0f%%",
        params.spread * 100.0f] norm:params.spread
        panel:kResynthesisPanel row:6u attrs:value style:style];
    [self drawSlider:@"DEV" value:[NSString stringWithFormat:@"%.0f%%",
        params.deviation * 100.0f] norm:params.deviation
        panel:kResynthesisPanel row:7u attrs:value style:style];
    [self drawSlider:@"MIX" value:[NSString stringWithFormat:@"%.0f%%",
        params.mix * 100.0f] norm:params.mix
        panel:kResynthesisPanel row:8u attrs:value style:style];

    [self drawMenu:@"MODE" value:[NSString stringWithUTF8String:
        s3g::ambiEffectTopologyName(params.topology)]
        panel:kTopologyPanel row:0u attrs:value style:style];
    [self drawSlider:@"AMT" value:[NSString stringWithFormat:@"%.0f%%",
        params.topologyAmount * 100.0f]
        norm:params.topologyAmount panel:kTopologyPanel row:1u
        attrs:value style:style];
    const double rateNorm = std::log(params.roamingRateHz / 0.005f)
        / std::log(2.0 / 0.005);
    [self drawSlider:@"RATE" value:[NSString stringWithFormat:@"%.3f Hz",
        params.roamingRateHz] norm:rateNorm panel:kTopologyPanel row:2u
        attrs:value style:style];

    [self drawSlider:@"MASK" value:[NSString stringWithFormat:@"%.0f%%",
        params.maskAmount * 100.0f] norm:params.maskAmount
        panel:kMaskPanel row:0u attrs:value style:style];
    [self drawSlider:@"AZ" value:[NSString stringWithFormat:@"%+.0f deg",
        params.maskAzimuthDeg]
        norm:(params.maskAzimuthDeg + 180.0f) / 360.0f
        panel:kMaskPanel row:1u attrs:value style:style];
    [self drawSlider:@"EL" value:[NSString stringWithFormat:@"%+.0f deg",
        params.maskElevationDeg]
        norm:(params.maskElevationDeg + 90.0f) / 180.0f
        panel:kMaskPanel row:2u attrs:value style:style];
    [self drawSlider:@"WIDTH" value:[NSString stringWithFormat:@"%.0f%%",
        params.maskWidth * 100.0f] norm:params.maskWidth
        panel:kMaskPanel row:3u attrs:value style:style];
    [self drawSlider:@"DRY" value:params.maskDry < 0.005f
        ? @"FX ONLY" : [NSString stringWithFormat:@"%.0f%%",
            (params.maskDry - 1.0f) * 100.0f]
        norm:params.maskDry panel:kMaskPanel row:4u
        attrs:value style:style];

    if (_openMenu > 0 && _menuItems > 0u) {
        NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
        NSString* bodyItems[] = { @"AUTO", @"ICOSA 12", @"DODECA 20", @"SPHERE 24" };
        NSString* modeItems[] = { @"2", @"3", @"4", @"5", @"6", @"7", @"8", @"9", @"10", @"11", @"12" };
        NSString* topologyItems[] = { @"LOCAL", @"CROSS", @"DIFFUSE", @"ROAMING" };
        NSString** items = _openMenu == 1 ? orderItems
            : (_openMenu == 2 ? bodyItems : (_openMenu == 3 ? modeItems : topologyItems));
        int selected = 0;
        if (_openMenu == 1) selected = static_cast<int>(params.order) - 1;
        else if (_openMenu == 2) selected = params.body == s3g::AmbiEffectBody::Auto
            ? 0 : (params.body == s3g::AmbiEffectBody::Dodeca20 ? 2
                : (params.body == s3g::AmbiEffectBody::Sphere24 ? 3 : 1));
        else if (_openMenu == 3) selected = static_cast<int>(params.modalCount) - 2;
        else selected = static_cast<int>(params.topology);
        const CGFloat width = s3g::gui_layout::processorMenuWidth(
            kOutputPanel.frame.width);
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(_menuOrigin.x, _menuOrigin.y,
            width, 18.0 * _menuItems), 18.0, items, _menuItems, selected,
            _hoverMenuItem, value, style);
    }
}

- (void)updateSliderAtPoint:(NSPoint)point
{
    auto* instance = static_cast<Plugin*>(_plugin);
    uint32_t pickup = 0u;
    if (_dragSlider >= 0
        && pickupTuneIndex(static_cast<clap_id>(_dragSlider), pickup)) {
        const NSRect axis = tuneAxisRect();
        const double norm = std::clamp((point.x - axis.origin.x) / axis.size.width,
            0.0, 1.0);
        [self setParam:static_cast<clap_id>(_dragSlider) value:norm * 2.0 - 1.0];
        return;
    }
    if (_dragSlider >= 0
        && pickupDecayIndex(static_cast<clap_id>(_dragSlider), pickup)) {
        const NSRect axis = decayAxisRect();
        const double norm = std::clamp((point.x - axis.origin.x) / axis.size.width,
            0.0, 1.0);
        [self setParam:static_cast<clap_id>(_dragSlider) value:norm * 2.0 - 1.0];
        return;
    }
    const s3g::gui_layout::Panel* panel = &kResynthesisPanel;
    if (_dragSlider == kParamOutput) panel = &kOutputPanel;
    else if (_dragSlider == kParamCaptureSeconds || _dragSlider == kParamSensitivity)
        panel = &kCapturePanel;
    else if (_dragSlider == kParamTopologyAmount || _dragSlider == kParamRoamingRate)
        panel = &kTopologyPanel;
    else if (_dragSlider >= static_cast<int>(kParamMaskAmount)
        && _dragSlider <= static_cast<int>(kParamMaskDry))
        panel = &kMaskPanel;
    const double norm = std::clamp((point.x
        - s3g::gui_layout::processorControlX(panel->frame.x))
        / s3g::gui_layout::processorTrackWidth(panel->frame.width), 0.0, 1.0);
    switch (_dragSlider) {
    case kParamOutput: [self setParam:kParamOutput value:-60.0 + norm * 72.0]; break;
    case kParamCaptureSeconds: [self setParam:kParamCaptureSeconds value:0.25 + norm * 2.75]; break;
    case kParamSensitivity: [self setParam:kParamSensitivity value:norm]; break;
    case kParamTranspose: [self setParam:kParamTranspose value:-24.0 + norm * 48.0]; break;
    case kParamHarmonicPull: [self setParam:kParamHarmonicPull value:norm]; break;
    case kParamHarmonicStretch: [self setParam:kParamHarmonicStretch value:norm * 2.0 - 1.0]; break;
    case kParamDecay: [self setParam:kParamDecay value:0.08 * std::pow(8.0 / 0.08, norm)]; break;
    case kParamDecayTilt: [self setParam:kParamDecayTilt value:norm * 2.0 - 1.0]; break;
    case kParamDrive: [self setParam:kParamDrive value:norm]; break;
    case kParamSpread: [self setParam:kParamSpread value:norm]; break;
    case kParamDeviation: [self setParam:kParamDeviation value:norm]; break;
    case kParamMix: [self setParam:kParamMix value:norm]; break;
    case kParamTopologyAmount: [self setParam:kParamTopologyAmount value:norm]; break;
    case kParamRoamingRate: [self setParam:kParamRoamingRate value:0.005 * std::pow(2.0 / 0.005, norm)]; break;
    case kParamMaskAmount: [self setParam:kParamMaskAmount value:norm]; break;
    case kParamMaskAzimuth: [self setParam:kParamMaskAzimuth value:-180.0 + norm * 360.0]; break;
    case kParamMaskElevation: [self setParam:kParamMaskElevation value:-90.0 + norm * 180.0]; break;
    case kParamMaskWidth: [self setParam:kParamMaskWidth value:norm]; break;
    case kParamMaskDry: [self setParam:kParamMaskDry value:norm - 1.0]; break;
    default: (void)instance; break;
    }
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* instance = static_cast<Plugin*>(_plugin);
    if (s3g::clap_gui::handleProcessorTitleClick(point, &instance->plugin,
        @"Ambi Effect Resonance Print",
        s3g::gui_layout::transformTitleBand(kCanvas), instance->presetName,
        sizeof(instance->presetName))) {
        [self setNeedsDisplay:YES]; return;
    }
    if (_openMenu > 0) {
        const CGFloat width = s3g::gui_layout::processorMenuWidth(
            kOutputPanel.frame.width);
        const int hit = s3g::clap_gui::dropdownHitIndex(point,
            NSMakeRect(_menuOrigin.x, _menuOrigin.y, width, 18.0 * _menuItems),
            18.0, _menuItems);
        if (hit >= 0) {
            if (_openMenu == 1) [self setParamOnce:kParamOrder value:hit + 1.0];
            else if (_openMenu == 2) [self setParamOnce:kParamBody
                value:(hit == 0 ? 0.0 : hit + 2.0)];
            else if (_openMenu == 3) [self setParamOnce:kParamModalCount value:hit + 2.0];
            else [self setParamOnce:kParamTopology value:hit];
        }
        _openMenu = 0; _hoverMenuItem = -1;
        [self setNeedsDisplay:YES]; return;
    }
    const NSRect fieldPanel = s3g::clap_gui::cocoaRect(kFieldPanel.frame);
    for (uint32_t index = 0u; index < 3u; ++index) {
        if (NSPointInRect(point,
            s3g::clap_gui::topologyProcessorCameraButtonRect(
                fieldPanel, index))) {
            [self setViewPreset:static_cast<int>(index)];
            return;
        }
    }
    if (NSPointInRect(point, captureButtonRect())) {
        queueGuiParamGestureBegin(*instance, kParamCapture);
        [self setParam:kParamCapture value:1.0];
        [self setParam:kParamCapture value:0.0];
        queueGuiParamGestureEnd(*instance, kParamCapture);
        return;
    }
    if (NSPointInRect(point, applyButtonRect())) {
        if (!instance->capturing.load(std::memory_order_relaxed)
            && instance->hasPrint.load(std::memory_order_relaxed)) {
            [self setParamOnce:kParamPrintEnabled
                value:getParam(*instance, kParamPrintEnabled) != 0.0 ? 0.0 : 1.0];
        }
        return;
    }
    if (NSPointInRect(point, clearButtonRect())) {
        queueGuiParamGestureBegin(*instance, kParamClear);
        [self setParam:kParamClear value:1.0];
        [self setParam:kParamClear value:0.0];
        queueGuiParamGestureEnd(*instance, kParamClear);
        return;
    }
    const auto body = static_cast<s3g::AmbiEffectBody>(
        instance->resolvedBody.load(std::memory_order_relaxed));
    const uint32_t count = s3g::ambiEffectBodyPickupCount(body);
    const auto projected = projectBody(body, static_cast<float>(_viewAzDeg),
        static_cast<float>(_viewElDeg));
    for (uint32_t node = 0u; node < count; ++node) {
        const CGFloat dx = point.x - projected.points[node].x;
        const CGFloat dy = point.y - projected.points[node].y;
        if (dx * dx + dy * dy <= 18.0 * 18.0) {
            _selectedPickup = node;
            if ([event clickCount] >= 2) {
                [self setParamOnce:kParamPickupTuneFirst + node value:0.0];
                [self setParamOnce:kParamPickupDecayFirst + node value:0.0];
            }
            [self setNeedsDisplay:YES]; return;
        }
    }
    const auto beginAxis = [&](NSRect axis, clap_id id) {
        if (!NSPointInRect(point, NSInsetRect(axis, 0.0, -8.0))) return false;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
            event, &instance->plugin, id, &defaultValue)) {
            [self setParamOnce:id value:defaultValue];
        } else {
            queueGuiParamGestureBegin(*instance, id);
            _dragSlider = static_cast<int>(id);
            [self updateSliderAtPoint:point];
        }
        return true;
    };
    if (beginAxis(tuneAxisRect(), kParamPickupTuneFirst + _selectedPickup)) return;
    if (beginAxis(decayAxisRect(), kParamPickupDecayFirst + _selectedPickup)) return;

    struct MenuHit { s3g::gui_layout::Rect rect; int menu; uint32_t items; };
    const MenuHit menus[] {
        { s3g::gui_layout::sliderHitRect(kOutputPanel, 1u), 1, 7u },
        { s3g::gui_layout::sliderHitRect(kCapturePanel, 0u), 2, 4u },
        { s3g::gui_layout::sliderHitRect(kCapturePanel, 3u), 3, 11u },
        { s3g::gui_layout::sliderHitRect(kTopologyPanel, 0u), 4, 4u },
    };
    for (const auto& menu : menus) {
        if (!NSPointInRect(point, s3g::clap_gui::cocoaRect(menu.rect))) continue;
        _openMenu = menu.menu;
        _menuItems = menu.items;
        _menuOrigin = NSMakePoint(s3g::gui_layout::processorControlX(
            kOutputPanel.frame.x), menu.rect.y + 24.0);
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES]; return;
    }

    struct SliderHit { s3g::gui_layout::Rect rect; clap_id param; };
    const SliderHit sliders[] {
        { s3g::gui_layout::sliderHitRect(kOutputPanel, 0u), kParamOutput },
        { s3g::gui_layout::sliderHitRect(kCapturePanel, 1u), kParamCaptureSeconds },
        { s3g::gui_layout::sliderHitRect(kCapturePanel, 2u), kParamSensitivity },
        { s3g::gui_layout::sliderHitRect(kResynthesisPanel, 0u), kParamTranspose },
        { s3g::gui_layout::sliderHitRect(kResynthesisPanel, 1u), kParamHarmonicPull },
        { s3g::gui_layout::sliderHitRect(kResynthesisPanel, 2u), kParamHarmonicStretch },
        { s3g::gui_layout::sliderHitRect(kResynthesisPanel, 3u), kParamDecay },
        { s3g::gui_layout::sliderHitRect(kResynthesisPanel, 4u), kParamDecayTilt },
        { s3g::gui_layout::sliderHitRect(kResynthesisPanel, 5u), kParamDrive },
        { s3g::gui_layout::sliderHitRect(kResynthesisPanel, 6u), kParamSpread },
        { s3g::gui_layout::sliderHitRect(kResynthesisPanel, 7u), kParamDeviation },
        { s3g::gui_layout::sliderHitRect(kResynthesisPanel, 8u), kParamMix },
        { s3g::gui_layout::sliderHitRect(kTopologyPanel, 1u), kParamTopologyAmount },
        { s3g::gui_layout::sliderHitRect(kTopologyPanel, 2u), kParamRoamingRate },
        { s3g::gui_layout::sliderHitRect(kMaskPanel, 0u), kParamMaskAmount },
        { s3g::gui_layout::sliderHitRect(kMaskPanel, 1u), kParamMaskAzimuth },
        { s3g::gui_layout::sliderHitRect(kMaskPanel, 2u), kParamMaskElevation },
        { s3g::gui_layout::sliderHitRect(kMaskPanel, 3u), kParamMaskWidth },
        { s3g::gui_layout::sliderHitRect(kMaskPanel, 4u), kParamMaskDry },
    };
    for (const auto& slider : sliders) {
        if (!NSPointInRect(point, s3g::clap_gui::cocoaRect(slider.rect))) continue;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
            event, &instance->plugin, slider.param, &defaultValue)) {
            [self setParamOnce:slider.param value:defaultValue];
        } else {
            queueGuiParamGestureBegin(*instance, slider.param);
            _dragSlider = static_cast<int>(slider.param);
            [self updateSliderAtPoint:point];
        }
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(kFieldPlot))) {
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
    if (_dragSlider >= 0) [self updateSliderAtPoint:point];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu <= 0) return;
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    const CGFloat width = s3g::gui_layout::processorMenuWidth(
        kOutputPanel.frame.width);
    _hoverMenuItem = s3g::clap_gui::dropdownHitIndex(point,
        NSMakeRect(_menuOrigin.x, _menuOrigin.y, width, 18.0 * _menuItems),
        18.0, _menuItems);
    [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (_dragSlider >= 0) {
        queueGuiParamGestureEnd(*static_cast<Plugin*>(_plugin),
            static_cast<clap_id>(_dragSlider));
    }
    _dragSlider = -1;
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
    instance->guiView = [[S3GAmbiEffectResonancePrintView alloc]
        initWithPlugin:instance];
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
    [static_cast<S3GAmbiEffectResonancePrintView*>(instance->guiView)
        stopRefreshTimer];
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
    [static_cast<S3GAmbiEffectResonancePrintView*>(instance->guiView)
        startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return false;
    instance->guiVisible.store(false, std::memory_order_relaxed);
    [static_cast<S3GAmbiEffectResonancePrintView*>(instance->guiView)
        stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        instance->guiViewport, true);
}

const clap_plugin_gui_t guiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy,
    guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints,
    guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient,
    guiSuggestTitle, guiShow, guiHide,
};

} // namespace
