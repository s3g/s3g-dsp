} // namespace

namespace {

constexpr auto kMotionTitleBand = s3g::gui_layout::encoderTitleBand({
    static_cast<double>(kGuiWidth), static_cast<double>(kGuiHeight),
});
static_assert(s3g::gui_layout::processorTitleBandFits(kMotionTitleBand));

constexpr NSRect kSamplePanel = {{ 18.0, 54.0 }, { 944.0, 272.0 }};
constexpr NSRect kWaveRect = {{ 30.0, 82.0 }, { 920.0, 200.0 }};
constexpr NSRect kScopeRect = {{ 18.0, 326.0 }, { 944.0, 100.0 }};
constexpr NSRect kScopeGraphRect = {{ 228.0, 336.0 }, { 722.0, 80.0 }};
constexpr NSRect kMotionPanel = {{ 18.0, 442.0 }, { 300.0, 382.0 }};
constexpr NSRect kMotorPanel = {{ 330.0, 442.0 }, { 300.0, 382.0 }};
constexpr NSRect kOutputPanel = {{ 642.0, 442.0 }, { 320.0, 382.0 }};
constexpr NSRect kSampleLoadButton = {{ 674.0, 59.0 }, { 108.0, 15.0 }};
constexpr NSRect kClearButton = {{ 790.0, 59.0 }, { 52.0, 15.0 }};
constexpr NSRect kStorageButton = {{ 850.0, 59.0 }, { 100.0, 15.0 }};
constexpr NSRect kPreviewButton = {{ 662.0, 744.0 }, { 126.0, 30.0 }};
constexpr NSRect kStopButton = {{ 798.0, 744.0 }, { 144.0, 30.0 }};
constexpr NSRect kOutputPageButton = {{ 834.0, 448.0 }, { 116.0, 16.0 }};
constexpr clap_id kSoundModeUiId = 0xfffffff0u;

constexpr std::array<const char*, 22u> kMotionPresetNames {{
    "INIT", "Hover Field", "Mirror Flip", "Drunk Walk", "Zigzag Cuts",
    "Forward Tape", "Reverse Tape", "Packets", "Motor Hover",
    "Motor Mirror", "Motor Drunk", "Rounded Motor", "Wide Swarm",
    "Moving Loop", "Round Trip", "Routed Zigzag", "Freeze", "Iterate",
    "Pulser", "Doublets", "Bounce", "Routed Iterate",
}};
NSString* const kMotionPresetItems[] = {
    @"INIT", @"Hover Field", @"Mirror Flip", @"Drunk Walk",
    @"Zigzag Cuts", @"Forward Tape", @"Reverse Tape", @"Packets",
    @"Motor Hover", @"Motor Mirror", @"Motor Drunk", @"Rounded Motor",
    @"Wide Swarm", @"Moving Loop", @"Round Trip", @"Routed Zigzag", @"Freeze",
    @"Iterate", @"Pulser", @"Doublets", @"Bounce", @"Routed Iterate",
};
NSString* const kMotionModeItems[] = {
    @"Hover", @"Mirror", @"Drunk", @"Zigzag", @"Forward", @"Reverse",
    @"Moving Loop", @"Round Trip",
};
NSString* const kRateBasisItems[] = { @"Normal", @"Hertz" };
NSString* const kMotorEnvelopeItems[] = {
    @"Linear", @"Rounded", @"Exponential", @"Plateau",
};
NSString* const kSoundModeItems[] = {
    @"Continuous", @"Packets", @"Motor", @"Freeze Loop",
    @"Natural Iterate", @"Pulser", @"Doublets", @"Bounce",
    @"Routed Iterate",
};
NSString* const kSegmentTriggerItems[] = { @"Clock", @"Packet", @"Turn" };
NSString* const kSegmentOverlapItems[] = { @"Cut", @"Layer" };
NSString* const kMotionVoiceItems[] = { @"Poly", @"Mono", @"Legato" };
NSString* const kMotionTriggerItems[] = {
    @"Auto", @"Gate", @"One Shot", @"Toggle",
};
NSString* const kMotionMidiItems[] = {
    @"Omni", @"CH 01", @"CH 02", @"CH 03", @"CH 04", @"CH 05",
    @"CH 06", @"CH 07", @"CH 08", @"CH 09", @"CH 10", @"CH 11",
    @"CH 12", @"CH 13", @"CH 14", @"CH 15", @"CH 16",
};
NSString* const kOutputTraversalItems[] = {
    @"Sequential", @"Reverse Sequential", @"Palindrome", @"Random",
    @"Random Cycle",
};
NSString* const kOutputVoiceWidthItems[] = { @"Mono", @"Stereo Pair" };
NSString* const kStereoPairLayoutItems[] = { @"Adjacent", @"Split Banks" };
NSString* const kOutputAssignmentEventItems[] = {
    @"Note", @"Turn", @"Segment",
};
NSString* const kOffOnItems[] = { @"Off", @"On" };
NSString* const kActiveOutputCountItems[] = {
    @"02", @"03", @"04", @"05", @"06", @"07", @"08", @"09",
    @"10", @"11", @"12", @"13", @"14", @"15", @"16", @"17",
    @"18", @"19", @"20", @"21", @"22", @"23", @"24", @"25",
    @"26", @"27", @"28", @"29", @"30", @"31", @"32",
};
NSString* const kOutputVoicePanelTitle = @"OUTPUT / VOICE / PITCH / MIDI";

struct MotionMenuSpec {
    NSString* const* items = nullptr;
    uint32_t count = 0u;
    uint32_t columns = 1u;
};

struct EventSliderSpec {
    clap_id id = CLAP_INVALID_ID;
    const char* label = "";
};

EventSliderSpec eventSliderSpec(int model, std::size_t row) noexcept
{
    constexpr EventSliderSpec freeze[] {
        { kEventRateParamId, "RATE" },
        { kFieldParamId, "LENGTH" },
        { kEventPitchParamId, "PITCH RAND" },
        { kEventLevelParamId, "AMP RAND" },
        { kJitterParamId, "TIME RAND" },
        { kJoinParamId, "SPLICE" },
    };
    constexpr EventSliderSpec iterate[] {
        { kEventRateParamId, "RATE" },
        { kFieldParamId, "LENGTH" },
        { kEventStepParamId, "STEP" },
        { kEventPitchParamId, "PITCH RAND" },
        { kEventLevelParamId, "AMP RAND" },
        { kJitterParamId, "TIME RAND" },
    };
    constexpr EventSliderSpec pulser[] {
        { kEventRateParamId, "RATE" },
        { kPacketDutyParamId, "DURATION" },
        { kFieldParamId, "SOURCE AREA" },
        { kEventPitchParamId, "PITCH RAND" },
        { kEventLevelParamId, "AMP RAND" },
        { kJitterParamId, "TIME RAND" },
    };
    constexpr EventSliderSpec doublets[] {
        { kMotionRateParamId, "PLAY SPEED" },
        { kEventRepeatsParamId, "REPEATS" },
        { kFieldParamId, "SEGMENT" },
        { kEventStepParamId, "ADVANCE" },
        { kJoinParamId, "SPLICE" },
        { kEventLevelParamId, "AMP RAND" },
    };
    constexpr EventSliderSpec bounce[] {
        { kEventRateParamId, "START RATE" },
        { kEventRepeatsParamId, "BOUNCES" },
        { kFieldParamId, "LENGTH" },
        { kEventLevelParamId, "DECAY" },
        { kEventCurveParamId, "ACCEL" },
        { kPacketDutyParamId, "MIN LENGTH" },
    };
    if (row >= 6u) return {};
    switch (model) {
    case 1: return freeze[row];
    case 2:
    case 6: return iterate[row];
    case 3: return pulser[row];
    case 4: return doublets[row];
    case 5: return bounce[row];
    default: return {};
    }
}

const char* eventModelDescription(int model) noexcept
{
    switch (model) {
    case 1: return "REPEATS ONE LOCUS SLICE / FLUID FREEZE";
    case 2: return "MOVING ITERATIONS / NATURAL TIME, PITCH + LEVEL DRIFT";
    case 3: return "SHORT ENVELOPED PACKETS FROM RANDOM SOURCE POINTS";
    case 4: return "GROUPED SOURCE SLICES / AAABBBCCC";
    case 5: return "ACCELERATING, DECAYING + SHRINKING REPEATS";
    case 6: return "NATURAL ITERATIONS / NEW OUTPUT PER EVENT";
    default: return "EVENT PROCESSING DISABLED / MOTOR REMAINS AVAILABLE";
    }
}

const char* eventSourceDescription(int model, int motion,
    double step) noexcept
{
    switch (model) {
    case 1:
    case 5:
        return "LOCUS LOCK";
    case 2:
    case 6:
        return step <= 0.0 ? "LIVE MOTION CURSOR" : "LOCUS + STEP SEQUENCE";
    case 3:
        return motion == 6 ? "RANDOM IN MOVING FIELD"
            : "RANDOM IN LOCUS FIELD";
    case 4:
        return "LOCUS + STEP GROUPS";
    default:
        return "LIVE MOTION CURSOR";
    }
}

const char* eventMotionRoleDescription(int model, int motion,
    int trigger, double step) noexcept
{
    if (model == 0) return "POSITION + PLAY SPEED + DIRECTION";
    if ((model == 2 || model == 6) && step <= 0.0)
        return trigger == 1
            ? "POSITION / SPEED / DIRECTION / PACKET CLOCK"
            : trigger == 2
            ? "POSITION / SPEED / DIRECTION / TURN CLOCK"
            : "POSITION / SPEED / DIRECTION";
    if (model == 3 && motion == 6)
        return trigger == 1
            ? "MOVING FIELD / SPEED / DIRECTION / PACKET CLOCK"
            : trigger == 2
            ? "MOVING FIELD / SPEED / DIRECTION / TURN CLOCK"
            : "MOVING FIELD / SPEED / DIRECTION";
    return trigger == 1
        ? "PLAY SPEED / ONSET DIRECTION / PACKET CLOCK"
        : trigger == 2
        ? "PLAY SPEED / ONSET DIRECTION / TURN CLOCK"
        : "PLAY SPEED / ONSET DIRECTION";
}

const char* eventCouplingStatus(int model, int motion, int trigger,
    double step) noexcept
{
    if ((model == 2 || model == 6) && step <= 0.0)
        return "FULL PATH FOLLOW";
    if (model == 3 && motion == 6) return "MOVING FIELD";
    if (model == 5 && trigger != 0)
        return "POSITION OVERRIDE / ACCEL NEEDS CLOCK";
    if (model == 4 && trigger != 0)
        return "POSITION OVERRIDE / CLOCK MAKES CONTIGUOUS GROUPS";
    return "POSITION OVERRIDE";
}

bool pathUsesTravel(int motion) noexcept
{
    return motion == 2 || motion == 3;
}

bool sourceStepIsActive(int motion, int model) noexcept
{
    return motion == 6 || model == 2 || model == 4 || model == 6;
}

bool eventSliderIsActive(int model, int trigger, clap_id id) noexcept
{
    if (id == kEventRateParamId)
        return trigger == 0 || model == 3;
    if (id == kEventCurveParamId && model == 5)
        return trigger == 0;
    return true;
}

const char* eventSliderLabel(int model, int trigger,
    const EventSliderSpec& spec) noexcept
{
    if (spec.id != kEventRateParamId) return spec.label;
    if (trigger != 0 && model == 3) return "DURATION BASIS";
    return spec.label;
}

MotionMenuSpec motionMenuSpec(clap_id id) noexcept
{
    switch (id) {
    case kSoundModeUiId: return { kSoundModeItems, 9u, 1u };
    case kMotionParamId: return { kMotionModeItems, 8u, 1u };
    case kRateBasisParamId: return { kRateBasisItems, 2u, 1u };
    case kMotorEnvelopeParamId:
        return { kMotorEnvelopeItems, 4u, 1u };
    case kSegmentTriggerParamId:
        return { kSegmentTriggerItems, 3u, 1u };
    case kEventOverlapParamId:
        return { kSegmentOverlapItems, 2u, 1u };
    case kVoiceModeParamId: return { kMotionVoiceItems, 3u, 1u };
    case kTriggerParamId: return { kMotionTriggerItems, 4u, 1u };
    case kMidiParamId: return { kMotionMidiItems, 17u, 2u };
    case kOutputTraversalParamId:
        return { kOutputTraversalItems, 5u, 1u };
    case kOutputVoiceWidthParamId:
        return { kOutputVoiceWidthItems, 2u, 1u };
    case kStereoPairLayoutParamId:
        return { kStereoPairLayoutItems, 2u, 1u };
    case kOutputAssignmentEventParamId:
        return { kOutputAssignmentEventItems, 3u, 1u };
    case kAvoidAdjacentParamId: return { kOffOnItems, 2u, 1u };
    case kActiveOutputCountParamId:
        return { kActiveOutputCountItems, 31u, 5u };
    default: return {};
    }
}

int soundModeIndex(int segmentModel, int articulation) noexcept
{
    if (segmentModel != 0) return std::clamp(segmentModel + 2, 3, 8);
    if (articulation == 2) return 1;
    if (articulation == 1) return 2;
    return 0;
}

int motionMenuIndex(clap_id id, double value) noexcept
{
    const int rounded = static_cast<int>(std::lround(value));
    return id == kActiveOutputCountParamId ? rounded - 2 : rounded;
}

double motionMenuValue(clap_id id, int index) noexcept
{
    return static_cast<double>(id == kActiveOutputCountParamId
        ? index + 2 : index);
}

double motionParamNormalized(const ParamDef& def, double value) noexcept
{
    if (def.id == kMotionRateParamId || def.id == kInnerRateParamId
        || def.id == kOuterRateParamId || def.id == kEventRateParamId) {
        return std::log(std::max(value, def.minimum) / def.minimum)
            / std::log(def.maximum / def.minimum);
    }
    return (value - def.minimum) / (def.maximum - def.minimum);
}

double motionParamFromNormalized(const ParamDef& def,
    double normalized) noexcept
{
    normalized = std::clamp(normalized, 0.0, 1.0);
    if (def.id == kMotionRateParamId || def.id == kInnerRateParamId
        || def.id == kOuterRateParamId || def.id == kEventRateParamId)
        return def.minimum * std::pow(def.maximum / def.minimum, normalized);
    const double value = def.minimum
        + normalized * (def.maximum - def.minimum);
    return def.stepped ? std::round(value) : value;
}

std::pair<double, double> motionFieldBounds(const Plugin& instance) noexcept
{
    const double start = std::clamp(paramValue(instance, kStartParamId),
        0.0, 1.0);
    const double end = std::clamp(paramValue(instance, kEndParamId),
        start, 1.0);
    const double locus = std::clamp(paramValue(instance, kLocusParamId),
        start, end);
    const double half = paramValue(instance, kFieldParamId) * 0.5;
    return { std::max(start, locus - half), std::min(end, locus + half) };
}

uint32_t motionPreviewRandom(uint32_t& randomState) noexcept
{
    if (randomState == 0u) randomState = 1u;
    randomState ^= randomState << 13u;
    randomState ^= randomState >> 17u;
    randomState ^= randomState << 5u;
    return randomState;
}

} // namespace

@interface S3GSampleMotionCursorView : NSView {
@private
    Plugin* _instance;
    CALayer* _heads[s3g::sample::kMaximumMotionVoices];
    CAShapeLayer* _headLines[s3g::sample::kMaximumMotionVoices];
    CATextLayer* _headFlags[s3g::sample::kMaximumMotionVoices];
    uint64_t _identities[s3g::sample::kMaximumMotionVoices];
    double _viewStart;
    double _viewSpan;
    NSUInteger _animationInstallCount;
}
- (instancetype)initWithPlugin:(Plugin*)instance frame:(NSRect)frame;
- (void)setViewStart:(double)start span:(double)span;
- (void)updateTrajectory;
- (void)invalidateTrajectory;
- (NSUInteger)motionAnimationCount;
- (NSUInteger)animationInstallCount;
- (double)firstLineHeight;
@end

@implementation S3GSampleMotionCursorView

- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return NO; }
- (NSView*)hitTest:(NSPoint)point { (void)point; return nil; }

- (instancetype)initWithPlugin:(Plugin*)instance frame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (!self) return nil;
    _instance = instance;
    _viewStart = 0.0;
    _viewSpan = 1.0;
    [self setWantsLayer:YES];
    self.layer.masksToBounds = YES;
    self.layer.backgroundColor = [[NSColor clearColor] CGColor];
    const CGFloat scale = [NSScreen mainScreen]
        ? [[NSScreen mainScreen] backingScaleFactor] : 2.0;
    const auto style = s3g::clap_gui::softTextStyle();
    for (std::size_t slot = 0u; slot < std::size(_heads); ++slot) {
        CALayer* head = [CALayer layer];
        head.bounds = CGRectMake(0.0, 0.0, 1.0, NSHeight(frame));
        head.anchorPoint = CGPointMake(0.5, 0.5);
        head.position = CGPointMake(-2.0, NSHeight(frame) * 0.5);
        head.masksToBounds = NO;
        head.hidden = YES;
        CAShapeLayer* line = [CAShapeLayer layer];
        line.frame = head.bounds;
        CGMutablePathRef path = CGPathCreateMutable();
        CGPathMoveToPoint(path, nullptr, 0.5, 0.0);
        CGPathAddLineToPoint(path, nullptr, 0.5, NSHeight(frame));
        line.path = path;
        CGPathRelease(path);
        line.fillColor = nullptr;
        line.lineWidth = 1.25;
        CATextLayer* flag = [CATextLayer layer];
        flag.bounds = CGRectMake(0.0, 0.0, 68.0, 14.0);
        flag.anchorPoint = CGPointMake(0.5, 0.5);
        flag.font = (__bridge CFTypeRef)@"Menlo";
        flag.fontSize = 9.0;
        flag.alignmentMode = kCAAlignmentCenter;
        flag.contentsScale = scale;
        flag.backgroundColor = [s3g::clap_gui::color(0x111111, 0.94)
            CGColor];
        NSColor* tint = slot % 3u == 0u ? style.fill
            : (slot % 3u == 1u ? style.accent : style.dim);
        line.strokeColor = tint.CGColor;
        flag.foregroundColor = tint.CGColor;
        [head addSublayer:line];
        [head addSublayer:flag];
        [self.layer addSublayer:head];
        _heads[slot] = head;
        _headLines[slot] = line;
        _headFlags[slot] = flag;
    }
    return self;
}

- (void)setViewStart:(double)start span:(double)span
{
    span = std::clamp(span, 1.0 / 128.0, 1.0);
    _viewStart = std::clamp(start, 0.0, 1.0 - span);
    _viewSpan = span;
}

- (void)invalidateTrajectory
{
    for (std::size_t slot = 0u; slot < std::size(_heads); ++slot) {
        _identities[slot] = 0u;
        [_heads[slot] removeAllAnimations];
        _heads[slot].hidden = YES;
    }
}

- (void)updateTrajectory
{
    if (!_instance) return;
    const uint32_t count = std::min<uint32_t>(
        _instance->cursorCount.load(std::memory_order_acquire),
        static_cast<uint32_t>(std::size(_heads)));
    const auto sourceX = [&](double normalized) {
        return NSWidth(self.bounds) * (normalized - _viewStart) / _viewSpan;
    };
    for (std::size_t slot = 0u; slot < std::size(_heads); ++slot) {
        CALayer* head = _heads[slot];
        if (slot >= count) {
            _identities[slot] = 0u;
            [head removeAllAnimations];
            head.hidden = YES;
            continue;
        }
        const uint64_t identity = _instance->cursorIdentities[slot].load(
            std::memory_order_relaxed);
        const double observed = _instance->cursorPositions[slot].load(
            std::memory_order_relaxed);
        const CGFloat destination = sourceX(observed);
        const CALayer* presentation = [head presentationLayer];
        const CGFloat origin = !head.hidden && presentation
            ? presentation.position.x : destination;
        const BOOL flagLeft = destination > NSWidth(self.bounds) - 82.0;
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        head.hidden = NO;
        head.position = CGPointMake(destination, NSMidY(self.bounds));
        const unsigned key = static_cast<unsigned>(
            _instance->cursorKeys[slot].load(std::memory_order_relaxed));
        const unsigned first = static_cast<unsigned>(
            _instance->cursorOutputFirst[slot].load(
                std::memory_order_relaxed)) + 1u;
        const unsigned second = static_cast<unsigned>(
            _instance->cursorOutputSecond[slot].load(
                std::memory_order_relaxed)) + 1u;
        const unsigned width = static_cast<unsigned>(
            _instance->cursorOutputWidths[slot].load(
                std::memory_order_relaxed));
        _headFlags[slot].string = width > 1u
            ? [NSString stringWithFormat:@"N%u>%02u/%02u", key, first,
                second]
            : [NSString stringWithFormat:@"N%u>%02u", key, first];
        _headFlags[slot].position = CGPointMake(flagLeft ? -36.0 : 36.0,
            23.0 + static_cast<CGFloat>(slot % 10u) * 15.0);
        [CATransaction commit];
        CABasicAnimation* animation = [CABasicAnimation
            animationWithKeyPath:@"position.x"];
        animation.fromValue = @(origin);
        animation.toValue = @(destination);
        animation.duration = 1.0 / 30.0;
        animation.timingFunction = [CAMediaTimingFunction
            functionWithName:kCAMediaTimingFunctionLinear];
        animation.removedOnCompletion = YES;
        [head addAnimation:animation forKey:@"s3g.cursor.motion"];
        if (_identities[slot] != identity) ++_animationInstallCount;
        _identities[slot] = identity;
    }
}

- (NSUInteger)motionAnimationCount
{
    NSUInteger count = 0u;
    for (CALayer* head : _heads) if (!head.hidden) ++count;
    return count;
}
- (NSUInteger)animationInstallCount { return _animationInstallCount; }
- (double)firstLineHeight
{
    const CGRect bounds = _headLines[0u].path
        ? CGPathGetBoundingBox(_headLines[0u].path) : CGRectZero;
    return CGRectGetHeight(bounds);
}

@end

@interface S3GSampleMotionView : NSView <NSDraggingDestination> {
@private
    Plugin* _instance;
    NSTimer* _timer;
    S3GSampleMotionCursorView* _cursorView;
    clap_id _dragParam;
    NSRect _dragPanel;
    clap_id _openMenuParam;
    NSInteger _presetIndex;
    BOOL _presetMenuOpen;
    int _menuHover;
    NSTrackingArea* _trackingArea;
    uint32_t _feedbackMask;
    double _feedbackUntil;
    double _waveZoom;
    double _waveViewStart;
    BOOL _waveDragging;
    BOOL _outputRoutingPage;
    const SampleAsset* _displayedAsset;
    char _presetName[64];
}
- (instancetype)initWithPlugin:(Plugin*)instance;
- (void)startTimer;
- (void)stopTimer;
- (NSUInteger)cursorMotionAnimationCount;
- (NSUInteger)cursorAnimationInstallCount;
- (double)cursorLineHeight;
- (BOOL)canvasMenuOpen;
- (int)canvasMenuHover;
- (double)waveZoomValue;
- (double)waveViewStartValue;
- (unsigned)scopeFocusedKey;
- (BOOL)outputRoutingPage;
- (BOOL)eventPage;
- (BOOL)loadDocumentationSample;
@end

@implementation S3GSampleMotionView

- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (instancetype)initWithPlugin:(Plugin*)instance
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (!self) return nil;
    _instance = instance;
    _dragParam = CLAP_INVALID_ID;
    _openMenuParam = CLAP_INVALID_ID;
    _menuHover = -1;
    _waveZoom = 1.0;
    _waveViewStart = 0.0;
    _outputRoutingPage = NO;
    _displayedAsset = instance ? instance->controlAsset.get() : nullptr;
    std::snprintf(_presetName, sizeof(_presetName), "%s", "INIT");
    _cursorView = [[S3GSampleMotionCursorView alloc]
        initWithPlugin:instance frame:kWaveRect];
    [self addSubview:_cursorView];
    [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    [self startTimer];
    return self;
}

- (void)updateTrackingAreas
{
    if (_trackingArea) [self removeTrackingArea:_trackingArea];
    _trackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds
        options:(NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited
            | NSTrackingActiveAlways | NSTrackingInVisibleRect)
        owner:self userInfo:nil];
    [self addTrackingArea:_trackingArea];
    [super updateTrackingAreas];
}

- (void)startTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 30.0 target:self
        selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopTimer
{
    [_timer invalidate];
    _timer = nil;
    [_cursorView invalidateTrajectory];
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    serviceLoads(*_instance);
    const SampleAsset* asset = _instance->controlAsset.get();
    if (asset != _displayedAsset) {
        _displayedAsset = asset;
        _waveZoom = 1.0;
        _waveViewStart = 0.0;
        [_cursorView setViewStart:0.0 span:1.0];
        [_cursorView invalidateTrajectory];
    }
    const uint32_t feedback = _instance->actionFeedback.exchange(0u,
        std::memory_order_acq_rel);
    if (feedback != 0u) {
        _feedbackMask |= feedback;
        _feedbackUntil = CACurrentMediaTime() + 0.12;
    }
    if (_feedbackMask != 0u && CACurrentMediaTime() >= _feedbackUntil)
        _feedbackMask = 0u;
    [_cursorView updateTrajectory];
    [self setNeedsDisplay:YES];
}

- (NSUInteger)cursorMotionAnimationCount
{ return [_cursorView motionAnimationCount]; }
- (NSUInteger)cursorAnimationInstallCount
{ return [_cursorView animationInstallCount]; }
- (double)cursorLineHeight { return [_cursorView firstLineHeight]; }
- (BOOL)canvasMenuOpen
{ return _presetMenuOpen || _openMenuParam != CLAP_INVALID_ID; }
- (int)canvasMenuHover { return _menuHover; }
- (double)waveZoomValue { return _waveZoom; }
- (double)waveViewStartValue { return _waveViewStart; }
- (BOOL)outputRoutingPage { return _outputRoutingPage; }
- (BOOL)eventPage
{ return paramValue(*_instance, kSegmentModelParamId) != 0.0; }

- (uint32_t)newestVoiceCursorSlot
{
    const uint32_t count = std::min<uint32_t>(
        _instance->cursorCount.load(std::memory_order_acquire),
        static_cast<uint32_t>(_instance->cursorIdentities.size()));
    uint32_t newest = std::numeric_limits<uint32_t>::max();
    uint64_t identity = 0u;
    for (uint32_t slot = 0u; slot < count; ++slot) {
        const uint64_t candidate = _instance->cursorIdentities[slot].load(
            std::memory_order_relaxed);
        if (newest == std::numeric_limits<uint32_t>::max()
            || candidate > identity) {
            newest = slot;
            identity = candidate;
        }
    }
    return newest;
}

- (unsigned)scopeFocusedKey
{
    const uint32_t slot = [self newestVoiceCursorSlot];
    return slot == std::numeric_limits<uint32_t>::max() ? 0u
        : static_cast<unsigned>(_instance->cursorKeys[slot].load(
            std::memory_order_relaxed));
}

- (double)waveVisibleSpan
{ return 1.0 / std::clamp(_waveZoom, 1.0, 128.0); }

- (double)waveNormalizedAtPoint:(NSPoint)point
{
    const double local = std::clamp(static_cast<double>(
        (point.x - NSMinX(kWaveRect)) / NSWidth(kWaveRect)), 0.0, 1.0);
    return std::clamp(_waveViewStart + local * [self waveVisibleSpan],
        0.0, 1.0);
}

- (CGFloat)waveXForNormalized:(double)value
{
    return NSMinX(kWaveRect) + static_cast<CGFloat>(
        (value - _waveViewStart) / [self waveVisibleSpan])
        * NSWidth(kWaveRect);
}

- (void)openSample
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setAllowedFileTypes:@[ @"wav", @"aif", @"aiff", @"caf",
        @"m4a", @"mp3", @"flac" ]];
    if ([panel runModal] == NSModalResponseOK)
        queueSampleLoad(*_instance, std::string([[[panel URL] path]
            UTF8String]));
}

- (void)applyPreset:(NSInteger)index
{
    _presetIndex = std::clamp<NSInteger>(index, 0,
        static_cast<NSInteger>(kMotionPresetNames.size() - 1u));
    const auto set = [&](clap_id id, double value) {
        queueGuiParamGesture(*_instance, id, value);
    };
    for (const auto& def : kParamDefs) set(def.id, def.defaultValue);
    switch (_presetIndex) {
    case 1: set(kFieldParamId, 0.32); set(kMotionRateParamId, 0.55); break;
    case 2:
        set(kMotionParamId, 1.0); set(kFieldParamId, 0.46);
        set(kMotionRateParamId, 0.80); set(kJoinParamId, 0.72); break;
    case 3:
        set(kMotionParamId, 2.0); set(kFieldParamId, 0.55);
        set(kMotionRateParamId, 0.65); set(kTravelParamId, 0.62);
        set(kJitterParamId, 0.38); set(kSeedParamId, 4312.0); break;
    case 4:
        set(kMotionParamId, 3.0); set(kFieldParamId, 0.72);
        set(kMotionRateParamId, 1.0); set(kTravelParamId, 0.48);
        set(kJitterParamId, 0.35); set(kJoinParamId, 0.78);
        set(kSeedParamId, 4312.0); break;
    case 5:
        set(kMotionParamId, 4.0); set(kFieldParamId, 0.76);
        set(kMotionRateParamId, 1.0); set(kJoinParamId, 0.86); break;
    case 6:
        set(kMotionParamId, 5.0); set(kFieldParamId, 0.76);
        set(kMotionRateParamId, 1.0); set(kJoinParamId, 0.86); break;
    case 7:
        set(kMotionParamId, 3.0); set(kArticulationParamId, 2.0);
        set(kMotionRateParamId, 0.75); set(kTravelParamId, 0.40);
        set(kInnerRateParamId, 9.0); set(kPacketDutyParamId, 0.52);
        set(kJitterParamId, 0.25); break;
    case 8:
        set(kArticulationParamId, 1.0); set(kMotionRateParamId, 0.50);
        set(kInnerRateParamId, 18.0); set(kOuterRateParamId, 0.7);
        set(kMotorEnvelopeParamId, 1.0); break;
    case 9:
        set(kMotionParamId, 1.0); set(kArticulationParamId, 1.0);
        set(kMotionRateParamId, 0.70); set(kInnerRateParamId, 24.0);
        set(kOuterRateParamId, 1.2); set(kSymmetryParamId, 0.38);
        set(kMotorEnvelopeParamId, 2.0); break;
    case 10:
        set(kMotionParamId, 2.0); set(kArticulationParamId, 1.0);
        set(kFieldParamId, 0.58); set(kMotionRateParamId, 0.60);
        set(kTravelParamId, 0.72); set(kJitterParamId, 0.68);
        set(kInnerRateParamId, 19.0); set(kOuterRateParamId, 1.3);
        set(kPacketDutyParamId, 0.58); set(kSymmetryParamId, 0.37);
        set(kJoinParamId, 0.84); set(kSeedParamId, 4312.0); break;
    case 11:
        set(kArticulationParamId, 1.0); set(kMotionRateParamId, 0.50);
        set(kInnerRateParamId, 14.0); set(kOuterRateParamId, 0.55);
        set(kMotorEnvelopeParamId, 1.0); set(kSymmetryParamId, 0.42);
        break;
    case 12:
        set(kVoiceModeParamId, 0.0); set(kFieldParamId, 0.82);
        set(kMotionRateParamId, 0.24); set(kAttackParamId, 0.08);
        set(kReleaseParamId, 0.35); break;
    case 13:
        set(kMotionParamId, 6.0); set(kFieldParamId, 0.18);
        set(kMotionRateParamId, 1.0); set(kEventStepParamId, 0.025);
        set(kJoinParamId, 0.86); break;
    case 14:
        set(kMotionParamId, 7.0); set(kFieldParamId, 0.48);
        set(kMotionRateParamId, 0.85); set(kJoinParamId, 0.72); break;
    case 15:
        set(kMotionParamId, 3.0); set(kFieldParamId, 0.70);
        set(kMotionRateParamId, 1.0); set(kTravelParamId, 0.48);
        set(kJitterParamId, 0.28); set(kOutputTraversalParamId, 4.0);
        set(kOutputVoiceWidthParamId, 0.0);
        set(kOutputAssignmentEventParamId, 1.0);
        set(kAvoidAdjacentParamId, 1.0); break;
    case 16:
        set(kSegmentModelParamId, 1.0); set(kEventRateParamId, 6.0);
        set(kEventRepeatsParamId, 8.0); set(kFieldParamId, 0.08);
        set(kEventStepParamId, 0.0); set(kEventOverlapParamId, 1.0);
        set(kEventPitchParamId, 0.08); set(kEventLevelParamId, 0.05);
        set(kJitterParamId, 0.08);
        break;
    case 17:
        set(kSegmentModelParamId, 2.0); set(kEventRateParamId, 5.0);
        set(kFieldParamId, 0.16); set(kEventStepParamId, 0.0);
        set(kEventPitchParamId, 0.18); set(kEventLevelParamId, 0.12);
        set(kJitterParamId, 0.12); set(kJoinParamId, 0.92);
        set(kEventOverlapParamId, 1.0);
        break;
    case 18:
        set(kSegmentModelParamId, 3.0); set(kEventRateParamId, 12.0);
        set(kFieldParamId, 0.35); set(kPacketDutyParamId, 0.24);
        set(kEventOverlapParamId, 1.0); set(kEventPitchParamId, 1.0);
        set(kEventLevelParamId, 0.25); set(kJitterParamId, 0.20);
        set(kMotorEnvelopeParamId, 3.0); set(kJoinParamId, 1.0); break;
    case 19:
        set(kSegmentModelParamId, 4.0); set(kMotionRateParamId, 1.0);
        set(kEventRepeatsParamId, 3.0); set(kFieldParamId, 0.10);
        set(kEventStepParamId, 0.10); set(kEventLevelParamId, 0.0);
        set(kJitterParamId, 0.0); set(kJoinParamId, 0.88);
        break;
    case 20:
        set(kSegmentModelParamId, 5.0); set(kEventRateParamId, 2.0);
        set(kEventRepeatsParamId, 10.0); set(kFieldParamId, 0.18);
        set(kEventCurveParamId, 0.22); set(kEventLevelParamId, 0.18);
        set(kPacketDutyParamId, 0.25); set(kEventOverlapParamId, 1.0);
        break;
    case 21:
        set(kSegmentModelParamId, 6.0); set(kEventRateParamId, 6.0);
        set(kFieldParamId, 0.14); set(kEventStepParamId, 0.0);
        set(kEventPitchParamId, 0.20); set(kEventLevelParamId, 0.12);
        set(kJitterParamId, 0.12); set(kEventOverlapParamId, 1.0);
        set(kOutputTraversalParamId, 4.0);
        set(kOutputVoiceWidthParamId, 0.0);
        set(kOutputAssignmentEventParamId, 2.0);
        set(kAvoidAdjacentParamId, 1.0); break;
    default: break;
    }
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        kMotionPresetNames[static_cast<std::size_t>(_presetIndex)]);
    markStateDirty(*_instance);
}

- (void)applyEventModelRecipe:(NSInteger)index
{
    index = std::clamp<NSInteger>(index, 0, 6);
    const auto set = [&](clap_id id, double value) {
        queueGuiParamGesture(*_instance, id, value);
    };
    set(kSegmentModelParamId, static_cast<double>(index));
    switch (index) {
    case 1: // Freeze: one short locus slice with restrained variation.
        set(kSegmentTriggerParamId, 0.0);
        set(kEventRateParamId, 6.0);
        set(kEventRepeatsParamId, 8.0);
        set(kFieldParamId, 0.08);
        set(kEventStepParamId, 0.0);
        set(kEventPitchParamId, 0.08);
        set(kEventLevelParamId, 0.05);
        set(kEventCurveParamId, 0.0);
        set(kJitterParamId, 0.08);
        set(kJoinParamId, 1.0);
        set(kEventOverlapParamId, 1.0);
        break;
    case 2: // Iterate: follow motion and naturalise successive copies.
        set(kSegmentTriggerParamId, 0.0);
        set(kEventRateParamId, 5.0);
        set(kFieldParamId, 0.16);
        set(kEventStepParamId, 0.0);
        set(kEventPitchParamId, 0.18);
        set(kEventLevelParamId, 0.12);
        set(kEventCurveParamId, 0.0);
        set(kJitterParamId, 0.12);
        set(kJoinParamId, 0.92);
        set(kEventOverlapParamId, 1.0);
        break;
    case 3: // Pulser: short shaped packets selected across the field.
        set(kSegmentTriggerParamId, 0.0);
        set(kEventRateParamId, 12.0);
        set(kPacketDutyParamId, 0.24);
        set(kFieldParamId, 0.35);
        set(kEventPitchParamId, 1.0);
        set(kEventLevelParamId, 0.25);
        set(kEventCurveParamId, 0.0);
        set(kJitterParamId, 0.20);
        set(kMotorEnvelopeParamId, 3.0);
        set(kJoinParamId, 1.0);
        set(kEventOverlapParamId, 1.0);
        break;
    case 4: // Doublets: contiguous AAABBBCCC-style slice groups.
        set(kSegmentTriggerParamId, 0.0);
        set(kMotionRateParamId, 1.0);
        set(kEventRepeatsParamId, 3.0);
        set(kFieldParamId, 0.08);
        set(kEventStepParamId, 0.08);
        set(kEventPitchParamId, 0.0);
        set(kEventLevelParamId, 0.0);
        set(kEventCurveParamId, 0.0);
        set(kJitterParamId, 0.0);
        set(kJoinParamId, 0.88);
        set(kEventOverlapParamId, 0.0);
        break;
    case 5: // Bounce: each hit becomes faster, quieter, and shorter.
        set(kSegmentTriggerParamId, 0.0);
        set(kEventRateParamId, 2.0);
        set(kEventRepeatsParamId, 10.0);
        set(kFieldParamId, 0.18);
        set(kEventStepParamId, 0.0);
        set(kEventPitchParamId, 0.0);
        set(kEventLevelParamId, 0.18);
        set(kEventCurveParamId, 0.22);
        set(kPacketDutyParamId, 0.25);
        set(kJitterParamId, 0.0);
        set(kJoinParamId, 0.90);
        set(kEventOverlapParamId, 1.0);
        break;
    case 6: // Routed Iterate: natural copies, random-cycle spatial allocation.
        set(kSegmentTriggerParamId, 0.0);
        set(kEventRateParamId, 6.0);
        set(kFieldParamId, 0.14);
        set(kEventStepParamId, 0.0);
        set(kEventPitchParamId, 0.20);
        set(kEventLevelParamId, 0.12);
        set(kEventCurveParamId, 0.0);
        set(kJitterParamId, 0.12);
        set(kJoinParamId, 0.92);
        set(kEventOverlapParamId, 1.0);
        if (isMultichannel(*_instance)) {
            set(kOutputTraversalParamId, 4.0);
            set(kOutputVoiceWidthParamId, 0.0);
            set(kOutputAssignmentEventParamId, 2.0);
            set(kAvoidAdjacentParamId, 1.0);
        }
        break;
    default:
        break;
    }
    _presetIndex = -1;
    std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
    markStateDirty(*_instance);
}

- (void)applySoundMode:(NSInteger)index
{
    index = std::clamp<NSInteger>(index, 0, 8);
    if (index >= 3) {
        [self applyEventModelRecipe:index - 2];
        return;
    }
    const double articulation = index == 1 ? 2.0
        : index == 2 ? 1.0 : 0.0;
    queueGuiParamGesture(*_instance, kSegmentModelParamId, 0.0);
    queueGuiParamGesture(*_instance, kArticulationParamId, articulation);
    _presetIndex = -1;
    std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
    markStateDirty(*_instance);
}

- (NSRect)menuRectForParam:(clap_id)id
{
    struct Row { clap_id id; NSRect panel; CGFloat y; };
    const Row rows[] = {
        { kMotionParamId, kMotionPanel, 482.0 },
        { kRateBasisParamId, kMotionPanel, 607.0 },
    };
    for (const auto& row : rows) if (row.id == id)
        return NSMakeRect(row.panel.origin.x + 104.0, row.y - 2.0,
            row.panel.size.width - 116.0, 18.0);
    if (id == kSoundModeUiId)
        return NSMakeRect(kMotorPanel.origin.x + 104.0, 480.0,
            kMotorPanel.size.width - 116.0, 18.0);
    const int selectedModel = static_cast<int>(std::lround(paramValue(
        *_instance, kSegmentModelParamId)));
    const bool eventActive = selectedModel != 0;
    const int articulation = static_cast<int>(std::lround(paramValue(
        *_instance, kArticulationParamId)));
    const Row motorRows[] = {
        { kMotorEnvelopeParamId, kMotorPanel, 507.0 },
    };
    if (eventActive) {
        const int model = static_cast<int>(std::lround(paramValue(
            *_instance, kSegmentModelParamId)));
        const Row eventRows[] = {
            { kSegmentTriggerParamId, kMotorPanel, 507.0 },
            { kMotorEnvelopeParamId, kMotorPanel, 682.0 },
            { kEventOverlapParamId, kMotorPanel,
                model == 3 ? 707.0 : 682.0 },
        };
        for (std::size_t index = 0u; index < std::size(eventRows); ++index) {
            const auto& row = eventRows[index];
            if (row.id == kMotorEnvelopeParamId && model != 3) continue;
            if (row.id == id) return NSMakeRect(row.panel.origin.x + 104.0,
                row.y - 2.0, row.panel.size.width - 116.0, 18.0);
        }
    } else {
        for (const auto& row : motorRows)
            if (articulation == 1 && row.id == id) return NSMakeRect(
                row.panel.origin.x + 104.0, row.y - 2.0,
                row.panel.size.width - 116.0, 18.0);
    }
    const Row voiceRows[] = {
        { kVoiceModeParamId, kOutputPanel, 507.0 },
        { kTriggerParamId, kOutputPanel, 532.0 },
        { kMidiParamId, kOutputPanel, 707.0 },
    };
    const Row routingRows[] = {
        { kOutputTraversalParamId, kOutputPanel, 507.0 },
        { kOutputVoiceWidthParamId, kOutputPanel, 532.0 },
        { kStereoPairLayoutParamId, kOutputPanel, 557.0 },
        { kActiveOutputCountParamId, kOutputPanel, 582.0 },
        { kOutputAssignmentEventParamId, kOutputPanel, 607.0 },
        { kAvoidAdjacentParamId, kOutputPanel, 632.0 },
    };
    const Row* outputRows = _outputRoutingPage ? routingRows : voiceRows;
    const std::size_t outputRowCount = _outputRoutingPage
        ? std::size(routingRows) : std::size(voiceRows);
    for (std::size_t index = 0u; index < outputRowCount; ++index) {
        const auto& row = outputRows[index];
        if (row.id == id) return NSMakeRect(row.panel.origin.x + 104.0,
            row.y - 2.0, row.panel.size.width - 116.0, 18.0);
    }
    return NSZeroRect;
}

- (NSRect)openMenuRect
{
    constexpr CGFloat rowHeight = 20.0;
    const NSRect presetAnchor = s3g::clap_gui::cocoaRect(
        kMotionTitleBand.presetMenu);
    if (_presetMenuOpen) return NSMakeRect(NSMinX(presetAnchor),
        NSMaxY(presetAnchor), NSWidth(presetAnchor),
        rowHeight * std::size(kMotionPresetItems));
    const MotionMenuSpec spec = motionMenuSpec(_openMenuParam);
    const NSRect anchor = [self menuRectForParam:_openMenuParam];
    const uint32_t rows = spec.count == 0u ? 0u
        : (spec.count + spec.columns - 1u) / spec.columns;
    return rows == 0u ? NSZeroRect : NSMakeRect(NSMinX(anchor),
        NSMaxY(anchor), NSWidth(anchor), rowHeight * rows);
}

- (void)closeMenus
{
    _presetMenuOpen = NO;
    _openMenuParam = CLAP_INVALID_ID;
    _menuHover = -1;
    [_cursorView setHidden:NO];
    [_cursorView updateTrajectory];
}

- (clap_id)sliderAtPoint:(NSPoint)point panel:(NSRect*)panelOut
{
    struct Row { clap_id id; NSRect panel; CGFloat y; };
    const Row rows[] = {
        { kStartParamId, kMotionPanel, 507.0 },
        { kEndParamId, kMotionPanel, 532.0 },
        { kLocusParamId, kMotionPanel, 557.0 },
        { kFieldParamId, kMotionPanel, 582.0 },
        { kMotionRateParamId, kMotionPanel, 632.0 },
        { kTravelParamId, kMotionPanel, 657.0 },
        { kJitterParamId, kMotionPanel, 682.0 },
        { kEventStepParamId, kMotionPanel, 707.0 },
    };
    const int motion = static_cast<int>(std::lround(paramValue(
        *_instance, kMotionParamId)));
    const int selectedModel = static_cast<int>(std::lround(paramValue(
        *_instance, kSegmentModelParamId)));
    for (const auto& row : rows) {
        if (row.id == kTravelParamId && !pathUsesTravel(motion)) continue;
        if (row.id == kEventStepParamId
            && !sourceStepIsActive(motion, selectedModel)) continue;
        const NSRect hit = NSMakeRect(row.panel.origin.x + 8.0,
            row.y - 10.0, row.panel.size.width - 16.0, 22.0);
        if (NSPointInRect(point, hit)) {
            if (panelOut) *panelOut = row.panel;
            return row.id;
        }
    }
    const bool eventActive = selectedModel != 0;
    if (eventActive) {
        const int model = selectedModel;
        const int trigger = static_cast<int>(std::lround(paramValue(
            *_instance, kSegmentTriggerParamId)));
        for (std::size_t index = 0u; index < 6u; ++index) {
            const auto spec = eventSliderSpec(model, index);
            if (spec.id == CLAP_INVALID_ID) continue;
            if (!eventSliderIsActive(model, trigger, spec.id)) continue;
            const CGFloat y = 532.0 + static_cast<CGFloat>(index) * 25.0;
            const NSRect hit = NSMakeRect(kMotorPanel.origin.x + 8.0,
                y - 10.0, kMotorPanel.size.width - 16.0, 22.0);
            if (NSPointInRect(point, hit)) {
                if (panelOut) *panelOut = kMotorPanel;
                return spec.id;
            }
        }
        if (model != 0 && model != 3) {
            const NSRect hit = NSMakeRect(kMotorPanel.origin.x + 8.0,
                697.0, kMotorPanel.size.width - 16.0, 22.0);
            if (NSPointInRect(point, hit)) {
                if (panelOut) *panelOut = kMotorPanel;
                return kSeedParamId;
            }
        }
        if (trigger == 1) {
            const NSRect hit = NSMakeRect(kMotorPanel.origin.x + 8.0,
                722.0, kMotorPanel.size.width - 16.0, 22.0);
            if (NSPointInRect(point, hit)) {
                if (panelOut) *panelOut = kMotorPanel;
                return kInnerRateParamId;
            }
        }
    } else {
        const int articulation = static_cast<int>(std::lround(paramValue(
            *_instance, kArticulationParamId)));
        const Row continuousRows[] = {
            { kJoinParamId, kMotorPanel, 507.0 },
            { kSeedParamId, kMotorPanel, 532.0 },
        };
        const Row packetRows[] = {
            { kInnerRateParamId, kMotorPanel, 507.0 },
            { kPacketDutyParamId, kMotorPanel, 532.0 },
            { kJoinParamId, kMotorPanel, 557.0 },
            { kSeedParamId, kMotorPanel, 582.0 },
        };
        const Row motorRows[] = {
            { kInnerRateParamId, kMotorPanel, 532.0 },
            { kOuterRateParamId, kMotorPanel, 557.0 },
            { kPacketDutyParamId, kMotorPanel, 582.0 },
            { kSymmetryParamId, kMotorPanel, 607.0 },
            { kJoinParamId, kMotorPanel, 632.0 },
            { kSeedParamId, kMotorPanel, 657.0 },
        };
        const Row* directRows = articulation == 1 ? motorRows
            : articulation == 2 ? packetRows : continuousRows;
        const std::size_t directRowCount = articulation == 1
            ? std::size(motorRows) : articulation == 2
                ? std::size(packetRows) : std::size(continuousRows);
        for (std::size_t index = 0u; index < directRowCount; ++index) {
            const auto& row = directRows[index];
            const NSRect hit = NSMakeRect(row.panel.origin.x + 8.0,
                row.y - 10.0, row.panel.size.width - 16.0, 22.0);
            if (NSPointInRect(point, hit)) {
                if (panelOut) *panelOut = row.panel;
                return row.id;
            }
        }
    }
    const Row voiceRows[] = {
        { kOutParamId, kOutputPanel, 482.0 },
        { kRootParamId, kOutputPanel, 557.0 },
        { kTuneParamId, kOutputPanel, 582.0 },
        { kFineParamId, kOutputPanel, 607.0 },
        { kAttackParamId, kOutputPanel, 632.0 },
        { kReleaseParamId, kOutputPanel, 657.0 },
        { kVelocityParamId, kOutputPanel, 682.0 },
    };
    const Row routingRows[] = {
        { kOutParamId, kOutputPanel, 482.0 },
        { kAttackParamId, kOutputPanel, 657.0 },
        { kReleaseParamId, kOutputPanel, 682.0 },
    };
    const Row* outputRows = _outputRoutingPage ? routingRows : voiceRows;
    const std::size_t outputRowCount = _outputRoutingPage
        ? std::size(routingRows) : std::size(voiceRows);
    for (std::size_t index = 0u; index < outputRowCount; ++index) {
        const auto& row = outputRows[index];
        const NSRect hit = NSMakeRect(row.panel.origin.x + 8.0,
            row.y - 10.0, row.panel.size.width - 16.0, 22.0);
        if (NSPointInRect(point, hit)) {
            if (panelOut) *panelOut = row.panel;
            return row.id;
        }
    }
    return CLAP_INVALID_ID;
}

- (void)setDraggedParam:(clap_id)id point:(NSPoint)point panel:(NSRect)panel
{
    const ParamDef* def = paramDef(id);
    if (!def) return;
    const CGFloat left = panel.origin.x + 108.0;
    const CGFloat width = panel.size.width - 168.0;
    const double normalized = std::clamp(static_cast<double>(
        (point.x - left) / width), 0.0, 1.0);
    queueGuiParamValue(*_instance, id,
        motionParamFromNormalized(*def, normalized));
}

- (void)setRangeParam:(clap_id)id normalized:(double)value
{
    constexpr double epsilon = 1.0e-5;
    if (id == kStartParamId) {
        value = std::min(value,
            paramValue(*_instance, kEndParamId) - epsilon);
        if (paramValue(*_instance, kLocusParamId) < value)
            queueGuiParamValue(*_instance, kLocusParamId, value);
    } else if (id == kEndParamId) {
        value = std::max(value,
            paramValue(*_instance, kStartParamId) + epsilon);
        if (paramValue(*_instance, kLocusParamId) > value)
            queueGuiParamValue(*_instance, kLocusParamId, value);
    } else if (id == kLocusParamId) {
        value = std::clamp(value, paramValue(*_instance, kStartParamId),
            paramValue(*_instance, kEndParamId));
    }
    queueGuiParamValue(*_instance, id, value);
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (_presetMenuOpen || _openMenuParam != CLAP_INVALID_ID) {
        const bool preset = _presetMenuOpen;
        const MotionMenuSpec spec = preset
            ? MotionMenuSpec { kMotionPresetItems,
                static_cast<uint32_t>(std::size(kMotionPresetItems)), 1u }
            : motionMenuSpec(_openMenuParam);
        const int selected = spec.columns > 1u
            ? s3g::clap_gui::multiColumnDropdownHitIndex(point,
                [self openMenuRect], 20.0, spec.count, spec.columns)
            : s3g::clap_gui::dropdownHitIndex(point,
                [self openMenuRect], 20.0, spec.count);
        const clap_id id = _openMenuParam;
        [self closeMenus];
        if (selected >= 0) {
            if (preset) [self applyPreset:selected];
            else if (id == kSoundModeUiId)
                [self applySoundMode:selected];
            else queueGuiParamGesture(*_instance, id,
                    motionMenuValue(id, selected));
        }
        [self setNeedsDisplay:YES];
        return;
    }
    const NSRect presetAnchor = s3g::clap_gui::cocoaRect(
        kMotionTitleBand.presetMenu);
    if (NSPointInRect(point, presetAnchor)) {
        _presetMenuOpen = YES;
        _menuHover = -1;
        [_cursorView setHidden:YES];
        [self setNeedsDisplay:YES];
        return;
    }
    if (s3g::clap_gui::handleProcessorTitleClick(point,
            &_instance->plugin, [NSString stringWithUTF8String:
                _instance->plugin.desc->name], kMotionTitleBand,
            _presetName, sizeof(_presetName))) {
        _presetIndex = -1;
        markStateDirty(*_instance);
        [self setNeedsDisplay:YES];
        return;
    }
    if (isMultichannel(*_instance)
        && NSPointInRect(point, kOutputPageButton)) {
        _outputRoutingPage = !_outputRoutingPage;
        [self closeMenus];
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, kSampleLoadButton)) { [self openSample]; return; }
    if (NSPointInRect(point, kClearButton)) {
        cancelSampleLoads(*_instance);
        _instance->projectFileRegistration.clear();
        {
            std::lock_guard<std::mutex> lock(_instance->statusMutex);
            _instance->linkSourcePath.clear();
            _instance->projectRelativePath.clear();
            _instance->sourceFileBytes = 0u;
            _instance->projectStoragePending = false;
            _instance->projectContextPending = false;
            _instance->projectCopyInFlight = false;
        }
        publishAsset(*_instance, {}, "");
        _waveZoom = 1.0;
        _waveViewStart = 0.0;
        [_cursorView invalidateTrajectory];
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, kStorageButton)) {
        StorageMode mode = StorageMode::Project;
        {
            std::lock_guard<std::mutex> lock(_instance->statusMutex);
            mode = _instance->storageMode;
        }
        const auto next = static_cast<StorageMode>(
            (static_cast<uint8_t>(mode) + 1u) % 3u);
        setStorageMode(*_instance, next);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, kPreviewButton)) {
        requestAction(*_instance, kActionPreview); return;
    }
    if (NSPointInRect(point, kStopButton)) {
        requestAction(*_instance, kActionStopAll); return;
    }
    const clap_id menus[] = { kMotionParamId, kRateBasisParamId,
        kSoundModeUiId, kMotorEnvelopeParamId, kVoiceModeParamId,
        kTriggerParamId, kMidiParamId, kOutputTraversalParamId,
        kOutputVoiceWidthParamId, kStereoPairLayoutParamId,
        kActiveOutputCountParamId,
        kSegmentTriggerParamId, kEventOverlapParamId,
        kOutputAssignmentEventParamId, kAvoidAdjacentParamId };
    for (clap_id id : menus) if (NSPointInRect(point,
            [self menuRectForParam:id])) {
        _openMenuParam = id;
        _menuHover = -1;
        [_cursorView setHidden:YES];
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, kWaveRect) && _instance->controlAsset) {
        clap_id closest = CLAP_INVALID_ID;
        CGFloat distance = 12.0;
        for (clap_id id : { kStartParamId, kEndParamId, kLocusParamId }) {
            const CGFloat candidate = std::abs(point.x
                - [self waveXForNormalized:paramValue(*_instance, id)]);
            if (candidate < distance) { distance = candidate; closest = id; }
        }
        if (closest == CLAP_INVALID_ID) {
            if ([event clickCount] >= 2) {
                _waveZoom = 1.0;
                _waveViewStart = 0.0;
                [_cursorView setViewStart:0.0 span:1.0];
                [_cursorView updateTrajectory];
                [self setNeedsDisplay:YES];
            }
            return;
        }
        _dragParam = closest;
        _waveDragging = YES;
        queueGuiParamBegin(*_instance, _dragParam);
        [self setRangeParam:_dragParam
            normalized:[self waveNormalizedAtPoint:point]];
        return;
    }
    NSRect panel {};
    const clap_id slider = [self sliderAtPoint:point panel:&panel];
    if (slider == CLAP_INVALID_ID) return;
    double defaultValue = 0.0;
    if (s3g::clap_gui::sliderDoubleClickDefault(event, &_instance->plugin,
            slider, &defaultValue)) {
        queueGuiParamGesture(*_instance, slider, defaultValue);
        [self setNeedsDisplay:YES];
        return;
    }
    _dragParam = slider;
    _dragPanel = panel;
    queueGuiParamBegin(*_instance, slider);
    [self setDraggedParam:slider point:point panel:panel];
}

- (void)mouseDragged:(NSEvent*)event
{
    if (_dragParam == CLAP_INVALID_ID) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (_waveDragging)
        [self setRangeParam:_dragParam
            normalized:[self waveNormalizedAtPoint:point]];
    else [self setDraggedParam:_dragParam point:point panel:_dragPanel];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (_dragParam != CLAP_INVALID_ID)
        queueGuiParamEnd(*_instance, _dragParam);
    _dragParam = CLAP_INVALID_ID;
    _waveDragging = NO;
}

- (void)scrollWheel:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (!NSPointInRect(point, kWaveRect) || !_instance->controlAsset) {
        [super scrollWheel:event];
        return;
    }
    const double oldSpan = [self waveVisibleSpan];
    const bool pan = ([event modifierFlags] & NSEventModifierFlagShift) != 0
        || std::abs([event scrollingDeltaX])
            > std::abs([event scrollingDeltaY]);
    if (pan && _waveZoom > 1.0) {
        const double delta = std::abs([event scrollingDeltaX])
                > std::abs([event scrollingDeltaY])
            ? [event scrollingDeltaX] : [event scrollingDeltaY];
        _waveViewStart = std::clamp(_waveViewStart
            + delta * oldSpan * 0.0125, 0.0, 1.0 - oldSpan);
    } else {
        const double anchor = std::clamp(static_cast<double>(
            (point.x - NSMinX(kWaveRect)) / NSWidth(kWaveRect)), 0.0, 1.0);
        const double sourceAnchor = _waveViewStart + anchor * oldSpan;
        _waveZoom = std::clamp(_waveZoom * std::exp(
            static_cast<double>([event scrollingDeltaY]) * 0.08),
            1.0, 128.0);
        const double newSpan = [self waveVisibleSpan];
        _waveViewStart = std::clamp(sourceAnchor - anchor * newSpan,
            0.0, 1.0 - newSpan);
    }
    [_cursorView setViewStart:_waveViewStart span:[self waveVisibleSpan]];
    [_cursorView updateTrajectory];
    [self setNeedsDisplay:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (!_presetMenuOpen && _openMenuParam == CLAP_INVALID_ID) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    const MotionMenuSpec spec = _presetMenuOpen
        ? MotionMenuSpec { kMotionPresetItems,
            static_cast<uint32_t>(std::size(kMotionPresetItems)), 1u }
        : motionMenuSpec(_openMenuParam);
    const int hover = spec.columns > 1u
        ? s3g::clap_gui::multiColumnDropdownHitIndex(point,
            [self openMenuRect], 20.0, spec.count, spec.columns)
        : s3g::clap_gui::dropdownHitIndex(point,
            [self openMenuRect], 20.0, spec.count);
    if (hover != _menuHover) { _menuHover = hover; [self setNeedsDisplay:YES]; }
}

- (void)mouseExited:(NSEvent*)event
{ (void)event; _menuHover = -1; }

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
    return [[[sender draggingPasteboard] readObjectsForClasses:@[ [NSURL class] ]
        options:@{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }] count] > 0u
        ? NSDragOperationCopy : NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    NSArray<NSURL*>* urls = [[sender draggingPasteboard]
        readObjectsForClasses:@[ [NSURL class] ]
        options:@{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }];
    if ([urls count] == 0u) return NO;
    queueSampleLoad(*_instance, std::string([[[urls objectAtIndex:0u] path]
        UTF8String]));
    return YES;
}

- (BOOL)loadDocumentationSample
{
    if (!_instance || !_instance->controlAsset) return NO;
    [self applyEventModelRecipe:5];
    std::snprintf(_presetName, sizeof(_presetName), "%s", "BOUNCE");
    constexpr std::array<float, 3u> positions {{ 0.21f, 0.48f, 0.74f }};
    constexpr std::array<uint8_t, 3u> keys {{ 48u, 60u, 67u }};
    for (std::size_t slot = 0u; slot < positions.size(); ++slot) {
        _instance->cursorPositions[slot].store(positions[slot],
            std::memory_order_relaxed);
        _instance->cursorMotionPhases[slot].store(0.18f + 0.27f * slot,
            std::memory_order_relaxed);
        _instance->cursorInnerPhases[slot].store(0.12f + 0.16f * slot,
            std::memory_order_relaxed);
        _instance->cursorOuterPhases[slot].store(0.20f + 0.22f * slot,
            std::memory_order_relaxed);
        _instance->cursorIdentities[slot].store(1001u + slot,
            std::memory_order_relaxed);
        _instance->cursorKeys[slot].store(keys[slot],
            std::memory_order_relaxed);
        _instance->cursorDirections[slot].store(slot != 2u,
            std::memory_order_relaxed);
        _instance->cursorPacketActive[slot].store(slot != 1u,
            std::memory_order_relaxed);
        _instance->cursorOutputFirst[slot].store(
            static_cast<uint8_t>(slot * 2u), std::memory_order_relaxed);
        _instance->cursorOutputSecond[slot].store(
            static_cast<uint8_t>(slot * 2u + 1u),
            std::memory_order_relaxed);
        _instance->cursorOutputWidths[slot].store(2u,
            std::memory_order_relaxed);
    }
    _instance->cursorCount.store(static_cast<uint32_t>(positions.size()),
        std::memory_order_release);
    _instance->activeVoiceCount.store(static_cast<uint32_t>(positions.size()),
        std::memory_order_release);
    [_cursorView invalidateTrajectory];
    [_cursorView updateTrajectory];
    [self setNeedsDisplay:YES];
    [self displayIfNeeded];
    return YES;
}

- (void)drawButton:(NSRect)rect text:(NSString*)text active:(BOOL)active
    style:(const s3g::clap_gui::Style&)style attrs:(NSDictionary*)attrs
{
    [s3g::clap_gui::color(active ? 0x303030 : 0x181818) setFill];
    NSRectFill(rect);
    [style.grid setStroke]; NSFrameRect(rect);
    if (active) {
        [style.accent setStroke]; NSFrameRect(NSInsetRect(rect, 1.0, 1.0));
    }
    const NSSize size = [text sizeWithAttributes:attrs];
    [text drawAtPoint:NSMakePoint(NSMidX(rect) - size.width * 0.5,
        NSMidY(rect) - size.height * 0.5) withAttributes:attrs];
}

- (void)drawWaveform:(const s3g::clap_gui::Style&)style
    values:(NSDictionary*)values
{
    [style.strip setFill]; NSRectFill(kWaveRect);
    [style.grid setStroke]; NSFrameRect(kWaveRect);
    const auto asset = currentAsset(*_instance);
    if (!asset || !asset->valid()) {
        [@"DROP AUDIO HERE" drawAtPoint:NSMakePoint(
            NSMidX(kWaveRect) - 48.0, NSMidY(kWaveRect) - 7.0)
            withAttributes:values];
        return;
    }
    const uint32_t frames = asset->frameCount();
    const uint8_t lanes = asset->channelCount;
    const double visibleSpan = [self waveVisibleSpan];
    const double visibleEnd = _waveViewStart + visibleSpan;
    const auto bounds = motionFieldBounds(*_instance);
    const CGFloat fieldLeft = [self waveXForNormalized:bounds.first];
    const CGFloat fieldRight = [self waveXForNormalized:bounds.second];
    const NSRect clippedField = NSIntersectionRect(kWaveRect,
        NSMakeRect(fieldLeft, NSMinY(kWaveRect),
            fieldRight - fieldLeft, NSHeight(kWaveRect)));
    if (!NSIsEmptyRect(clippedField)) {
        [s3g::clap_gui::color(0xd79a55, 0.09) setFill];
        NSRectFill(clippedField);
    }
    const CGFloat laneHeight = (NSHeight(kWaveRect) - 4.0)
        / std::max<uint8_t>(1u, lanes);
    NSDictionary* channelAttrs = s3g::clap_gui::textAttrs(
        s3g::clap_gui::color(0x676767), 8.0);
    for (uint8_t lane = 0u; lane < lanes; ++lane) {
        const NSRect laneRect = NSMakeRect(NSMinX(kWaveRect),
            NSMinY(kWaveRect) + 2.0 + laneHeight * lane,
            NSWidth(kWaveRect), laneHeight);
        const auto& samples = asset->channels[lane];
        NSBezierPath* path = [NSBezierPath bezierPath];
        for (NSInteger pixel = 0;
             pixel < static_cast<NSInteger>(NSWidth(laneRect)); ++pixel) {
            const double sourceBegin = _waveViewStart
                + pixel / NSWidth(laneRect) * visibleSpan;
            const double sourceEnd = _waveViewStart
                + (pixel + 1.0) / NSWidth(laneRect) * visibleSpan;
            const uint32_t begin = std::min<uint32_t>(frames - 1u,
                static_cast<uint32_t>(sourceBegin * frames));
            const uint32_t finish = std::min<uint32_t>(frames,
                std::max(begin + 1u,
                    static_cast<uint32_t>(std::ceil(sourceEnd * frames))));
            const uint32_t stride = std::max<uint32_t>(1u,
                (finish - begin + 31u) / 32u);
            float low = 1.0f, high = -1.0f;
            for (uint32_t frame = begin; frame < finish; frame += stride) {
                low = std::min(low, samples[frame]);
                high = std::max(high, samples[frame]);
            }
            low = std::min(low, samples[finish - 1u]);
            high = std::max(high, samples[finish - 1u]);
            const CGFloat x = NSMinX(laneRect) + pixel;
            [path moveToPoint:NSMakePoint(x,
                NSMidY(laneRect) - high * NSHeight(laneRect) * 0.42)];
            [path lineToPoint:NSMakePoint(x,
                NSMidY(laneRect) - low * NSHeight(laneRect) * 0.42)];
        }
        [s3g::clap_gui::color(lane == 0u ? 0x747d78 : 0x68706c)
            setStroke];
        [path setLineWidth:1.0]; [path stroke];
        if (lane != 0u) {
            [s3g::clap_gui::color(0x333333) setFill];
            NSRectFill(NSMakeRect(NSMinX(kWaveRect) + 1.0,
                NSMinY(laneRect), NSWidth(kWaveRect) - 2.0, 1.0));
        }
        NSString* label = [NSString stringWithFormat:@"%02u",
            static_cast<unsigned>(lane + 1u)];
        [label drawAtPoint:NSMakePoint(NSMinX(laneRect) + 5.0,
            NSMinY(laneRect) + 3.0) withAttributes:channelAttrs];
    }
    struct Marker { clap_id id; NSString* label; NSColor* tint; };
    const Marker markers[] = {
        { kStartParamId, @"S", style.accent },
        { kEndParamId, @"E", style.accent },
        { kLocusParamId, @"L", style.fill },
    };
    for (const auto& marker : markers) {
        const double position = paramValue(*_instance, marker.id);
        if (position < _waveViewStart || position > visibleEnd) continue;
        const CGFloat x = [self waveXForNormalized:position];
        [marker.tint setStroke];
        NSBezierPath* line = [NSBezierPath bezierPath];
        [line moveToPoint:NSMakePoint(x, NSMinY(kWaveRect))];
        [line lineToPoint:NSMakePoint(x, NSMaxY(kWaveRect))];
        [line setLineWidth:marker.id == kLocusParamId ? 1.75 : 1.25];
        [line stroke];
        [marker.label drawAtPoint:NSMakePoint(std::clamp(x + 4.0,
            NSMinX(kWaveRect) + 4.0, NSMaxX(kWaveRect) - 18.0),
            NSMinY(kWaveRect) + 4.0) withAttributes:values];
    }
    for (const double edge : { bounds.first, bounds.second }) {
        if (edge < _waveViewStart || edge > visibleEnd) continue;
        [style.dim setStroke];
        NSBezierPath* line = [NSBezierPath bezierPath];
        [line moveToPoint:NSMakePoint([self waveXForNormalized:edge],
            NSMinY(kWaveRect))];
        [line lineToPoint:NSMakePoint([self waveXForNormalized:edge],
            NSMaxY(kWaveRect))];
        [line setLineWidth:0.75]; [line stroke];
    }
    if (![NSGraphicsContext currentContextDrawingToScreen]) {
        const uint32_t count = std::min<uint32_t>(
            _instance->cursorCount.load(std::memory_order_acquire),
            static_cast<uint32_t>(_instance->cursorPositions.size()));
        for (uint32_t slot = 0u; slot < count; ++slot) {
            const float position = _instance->cursorPositions[slot].load(
                std::memory_order_relaxed);
            if (position < _waveViewStart || position > visibleEnd) continue;
            const CGFloat x = [self waveXForNormalized:position];
            NSColor* tint = slot % 3u == 0u ? style.fill
                : (slot % 3u == 1u ? style.accent : style.dim);
            [tint setStroke];
            NSBezierPath* line = [NSBezierPath bezierPath];
            [line moveToPoint:NSMakePoint(x, NSMinY(kWaveRect))];
            [line lineToPoint:NSMakePoint(x, NSMaxY(kWaveRect))];
            [line setLineWidth:1.25]; [line stroke];
            const unsigned key = static_cast<unsigned>(
                _instance->cursorKeys[slot].load(
                    std::memory_order_relaxed));
            const unsigned first = static_cast<unsigned>(
                _instance->cursorOutputFirst[slot].load(
                    std::memory_order_relaxed)) + 1u;
            NSString* note = [NSString stringWithFormat:@"N%u>%02u", key,
                first];
            [note drawAtPoint:NSMakePoint(x + 4.0,
                NSMinY(kWaveRect) + 18.0 + (slot % 10u) * 15.0)
                withAttributes:values];
        }
    }
    const std::string storage = storageDetail(*_instance, asset);
    NSString* detail = [NSString stringWithFormat:
        @"%u VOICES  /  %u CH  /  %u FRAMES  /  %.0f HZ  /  %s",
        static_cast<unsigned>(_instance->activeVoiceCount.load(
            std::memory_order_relaxed)), static_cast<unsigned>(lanes),
        static_cast<unsigned>(frames), asset->sampleRate, storage.c_str()];
    [detail drawAtPoint:NSMakePoint(NSMinX(kWaveRect),
        NSMaxY(kWaveRect) + 6.0) withAttributes:values];
    NSString* help = [NSString stringWithFormat:
        @"DRAG S / E / L   SCROLL ZOOM   SHIFT-SCROLL PAN   DOUBLE-CLICK FIT   ZOOM %.1fX",
        _waveZoom];
    [help drawAtPoint:NSMakePoint(NSMinX(kWaveRect),
        NSMaxY(kWaveRect) + 23.0) withAttributes:values];
}

- (void)drawScope:(const s3g::clap_gui::Style&)style
    values:(NSDictionary*)values
{
    [style.strip setFill]; NSRectFill(kScopeRect);
    [style.grid setStroke]; NSFrameRect(kScopeRect);
    const int motion = static_cast<int>(std::lround(paramValue(*_instance,
        kMotionParamId)));
    const int articulation = static_cast<int>(std::lround(paramValue(
        *_instance, kArticulationParamId)));
    const int segmentModel = static_cast<int>(std::lround(paramValue(
        *_instance, kSegmentModelParamId)));
    const bool motor = articulation == 1;
    const bool packets = articulation != 0 || segmentModel != 0;
    const auto bounds = motionFieldBounds(*_instance);
    NSString* scopeTitle = [NSString stringWithFormat:@"SIGNAL FLOW / %u VOICES",
        static_cast<unsigned>(_instance->activeVoiceCount.load(
            std::memory_order_relaxed))];
    [scopeTitle drawAtPoint:NSMakePoint(NSMinX(kScopeRect) + 10.0,
        NSMinY(kScopeRect) + 8.0) withAttributes:values];
    NSString* path = [NSString stringWithFormat:@"1A PATH  %s",
        motionName(motion)];
    [path drawAtPoint:NSMakePoint(NSMinX(kScopeRect) + 10.0,
        NSMinY(kScopeRect) + 27.0) withAttributes:values];
    NSString* source = [NSString stringWithFormat:@"1B SOURCE %s",
        eventSourceDescription(segmentModel, motion,
            paramValue(*_instance, kEventStepParamId))];
    [source drawAtPoint:NSMakePoint(NSMinX(kScopeRect) + 10.0,
        NSMinY(kScopeRect) + 46.0) withAttributes:values];
    NSString* sound = [NSString stringWithFormat:@"2 SOUND  %s",
        segmentModel != 0 ? segmentModelName(segmentModel)
            : articulationName(articulation)];
    [sound drawAtPoint:NSMakePoint(NSMinX(kScopeRect) + 10.0,
        NSMinY(kScopeRect) + 65.0) withAttributes:values];
    [s3g::clap_gui::color(0x202020) setFill]; NSRectFill(kScopeGraphRect);
    [style.grid setStroke]; NSFrameRect(kScopeGraphRect);
    const auto yFor = [&](double normalized) {
        return NSMaxY(kScopeGraphRect) - 6.0
            - normalized * (NSHeight(kScopeGraphRect) - 12.0);
    };
    NSBezierPath* trajectory = [NSBezierPath bezierPath];
    uint32_t randomState = static_cast<uint32_t>(std::lround(
        paramValue(*_instance, kSeedParamId)));
    double drunkA = std::clamp(paramValue(*_instance, kLocusParamId),
        bounds.first, bounds.second);
    double drunkB = drunkA;
    const double travel = paramValue(*_instance, kTravelParamId)
        * (bounds.second - bounds.first);
    for (uint32_t point = 0u; point <= 256u; ++point) {
        const double phase = point / 256.0;
        double position = 0.5 * (bounds.first + bounds.second);
        if (motion == 0)
            position += 0.5 * (bounds.second - bounds.first)
                * std::sin(2.0 * 3.14159265358979323846 * phase);
        else if (motion == 1 || motion == 7) {
            const double triangle = phase < 0.5 ? phase * 2.0
                : 2.0 - phase * 2.0;
            position = bounds.first
                + triangle * (bounds.second - bounds.first);
        } else if (motion == 4 || motion == 6) {
            position = bounds.first + phase * (bounds.second - bounds.first);
        } else if (motion == 5) {
            position = bounds.second - phase * (bounds.second - bounds.first);
        } else {
            const uint32_t segment = std::min<uint32_t>(7u,
                static_cast<uint32_t>(phase * 8.0));
            randomState = static_cast<uint32_t>(std::lround(
                paramValue(*_instance, kSeedParamId)));
            drunkA = std::clamp(paramValue(*_instance, kLocusParamId),
                bounds.first, bounds.second);
            for (uint32_t step = 0u; step <= segment; ++step) {
                const double unit = static_cast<double>(
                    motionPreviewRandom(randomState) >> 8u) / 16777215.0;
                if (motion == 3) {
                    const double direction = (step & 1u) == 0u ? 1.0 : -1.0;
                    drunkB = std::clamp(drunkA + direction
                        * (0.20 + unit * 0.80) * travel,
                        bounds.first, bounds.second);
                    if (std::abs(drunkB - drunkA) < 1.0e-6)
                        drunkB = std::clamp(drunkA - direction
                            * (0.20 + unit * 0.80) * travel,
                            bounds.first, bounds.second);
                } else {
                    drunkB = std::clamp(drunkA
                        + (unit * 2.0 - 1.0) * travel,
                        bounds.first, bounds.second);
                }
                if (step != segment) drunkA = drunkB;
            }
            const double local = std::min(1.0, phase * 8.0 - segment);
            const double smooth = local * local * (3.0 - 2.0 * local);
            const double amount = local + (smooth - local)
                * paramValue(*_instance, kJoinParamId);
            position = drunkA + (drunkB - drunkA) * amount;
        }
        const NSPoint graphPoint = NSMakePoint(NSMinX(kScopeGraphRect)
                + phase * NSWidth(kScopeGraphRect), yFor(position));
        if (point == 0u) [trajectory moveToPoint:graphPoint];
        else [trajectory lineToPoint:graphPoint];
    }
    [style.accent setStroke]; [trajectory setLineWidth:1.25];
    [trajectory stroke];
    if (packets) {
        if (motor || segmentModel != 0) {
        NSBezierPath* envelope = [NSBezierPath bezierPath];
        const double symmetry = paramValue(*_instance, kSymmetryParamId);
        const auto shape = static_cast<MotorEnvelopeShape>(std::clamp(
            static_cast<int>(std::lround(paramValue(*_instance,
                kMotorEnvelopeParamId))), 0, 3));
        for (uint32_t point = 0u; point <= 128u; ++point) {
            const double phase = point / 128.0;
            const double level = s3g::sample::motorEnvelopeLevel(
                phase, symmetry, shape);
            const NSPoint p = NSMakePoint(NSMinX(kScopeGraphRect)
                    + phase * NSWidth(kScopeGraphRect),
                NSMaxY(kScopeGraphRect) - 4.0 - level * 18.0);
            if (point == 0u) [envelope moveToPoint:p];
            else [envelope lineToPoint:p];
        }
        [style.dim setStroke]; [envelope setLineWidth:1.0]; [envelope stroke];
        }
        const int ticks = static_cast<int>(std::clamp(std::lround(
            segmentModel != 0 ? paramValue(*_instance, kEventRateParamId)
                : motor ? paramValue(*_instance, kInnerRateParamId)
                    / std::max(0.05, paramValue(*_instance,
                        kOuterRateParamId))
                : paramValue(*_instance, kInnerRateParamId) * 0.5),
            1l, 64l));
        for (int tick = 0; tick < ticks; ++tick) {
            const CGFloat x = NSMinX(kScopeGraphRect)
                + (tick + 0.5) / ticks * NSWidth(kScopeGraphRect);
            [style.fill setFill];
            NSRectFill(NSMakeRect(x, NSMaxY(kScopeGraphRect) - 8.0,
                1.0, 4.0));
        }
    }
    const uint32_t focused = [self newestVoiceCursorSlot];
    if (focused != std::numeric_limits<uint32_t>::max()) {
        const double phase = motor
            ? _instance->cursorOuterPhases[focused].load(
                std::memory_order_relaxed)
            : packets ? _instance->cursorInnerPhases[focused].load(
                std::memory_order_relaxed)
            : _instance->cursorMotionPhases[focused].load(
                std::memory_order_relaxed);
        const CGFloat x = NSMinX(kScopeGraphRect)
            + phase * NSWidth(kScopeGraphRect);
        [style.fill setFill];
        NSRectFill(NSMakeRect(x - 0.5, NSMinY(kScopeGraphRect), 1.0,
            NSHeight(kScopeGraphRect)));
    }
}

- (void)drawSlider:(clap_id)id name:(NSString*)name y:(CGFloat)y
    panel:(NSRect)panel style:(const s3g::clap_gui::Style&)style
    labels:(NSDictionary*)labels values:(NSDictionary*)values
{
    [self drawSlider:id name:name y:y panel:panel style:style
        labels:labels values:values enabled:YES];
}

- (void)drawSlider:(clap_id)id name:(NSString*)name y:(CGFloat)y
    panel:(NSRect)panel style:(const s3g::clap_gui::Style&)style
    labels:(NSDictionary*)labels values:(NSDictionary*)values
    enabled:(BOOL)enabled
{
    const ParamDef* def = paramDef(id);
    char text[64] {};
    paramsValueToText(&_instance->plugin, id, paramValue(*_instance, id),
        text, sizeof(text));
    auto rowStyle = style;
    NSDictionary* rowLabels = labels;
    NSDictionary* rowValues = values;
    if (!enabled) {
        rowStyle.strip = s3g::clap_gui::color(0x161616);
        rowStyle.grid = s3g::clap_gui::color(0x303030);
        rowStyle.fill = s3g::clap_gui::color(0x363636);
        rowStyle.text = s3g::clap_gui::color(0x555555);
        rowLabels = s3g::clap_gui::textAttrs(
            s3g::clap_gui::color(0x606060), 10.0);
        rowValues = s3g::clap_gui::textAttrs(
            s3g::clap_gui::color(0x555555), 10.0);
    }
    s3g::clap_gui::drawSlider(name, [NSString stringWithUTF8String:text],
        motionParamNormalized(*def, paramValue(*_instance, id)), y,
        rowLabels, rowValues, rowStyle, panel.origin.x + 12.0,
        panel.origin.x + 104.0, panel.origin.x + panel.size.width - 56.0,
        panel.size.width - 164.0, 48.0);
}

- (void)drawMenu:(clap_id)id name:(NSString*)name y:(CGFloat)y
    panel:(NSRect)panel style:(const s3g::clap_gui::Style&)style
    labels:(NSDictionary*)labels values:(NSDictionary*)values
{
    char text[64] {};
    paramsValueToText(&_instance->plugin, id, paramValue(*_instance, id),
        text, sizeof(text));
    s3g::clap_gui::drawMenu(name, [NSString stringWithUTF8String:text], y,
        labels, values, style, panel.origin.x + 12.0,
        panel.origin.x + 104.0, panel.size.width - 116.0);
}

- (void)drawSoundMenu:(const s3g::clap_gui::Style&)style
    labels:(NSDictionary*)labels values:(NSDictionary*)values
{
    const int model = static_cast<int>(std::lround(paramValue(
        *_instance, kSegmentModelParamId)));
    const int articulation = static_cast<int>(std::lround(paramValue(
        *_instance, kArticulationParamId)));
    const int index = soundModeIndex(model, articulation);
    s3g::clap_gui::drawMenu(@"SOUND", kSoundModeItems[index], 482.0,
        labels, values, style, kMotorPanel.origin.x + 12.0,
        kMotorPanel.origin.x + 104.0, kMotorPanel.size.width - 116.0);
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    const auto style = s3g::clap_gui::softTextStyle();
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    NSDictionary* title = s3g::clap_gui::softTitleAttrs();
    [style.bg setFill]; NSRectFill(self.bounds);
    s3g::clap_gui::drawProcessorTitleBand(
        [NSString stringWithUTF8String:_instance->plugin.desc->name],
        [NSString stringWithUTF8String:_presetName],
        s3g::clap_gui::peakDbText(_instance->outputPeak.load(
            std::memory_order_relaxed)), kMotionTitleBand,
        title, labels, values, style);
    s3g::clap_gui::drawPanelFrame(NSMinX(kSamplePanel), NSMinY(kSamplePanel),
        NSWidth(kSamplePanel), NSHeight(kSamplePanel), style);
    s3g::clap_gui::drawPanelHeader(@"SAMPLE", true, NSMinX(kSamplePanel),
        NSMinY(kSamplePanel), NSWidth(kSamplePanel),
        static_cast<CGFloat>(s3g::gui_layout::kStandardMetrics.headerHeight),
        labels, style);
    s3g::clap_gui::drawToolboxHeaderButton(kSampleLoadButton, kSamplePanel,
        @"LOAD SAMPLE", false, labels, style);
    s3g::clap_gui::drawToolboxHeaderButton(kClearButton, kSamplePanel,
        @"CLEAR", false, labels, style);
    StorageMode storageMode = StorageMode::Project;
    {
        std::lock_guard<std::mutex> lock(_instance->statusMutex);
        storageMode = _instance->storageMode;
    }
    NSString* storageLabel = [NSString stringWithFormat:@"STORE %s",
        s3g::sample_storage::storageModeName(storageMode)];
    s3g::clap_gui::drawToolboxHeaderButton(kStorageButton, kSamplePanel,
        storageLabel, storageMode != StorageMode::Link, labels, style);
    std::string status;
    {
        std::lock_guard<std::mutex> lock(_instance->statusMutex);
        status = _instance->status;
    }
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(NSMakeRect(90.0, 57.0, 586.0, 19.0));
    [[NSString stringWithUTF8String:status.c_str()]
        drawAtPoint:NSMakePoint(90.0, 59.0) withAttributes:values];
    [NSGraphicsContext restoreGraphicsState];
    [self drawWaveform:style values:values];
    [self drawScope:style values:values];
    const int selectedMotion = static_cast<int>(std::lround(paramValue(
        *_instance, kMotionParamId)));
    const int selectedModel = static_cast<int>(std::lround(paramValue(
        *_instance, kSegmentModelParamId)));
    const bool eventActive = selectedModel != 0;
    NSString* outputSection = _outputRoutingPage ? @"OUTPUT / ROUTING"
        : isMultichannel(*_instance) ? @"VOICE / PITCH / MIDI"
        : kOutputVoicePanelTitle;
    NSString* outputTitle = [NSString stringWithFormat:@"3  %@",
        outputSection];
    const struct Panel { NSRect rect; NSString* title; } panels[] = {
        { kMotionPanel, @"1  SOURCE FIELD + MOTION" },
        { kMotorPanel, eventActive ? @"2  EVENT PROCESS"
            : @"2  DIRECT PLAYBACK" },
        { kOutputPanel, outputTitle },
    };
    for (const auto& panel : panels) {
        s3g::clap_gui::drawPanelFrame(NSMinX(panel.rect), NSMinY(panel.rect),
            NSWidth(panel.rect), NSHeight(panel.rect), style);
        s3g::clap_gui::drawPanelHeader(panel.title, true,
            NSMinX(panel.rect), NSMinY(panel.rect), NSWidth(panel.rect),
            27.0, labels, style);
    }
    if (isMultichannel(*_instance))
        s3g::clap_gui::drawToolboxHeaderButton(kOutputPageButton, kOutputPanel,
            _outputRoutingPage ? @"VOICE / PITCH" : @"ROUTING",
            _outputRoutingPage, labels, style);
    [self drawMenu:kMotionParamId name:@"PATH" y:482 panel:kMotionPanel style:style labels:labels values:values];
    [self drawSlider:kStartParamId name:@"START" y:507 panel:kMotionPanel style:style labels:labels values:values];
    [self drawSlider:kEndParamId name:@"END" y:532 panel:kMotionPanel style:style labels:labels values:values];
    [self drawSlider:kLocusParamId name:@"LOCUS" y:557 panel:kMotionPanel style:style labels:labels values:values];
    [self drawSlider:kFieldParamId name:@"FIELD WIDTH" y:582 panel:kMotionPanel style:style labels:labels values:values];
    [self drawMenu:kRateBasisParamId name:@"RATE BASIS" y:607 panel:kMotionPanel style:style labels:labels values:values];
    [self drawSlider:kMotionRateParamId name:@"PLAY SPEED" y:632 panel:kMotionPanel style:style labels:labels values:values];
    [self drawSlider:kTravelParamId name:@"TRAVEL" y:657 panel:kMotionPanel style:style labels:labels values:values enabled:pathUsesTravel(selectedMotion)];
    [self drawSlider:kJitterParamId name:@"SHARED RAND" y:682 panel:kMotionPanel style:style labels:labels values:values];
    [self drawSlider:kEventStepParamId name:@"SOURCE STEP" y:707 panel:kMotionPanel style:style labels:labels values:values enabled:sourceStepIsActive(selectedMotion, selectedModel)];
    [self drawSoundMenu:style labels:labels values:values];
    if (eventActive) {
        const int model = selectedModel;
        const int trigger = static_cast<int>(std::lround(paramValue(
            *_instance, kSegmentTriggerParamId)));
        [self drawMenu:kSegmentTriggerParamId name:@"TRIGGER" y:507 panel:kMotorPanel style:style labels:labels values:values];
        for (std::size_t index = 0u; index < 6u; ++index) {
            const auto spec = eventSliderSpec(model, index);
            if (spec.id == CLAP_INVALID_ID) continue;
            [self drawSlider:spec.id
                name:[NSString stringWithUTF8String:
                    eventSliderLabel(model, trigger, spec)]
                y:532.0 + static_cast<CGFloat>(index) * 25.0
                panel:kMotorPanel style:style labels:labels
                values:values enabled:eventSliderIsActive(
                    model, trigger, spec.id)];
        }
        if (model == 3) {
            [self drawMenu:kMotorEnvelopeParamId name:@"SHAPE" y:682 panel:kMotorPanel style:style labels:labels values:values];
            [self drawMenu:kEventOverlapParamId name:@"OVERLAP" y:707 panel:kMotorPanel style:style labels:labels values:values];
        } else {
            [self drawMenu:kEventOverlapParamId name:@"OVERLAP" y:682 panel:kMotorPanel style:style labels:labels values:values];
            [self drawSlider:kSeedParamId name:@"SEED" y:707 panel:kMotorPanel style:style labels:labels values:values];
        }
        if (trigger == 1)
            [self drawSlider:kInnerRateParamId name:@"PACKET RATE" y:732 panel:kMotorPanel style:style labels:labels values:values];
        const int motion = selectedMotion;
        const double step = paramValue(*_instance, kEventStepParamId);
        const CGFloat readoutY = trigger == 1 ? 758.0 : 738.0;
        NSDictionary* sourceAttrs = s3g::clap_gui::textAttrs(
            style.accent, 8.0);
        NSString* coupling = [NSString stringWithFormat:@"LINK  %s",
            eventCouplingStatus(model, motion, trigger, step)];
        [coupling drawAtPoint:NSMakePoint(NSMinX(kMotorPanel) + 10.0,
            readoutY) withAttributes:sourceAttrs];
        NSString* source = [NSString stringWithFormat:@"SOURCE  %s",
            eventSourceDescription(model, motion, step)];
        [source drawAtPoint:NSMakePoint(NSMinX(kMotorPanel) + 10.0,
            readoutY + 13.0) withAttributes:labels];
        NSString* role = [NSString stringWithFormat:@"MOTION  %s",
            eventMotionRoleDescription(model, motion, trigger, step)];
        [role drawAtPoint:NSMakePoint(NSMinX(kMotorPanel) + 10.0,
            readoutY + 26.0) withAttributes:labels];
        NSString* description = [NSString stringWithUTF8String:
            eventModelDescription(model)];
        NSDictionary* processAttrs = s3g::clap_gui::textAttrs(
            style.dim, 8.0);
        [description drawAtPoint:NSMakePoint(NSMinX(kMotorPanel) + 10.0,
            readoutY + 39.0) withAttributes:processAttrs];
    } else {
        const int articulation = static_cast<int>(std::lround(paramValue(
            *_instance, kArticulationParamId)));
        if (articulation == 1) {
            [self drawMenu:kMotorEnvelopeParamId name:@"SHAPE" y:507 panel:kMotorPanel style:style labels:labels values:values];
            [self drawSlider:kInnerRateParamId name:@"INNER RATE" y:532 panel:kMotorPanel style:style labels:labels values:values];
            [self drawSlider:kOuterRateParamId name:@"OUTER RATE" y:557 panel:kMotorPanel style:style labels:labels values:values];
            [self drawSlider:kPacketDutyParamId name:@"DUTY" y:582 panel:kMotorPanel style:style labels:labels values:values];
            [self drawSlider:kSymmetryParamId name:@"SYMMETRY" y:607 panel:kMotorPanel style:style labels:labels values:values];
            [self drawSlider:kJoinParamId name:@"JOIN" y:632 panel:kMotorPanel style:style labels:labels values:values];
            [self drawSlider:kSeedParamId name:@"SEED" y:657 panel:kMotorPanel style:style labels:labels values:values];
        } else if (articulation == 2) {
            [self drawSlider:kInnerRateParamId name:@"INNER RATE" y:507 panel:kMotorPanel style:style labels:labels values:values];
            [self drawSlider:kPacketDutyParamId name:@"DUTY" y:532 panel:kMotorPanel style:style labels:labels values:values];
            [self drawSlider:kJoinParamId name:@"JOIN" y:557 panel:kMotorPanel style:style labels:labels values:values];
            [self drawSlider:kSeedParamId name:@"SEED" y:582 panel:kMotorPanel style:style labels:labels values:values];
        } else {
            [self drawSlider:kJoinParamId name:@"JOIN" y:507 panel:kMotorPanel style:style labels:labels values:values];
            [self drawSlider:kSeedParamId name:@"SEED" y:532 panel:kMotorPanel style:style labels:labels values:values];
        }
        NSDictionary* sourceAttrs = s3g::clap_gui::textAttrs(
            style.accent, 8.0);
        [@"LINK  FULL PATH FOLLOW"
            drawAtPoint:NSMakePoint(NSMinX(kMotorPanel) + 10.0, 690.0)
            withAttributes:sourceAttrs];
        [@"SOURCE  LIVE MOTION CURSOR"
            drawAtPoint:NSMakePoint(NSMinX(kMotorPanel) + 10.0, 703.0)
            withAttributes:labels];
        [@"MOTION  POSITION / PLAY SPEED / DIRECTION"
            drawAtPoint:NSMakePoint(NSMinX(kMotorPanel) + 10.0, 716.0)
            withAttributes:labels];
    }
    [self drawSlider:kOutParamId name:@"OUT" y:482 panel:kOutputPanel style:style labels:labels values:values];
    if (_outputRoutingPage) {
        [self drawMenu:kOutputTraversalParamId name:@"ROUTE" y:507 panel:kOutputPanel style:style labels:labels values:values];
        [self drawMenu:kOutputVoiceWidthParamId name:@"VOICE OUT" y:532 panel:kOutputPanel style:style labels:labels values:values];
        [self drawMenu:kStereoPairLayoutParamId name:@"PAIR MAP" y:557 panel:kOutputPanel style:style labels:labels values:values];
        [self drawMenu:kActiveOutputCountParamId name:@"OUTPUTS" y:582 panel:kOutputPanel style:style labels:labels values:values];
        [self drawMenu:kOutputAssignmentEventParamId name:@"ROUTE ON" y:607 panel:kOutputPanel style:style labels:labels values:values];
        [self drawMenu:kAvoidAdjacentParamId name:@"AVOID NEAR" y:632 panel:kOutputPanel style:style labels:labels values:values];
        [self drawSlider:kAttackParamId name:@"ATTACK" y:657 panel:kOutputPanel style:style labels:labels values:values];
        [self drawSlider:kReleaseParamId name:@"RELEASE" y:682 panel:kOutputPanel style:style labels:labels values:values];
    } else {
        [self drawMenu:kVoiceModeParamId name:@"VOICE" y:507 panel:kOutputPanel style:style labels:labels values:values];
        [self drawMenu:kTriggerParamId name:@"TRIGGER" y:532 panel:kOutputPanel style:style labels:labels values:values];
        [self drawSlider:kRootParamId name:@"ROOT" y:557 panel:kOutputPanel style:style labels:labels values:values];
        [self drawSlider:kTuneParamId name:@"TUNE" y:582 panel:kOutputPanel style:style labels:labels values:values];
        [self drawSlider:kFineParamId name:@"FINE" y:607 panel:kOutputPanel style:style labels:labels values:values];
        [self drawSlider:kAttackParamId name:@"ATTACK" y:632 panel:kOutputPanel style:style labels:labels values:values];
        [self drawSlider:kReleaseParamId name:@"RELEASE" y:657 panel:kOutputPanel style:style labels:labels values:values];
        [self drawSlider:kVelocityParamId name:@"VELOCITY" y:682 panel:kOutputPanel style:style labels:labels values:values];
        [self drawMenu:kMidiParamId name:@"MIDI" y:707 panel:kOutputPanel style:style labels:labels values:values];
    }
    [self drawButton:kPreviewButton text:@"PLAY" active:(_feedbackMask & kActionPreview) != 0u style:style attrs:values];
    [self drawButton:kStopButton text:@"STOP / KILL ALL" active:(_feedbackMask & kActionStopAll) != 0u style:style attrs:values];
    if (_presetMenuOpen) {
        s3g::clap_gui::drawDropdownMenu([self openMenuRect], 20.0,
            kMotionPresetItems, static_cast<uint32_t>(std::size(kMotionPresetItems)),
            static_cast<int>(_presetIndex), _menuHover, values, style);
    } else if (_openMenuParam != CLAP_INVALID_ID) {
        const MotionMenuSpec spec = motionMenuSpec(_openMenuParam);
        const int selected = _openMenuParam == kSoundModeUiId
            ? soundModeIndex(static_cast<int>(std::lround(paramValue(
                    *_instance, kSegmentModelParamId))),
                static_cast<int>(std::lround(paramValue(
                    *_instance, kArticulationParamId))))
            : motionMenuIndex(_openMenuParam,
                paramValue(*_instance, _openMenuParam));
        if (spec.columns > 1u)
            s3g::clap_gui::drawMultiColumnDropdownMenu([self openMenuRect],
                20.0, spec.items, spec.count, spec.columns, selected,
                _menuHover, values, style);
        else s3g::clap_gui::drawDropdownMenu([self openMenuRect], 20.0,
            spec.items, spec.count, selected, _menuHover, values, style);
    }
}

@end
