} // namespace

namespace {

constexpr auto kLanesTitleBand = s3g::gui_layout::encoderTitleBand({
    static_cast<double>(kGuiWidth), static_cast<double>(kGuiHeight),
});
static_assert(s3g::gui_layout::processorTitleBandFits(kLanesTitleBand));

constexpr NSRect kSamplePanel = {{ 18.0, 54.0 }, { 850.0, 296.0 }};
constexpr NSRect kStorageButton = {{ 744.0, 60.0 }, { 112.0, 16.0 }};
constexpr NSRect kLaneTimingPanel = {{ 880.0, 54.0 }, { 382.0, 120.0 }};
constexpr NSRect kTimingSpeedColumn = kLaneTimingPanel;
constexpr NSRect kTimingStretchColumn = kLaneTimingPanel;
constexpr NSRect kTimingNudgeColumn = kLaneTimingPanel;
constexpr CGFloat kTimingSpeedY = 94.0;
constexpr CGFloat kTimingStretchY = 120.0;
constexpr CGFloat kTimingNudgeY = 146.0;
constexpr NSRect kPathScope = {{ 18.0, 362.0 }, { 850.0, 180.0 }};
constexpr NSRect kPathGraph = {{ 218.0, 390.0 }, { 638.0, 138.0 }};
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
constexpr NSRect kLoopPanel = {{ 880.0, 54.0 }, { 382.0, 176.0 }};
constexpr NSRect kGrainPanel = {{ 880.0, 242.0 }, { 382.0, 224.0 }};
constexpr NSRect kGrainSourceColumn = kGrainPanel;
constexpr NSRect kGrainTextureColumn = kGrainPanel;
constexpr NSRect kPathPanel = {{ 880.0, 478.0 }, { 382.0, 248.0 }};
constexpr NSRect kPathColumn1 = kPathPanel;
constexpr NSRect kPathColumn2 = kPathPanel;
constexpr NSRect kGrainEventPanel = {{ 18.0, 554.0 }, { 850.0, 172.0 }};
constexpr NSRect kGrainEventColumn = kGrainEventPanel;
constexpr NSRect kVoicePanel = {{ 18.0, 738.0 }, { 1244.0, 198.0 }};
constexpr NSRect kVoiceColumn1 = {{ 18.0, 738.0 }, { 392.0, 198.0 }};
constexpr NSRect kVoiceColumn2 = {{ 422.0, 738.0 }, { 392.0, 198.0 }};
constexpr NSRect kVoiceColumn3 = {{ 826.0, 738.0 }, { 436.0, 198.0 }};
constexpr NSRect kOutputPageButton = {{ 850.0, 744.0 }, { 116.0, 16.0 }};
constexpr NSRect kPreviewButton = {{ 974.0, 744.0 }, { 126.0, 16.0 }};
constexpr NSRect kStopButton = {{ 1108.0, 744.0 }, { 142.0, 16.0 }};
#else
constexpr NSRect kLoopPanel = {{ 880.0, 186.0 }, { 382.0, 164.0 }};
constexpr NSRect kPathPanel = {{ 880.0, 362.0 }, { 382.0, 342.0 }};
constexpr NSRect kVoicePanel = {{ 18.0, 554.0 }, { 850.0, 150.0 }};
constexpr NSRect kVoiceColumn1 = {{ 18.0, 554.0 }, { 270.0, 150.0 }};
constexpr NSRect kVoiceColumn2 = {{ 300.0, 554.0 }, { 270.0, 150.0 }};
constexpr NSRect kVoiceColumn3 = {{ 582.0, 554.0 }, { 286.0, 150.0 }};
constexpr NSRect kOutputPageButton = {{ 466.0, 560.0 }, { 116.0, 16.0 }};
constexpr NSRect kPreviewButton = {{ 590.0, 560.0 }, { 126.0, 16.0 }};
constexpr NSRect kStopButton = {{ 724.0, 560.0 }, { 132.0, 16.0 }};
#endif

#if defined(S3G_SAMPLE_GRAINS_VARIANT)
constexpr std::array<const char*, 8u> kPresetNames {{
    "INIT", "Soft Cloud", "Frozen Halo", "Loudness Sort",
    "Stutter Field", "Shrinking Rain", "Doublet Scatter", "Manual Path",
}};
NSString* const kPresetItems[] = {
    @"INIT", @"Soft Cloud", @"Frozen Halo", @"Loudness Sort",
    @"Stutter Field", @"Shrinking Rain", @"Doublet Scatter", @"Manual Path",
};
#else
constexpr std::array<const char*, 8u> kPresetNames {{
    "INIT", "Diagonal Blend", "Diagonal Jumps", "Staircase",
    "Sine Weave", "Random Cuts", "Ping Pong", "Manual Path",
}};
NSString* const kPresetItems[] = {
    @"INIT", @"Diagonal Blend", @"Diagonal Jumps", @"Staircase",
    @"Sine Weave", @"Random Cuts", @"Ping Pong", @"Manual Path",
};
#endif
NSString* const kTransportItems[] = {
    @"Forward", @"Reverse", @"Ping Pong",
};
NSString* const kRateBasisItems[] = { @"Normal", @"Hertz" };
NSString* const kPathItems[] = {
    @"Down", @"Up", @"Triangle", @"Sine", @"Steps Down",
    @"Steps Up", @"Random", @"Manual",
};
NSString* const kBlendItems[] = { @"Crossfade", @"Jump" };
NSString* const kShapeItems[] = {
    @"Linear", @"Smooth", @"Exponential", @"Plateau",
};
NSString* const kVoiceItems[] = { @"Poly", @"Mono", @"Legato" };
NSString* const kTriggerItems[] = {
    @"Auto", @"Gate", @"One Shot", @"Toggle",
};
NSString* const kOutputModeItems[] = { @"Preserve Field", @"Distribute" };
NSString* const kActiveOutputCountItems[] = {
    @"02", @"03", @"04", @"05", @"06", @"07", @"08", @"09",
    @"10", @"11", @"12", @"13", @"14", @"15", @"16", @"17",
    @"18", @"19", @"20", @"21", @"22", @"23", @"24", @"25",
    @"26", @"27", @"28", @"29", @"30", @"31", @"32",
};
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
NSString* const kAllocationCadenceItems[] = { @"Grain" };
#else
NSString* const kAllocationCadenceItems[] = { @"Note", @"Lane", @"Turn" };
#endif
NSString* const kTraversalItems[] = {
    @"Sequential", @"Reverse", @"Palindrome", @"Random", @"Random Cycle",
};
NSString* const kOutputWidthItems[] = { @"Mono", @"Stereo" };
NSString* const kPairLayoutItems[] = { @"Adjacent", @"Split Banks" };
NSString* const kOnOffItems[] = { @"Off", @"On" };
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
NSString* const kChannelModeItems[] = {
    @"Preserve Origins", @"Mono Sum", @"Left", @"Right", @"Mid", @"Side",
};
NSString* const kStereoLinkItems[] = { @"Linked", @"Independent" };
NSString* const kGrainSourceModeItems[] = {
    @"Scan", @"Freeze", @"Cloud", @"Slice",
};
NSString* const kGrainEnvelopeItems[] = {
    @"Parzen", @"Sine", @"Hann", @"Triangle", @"Gaussian",
};
NSString* const kGrainTimingItems[] = { @"Regular", @"Scatter" };
NSString* const kGrainMutateItems[] = {
    @"Ordinary", @"Sorter", @"Stutter", @"Shrink", @"Doublets",
};
#endif
NSString* const kMidiItems[] = {
    @"Omni", @"CH 01", @"CH 02", @"CH 03", @"CH 04", @"CH 05",
    @"CH 06", @"CH 07", @"CH 08", @"CH 09", @"CH 10", @"CH 11",
    @"CH 12", @"CH 13", @"CH 14", @"CH 15", @"CH 16",
};

struct LanesMenuSpec {
    NSString* const* items = nullptr;
    uint32_t count = 0u;
    uint32_t columns = 1u;
};

LanesMenuSpec lanesMenuSpec(clap_id id) noexcept
{
    switch (id) {
    case kTransportParamId: return { kTransportItems, 3u, 1u };
    case kRateBasisParamId: return { kRateBasisItems, 2u, 1u };
    case kPathParamId: return { kPathItems, 8u, 1u };
    case kBlendParamId: return { kBlendItems, 2u, 1u };
    case kShapeParamId: return { kShapeItems, 4u, 1u };
    case kVoiceModeParamId: return { kVoiceItems, 3u, 1u };
    case kTriggerParamId: return { kTriggerItems, 4u, 1u };
    case kOutputModeParamId: return { kOutputModeItems, 2u, 1u };
    case kActiveOutputsParamId:
        return { kActiveOutputCountItems, 31u, 5u };
    case kAllocationCadenceParamId:
        return { kAllocationCadenceItems,
            static_cast<uint32_t>(std::size(kAllocationCadenceItems)), 1u };
    case kTraversalParamId: return { kTraversalItems, 5u, 1u };
    case kOutputWidthParamId: return { kOutputWidthItems, 2u, 1u };
    case kPairLayoutParamId: return { kPairLayoutItems, 2u, 1u };
    case kAvoidAdjacentParamId: return { kOnOffItems, 2u, 1u };
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    case kGrainSourceModeParamId:
        return { kGrainSourceModeItems, 4u, 1u };
    case kGrainEnvelopeParamId:
        return { kGrainEnvelopeItems, 5u, 1u };
    case kGrainTimingParamId:
        return { kGrainTimingItems, 2u, 1u };
    case kGrainMutateParamId:
        return { kGrainMutateItems, 5u, 1u };
    case kSourceTimeSyncParamId: return { kOnOffItems, 2u, 1u };
    case kChannelModeParamId: return { kChannelModeItems, 6u, 1u };
    case kStereoLinkParamId: return { kStereoLinkItems, 2u, 1u };
#endif
    case kMidiParamId: return { kMidiItems, 17u, 2u };
    default: return {};
    }
}

int lanesMenuIndex(clap_id id, double value) noexcept
{
    const int index = static_cast<int>(std::lround(value));
    return id == kActiveOutputsParamId ? index - 2 : index;
}

double lanesMenuValue(clap_id id, int index) noexcept
{
    return static_cast<double>(id == kActiveOutputsParamId
        ? index + 2 : index);
}

bool isOutputRoutingParam(clap_id id) noexcept
{
    return id >= kOutputModeParamId && id <= kAvoidAdjacentParamId;
}

#if defined(S3G_SAMPLE_GRAINS_VARIANT)
bool isStereoOutputParam(clap_id id) noexcept
{
    return id >= kChannelModeParamId && id <= kMonoSpreadParamId;
}
#endif

bool isVoicePageParam(clap_id id) noexcept
{
    switch (id) {
    case kOutParamId:
    case kPanParamId:
    case kVoiceModeParamId:
    case kTriggerParamId:
    case kRootParamId:
    case kTuneParamId:
    case kFineParamId:
    case kAttackParamId:
    case kReleaseParamId:
    case kVelocityParamId:
    case kMidiParamId:
        return true;
    default:
        return false;
    }
}

struct UiRow {
    clap_id id;
    NSRect panel;
    CGFloat y;
    const char* label;
    bool menu;
};

constexpr UiRow kRows[] = {
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    { kGrainSourceModeParamId, kGrainPanel, 282.0, "SOURCE MODE", true },
    { kGrainEnvelopeParamId, kGrainPanel, 306.0, "ENVELOPE", true },
    { kGrainDensityParamId, kGrainSourceColumn, 330.0, "DENSITY", false },
    { kGrainSizeParamId, kGrainSourceColumn, 354.0, "GRAIN SIZE", false },
    { kSourcePositionParamId, kGrainSourceColumn, 378.0, "POSITION", false },
    { kPositionSprayParamId, kGrainTextureColumn, 402.0, "POSITION SPRAY", false },
    { kPitchSprayParamId, kGrainTextureColumn, 426.0, "PITCH SPRAY", false },
    { kReverseChanceParamId, kGrainTextureColumn, 450.0, "REVERSE", false },
    { kGrainTimingParamId, kGrainEventColumn, 594.0, "TIMING", true },
    { kGrainMutateParamId, kGrainEventColumn, 618.0, "GRAIN PROCESS", true },
    { kMutateAmountParamId, kGrainEventColumn, 642.0, "AMOUNT", false },
    { kRegionCountParamId, kGrainEventColumn, 666.0, "REGIONS", false },
    { kSourceTimeSyncParamId, kGrainEventColumn, 690.0, "SOURCE SYNC", true },
    { kOutputModeParamId, kVoiceColumn1, 776.0, "ROUTING MODE", true },
    { kActiveOutputsParamId, kVoiceColumn1, 800.0, "ACTIVE OUT", true },
    { kAllocationCadenceParamId, kVoiceColumn1, 824.0, "ALLOC CLOCK", true },
    { kTraversalParamId, kVoiceColumn2, 776.0, "TRAVERSAL", true },
    { kOutputWidthParamId, kVoiceColumn2, 800.0, "GRAIN WIDTH", true },
    { kPairLayoutParamId, kVoiceColumn2, 824.0, "PAIR LAYOUT", true },
    { kAvoidAdjacentParamId, kVoiceColumn3, 776.0, "AVOID NEAR", true },
    { kTransportParamId, kLoopPanel, 94.0, "DIRECTION", true },
    { kRateBasisParamId, kLoopPanel, 118.0, "RATE BASIS", true },
    { kRateParamId, kLoopPanel, 142.0, "SCAN SPEED", false },
    { kStartParamId, kLoopPanel, 166.0, "START", false },
    { kEndParamId, kLoopPanel, 190.0, "END", false },
    { kLoopCrossfadeParamId, kLoopPanel, 214.0, "LOOP JOIN", false },
    { kPathParamId, kPathPanel, 518.0, "PATH", true },
    { kBlendParamId, kPathPanel, 542.0, "LANE CHANGE", true },
    { kShapeParamId, kPathPanel, 566.0, "SHAPE", true },
    { kCyclesParamId, kPathColumn1, 590.0, "CYCLES", false },
    { kOffsetParamId, kPathColumn2, 614.0, "OFFSET", false },
    { kSkewParamId, kPathColumn1, 638.0, "SKEW", false },
    { kCurveParamId, kPathColumn2, 662.0, "CURVE", false },
    { kLaneSlewParamId, kPathColumn1, 686.0, "JUMP SLEW", false },
    { kSeedParamId, kPathColumn2, 710.0, "SEED", false },
    { kOutParamId, kVoiceColumn1, 776.0, "OUT", false },
    { kPanParamId, kVoiceColumn1, 800.0, "PAN", false },
    { kVoiceModeParamId, kVoiceColumn1, 824.0, "POLYPHONY", true },
    { kTriggerParamId, kVoiceColumn1, 848.0, "TRIGGER", true },
    { kMidiParamId, kVoiceColumn1, 872.0, "MIDI", true },
    { kRootParamId, kVoiceColumn2, 776.0, "ROOT", false },
    { kTuneParamId, kVoiceColumn2, 800.0, "TUNE", false },
    { kFineParamId, kVoiceColumn2, 824.0, "FINE", false },
    { kAttackParamId, kVoiceColumn2, 848.0, "ATTACK", false },
    { kReleaseParamId, kVoiceColumn2, 872.0, "RELEASE", false },
    { kChannelModeParamId, kVoiceColumn3, 776.0, "CHANNEL MODE", true },
    { kStereoLinkParamId, kVoiceColumn3, 800.0, "STEREO LINK", true },
    { kMonoSpreadParamId, kVoiceColumn3, 824.0, "MONO SPREAD", false },
    { kVelocityParamId, kVoiceColumn3, 848.0, "VELOCITY", false },
#else
    { kOutputModeParamId, kVoiceColumn1, 592.0, "SOURCE MODE", true },
    { kActiveOutputsParamId, kVoiceColumn1, 618.0, "ACTIVE OUT", true },
    { kAllocationCadenceParamId, kVoiceColumn1, 644.0, "ALLOC CLOCK", true },
    { kTraversalParamId, kVoiceColumn2, 592.0, "TRAVERSAL", true },
    { kOutputWidthParamId, kVoiceColumn2, 618.0, "VOICE WIDTH", true },
    { kPairLayoutParamId, kVoiceColumn2, 644.0, "PAIR LAYOUT", true },
    { kAvoidAdjacentParamId, kVoiceColumn3, 592.0, "AVOID NEAR", true },
    { kTransportParamId, kLoopPanel, 222.0, "DIRECTION", true },
    { kRateBasisParamId, kLoopPanel, 244.0, "RATE BASIS", true },
    { kRateParamId, kLoopPanel, 266.0, "PLAY SPEED", false },
    { kStartParamId, kLoopPanel, 288.0, "START", false },
    { kEndParamId, kLoopPanel, 310.0, "END", false },
    { kLoopCrossfadeParamId, kLoopPanel, 332.0, "LOOP JOIN", false },
    { kPathParamId, kPathPanel, 405.0, "PATH", true },
    { kBlendParamId, kPathPanel, 430.0, "LANE CHANGE", true },
    { kShapeParamId, kPathPanel, 455.0, "SHAPE", true },
    { kCyclesParamId, kPathPanel, 480.0, "CYCLES", false },
    { kOffsetParamId, kPathPanel, 505.0, "OFFSET", false },
    { kSkewParamId, kPathPanel, 530.0, "SKEW", false },
    { kCurveParamId, kPathPanel, 555.0, "CURVE", false },
    { kLaneSlewParamId, kPathPanel, 580.0, "JUMP SLEW", false },
    { kSeedParamId, kPathPanel, 605.0, "SEED", false },
    { kOutParamId, kVoiceColumn1, 592.0, "OUT", false },
    { kPanParamId, kVoiceColumn1, 616.0, "PAN", false },
    { kVoiceModeParamId, kVoiceColumn1, 640.0, "VOICE", true },
    { kTriggerParamId, kVoiceColumn1, 664.0, "TRIGGER", true },
    { kRootParamId, kVoiceColumn2, 592.0, "ROOT", false },
    { kTuneParamId, kVoiceColumn2, 616.0, "TUNE", false },
    { kFineParamId, kVoiceColumn2, 640.0, "FINE", false },
    { kAttackParamId, kVoiceColumn2, 664.0, "ATTACK", false },
    { kReleaseParamId, kVoiceColumn3, 592.0, "RELEASE", false },
    { kVelocityParamId, kVoiceColumn3, 616.0, "VELOCITY", false },
    { kMidiParamId, kVoiceColumn3, 640.0, "MIDI", true },
#endif
};

NSRect laneRowRect(std::size_t lane) noexcept
{
    return NSMakeRect(28.0, 84.0 + static_cast<CGFloat>(lane) * 63.0,
        828.0, 57.0);
}

NSRect laneWaveRect(std::size_t lane) noexcept
{
    const NSRect row = laneRowRect(lane);
    return NSMakeRect(118.0, row.origin.y + 5.0, 600.0,
        row.size.height - 10.0);
}

NSRect laneLoadRect(std::size_t lane) noexcept
{
    const NSRect row = laneRowRect(lane);
    return NSMakeRect(726.0, row.origin.y + 8.0, 56.0, 16.0);
}

NSRect laneClearRect(std::size_t lane) noexcept
{
    const NSRect row = laneRowRect(lane);
    return NSMakeRect(790.0, row.origin.y + 8.0, 52.0, 16.0);
}

double normalizedParam(const ParamDef& def, double value) noexcept
{
    if ((def.id == kRateParamId || def.id == kCyclesParamId
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
            || def.id == kGrainDensityParamId
            || def.id == kGrainSizeParamId
#endif
            || (def.id >= kLane1SpeedParamId
                && def.id <= kLane4StretchParamId
                && (def.id - kLane1SpeedParamId) % 3u < 2u))
        && def.minimum > 0.0)
        return std::clamp(std::log(value / def.minimum)
            / std::log(def.maximum / def.minimum), 0.0, 1.0);
    if (def.id == kAttackParamId || def.id == kReleaseParamId
        || def.id == kLaneSlewParamId)
        return std::sqrt(std::clamp((value - def.minimum)
            / (def.maximum - def.minimum), 0.0, 1.0));
    return std::clamp((value - def.minimum) / (def.maximum - def.minimum),
        0.0, 1.0);
}

double paramFromNormalized(const ParamDef& def, double value) noexcept
{
    value = std::clamp(value, 0.0, 1.0);
    if ((def.id == kRateParamId || def.id == kCyclesParamId
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
            || def.id == kGrainDensityParamId
            || def.id == kGrainSizeParamId
#endif
            || (def.id >= kLane1SpeedParamId
                && def.id <= kLane4StretchParamId
                && (def.id - kLane1SpeedParamId) % 3u < 2u))
        && def.minimum > 0.0)
        return def.minimum * std::pow(def.maximum / def.minimum, value);
    if (def.id == kAttackParamId || def.id == kReleaseParamId
        || def.id == kLaneSlewParamId) value *= value;
    return def.minimum + value * (def.maximum - def.minimum);
}

#if defined(S3G_SAMPLE_GRAINS_VARIANT)
float grainDisplayWindow(GrainEnvelope envelope, float phase) noexcept
{
    constexpr float pi = 3.14159265358979323846f;
    phase = std::clamp(phase, 0.0f, 1.0f);
    switch (envelope) {
    case GrainEnvelope::Sine:
        return std::sin(pi * phase);
    case GrainEnvelope::Hann:
        return 0.5f - 0.5f * std::cos(2.0f * pi * phase);
    case GrainEnvelope::Triangle:
        return 1.0f - std::abs(2.0f * phase - 1.0f);
    case GrainEnvelope::Gaussian: {
        const float value = (phase - 0.5f) / 0.18f;
        return std::exp(-0.5f * value * value);
    }
    case GrainEnvelope::Parzen:
    default: {
        const float triangle = 1.0f - std::abs(2.0f * phase - 1.0f);
        return triangle * triangle * (3.0f - 2.0f * triangle);
    }
    }
}

#endif

using ManualPathPoints = std::array<LanePathPoint,
    s3g::sample::kMaximumLanePathPoints>;

uint32_t editableManualPath(Plugin& instance,
    ManualPathPoints& points) noexcept
{
    uint32_t count = manualPathSnapshot(instance, points);
    if (count >= 2u) return count;
    const float lane = static_cast<float>(paramValue(
        instance, kManualLaneParamId));
    points[0u] = { 0.0f, lane };
    points[1u] = { 1.0f, lane };
    return 2u;
}

void drawVoiceFooterButton(NSRect button, NSString* label, bool active,
    NSDictionary* attrs, const s3g::clap_gui::Style& style)
{
    [s3g::clap_gui::color(active ? 0x505050 : 0x383838) setFill];
    NSRectFill(button);
    [(active ? style.accent : s3g::clap_gui::color(0x777777)) setStroke];
    NSFrameRect(button);
    const NSSize size = [label sizeWithAttributes:attrs];
    [label drawAtPoint:NSMakePoint(
        NSMinX(button) + (NSWidth(button) - size.width) * 0.5,
        NSMinY(button) + (NSHeight(button) - size.height) * 0.5 - 0.5)
        withAttributes:attrs];
}

} // namespace

@interface S3GSampleLanesView : NSView <NSDraggingDestination> {
@private
    Plugin* _instance;
    NSTimer* _timer;
    clap_id _dragParam;
    NSRect _dragPanel;
    NSInteger _dragPathPoint;
    clap_id _openMenuParam;
    BOOL _presetMenuOpen;
    NSInteger _presetIndex;
    int _menuHover;
    NSTrackingArea* _trackingArea;
    uint32_t _feedbackMask;
    double _feedbackUntil;
    BOOL _outputRoutingPage;
    char _presetName[64];
}
- (instancetype)initWithPlugin:(Plugin*)instance;
- (void)startTimer;
- (void)stopTimer;
- (void)openSample:(std::size_t)lane;
- (BOOL)outputRoutingPage;
@end

@implementation S3GSampleLanesView

- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (instancetype)initWithPlugin:(Plugin*)instance
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (!self) return nil;
    _instance = instance;
    _dragParam = CLAP_INVALID_ID;
    _dragPathPoint = -1;
    _openMenuParam = CLAP_INVALID_ID;
    _menuHover = -1;
    _presetIndex = 0;
    _outputRoutingPage = NO;
    std::snprintf(_presetName, sizeof(_presetName), "%s", "INIT");
    [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    [self startTimer];
    return self;
}

- (BOOL)outputRoutingPage { return _outputRoutingPage; }

- (BOOL)rowIsVisible:(const UiRow&)row
{
    const BOOL multichannel = _instance->outputChannelCount > 2u;
    if (isOutputRoutingParam(row.id))
        return multichannel && _outputRoutingPage;
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    if (isStereoOutputParam(row.id))
        return !multichannel && !_outputRoutingPage;
#endif
    if (isVoicePageParam(row.id)) return !_outputRoutingPage;
    return YES;
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
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    serviceLoads(*_instance);
    const uint32_t feedback = _instance->actionFeedback.exchange(0u,
        std::memory_order_acq_rel);
    if (feedback != 0u) {
        _feedbackMask |= feedback;
        _feedbackUntil = CACurrentMediaTime() + 0.12;
    }
    if (_feedbackMask != 0u && CACurrentMediaTime() >= _feedbackUntil)
        _feedbackMask = 0u;
    [self setNeedsDisplay:YES];
}

- (NSString*)parameterText:(clap_id)id
{
    char text[64] {};
    if (!paramsValueToText(&_instance->plugin, id,
            paramValue(*_instance, id), text, sizeof(text)))
        std::snprintf(text, sizeof(text), "%.3f", paramValue(*_instance, id));
    return [NSString stringWithUTF8String:text];
}

- (void)drawSlider:(const UiRow&)row style:(const s3g::clap_gui::Style&)style
    labels:(NSDictionary*)labels values:(NSDictionary*)values
{
    const auto* def = paramDef(row.id);
    if (!def) return;
    s3g::clap_gui::drawProcessorSlider(
        [NSString stringWithUTF8String:row.label], [self parameterText:row.id],
        normalizedParam(*def, paramValue(*_instance, row.id)), row.y,
        NSMinX(row.panel), NSWidth(row.panel), labels, values, style);
}

- (void)drawMenu:(const UiRow&)row style:(const s3g::clap_gui::Style&)style
    labels:(NSDictionary*)labels values:(NSDictionary*)values
{
    s3g::clap_gui::drawProcessorMenu(
        [NSString stringWithUTF8String:row.label], [self parameterText:row.id],
        row.y, NSMinX(row.panel), NSWidth(row.panel), labels, values, style);
}

- (void)drawLaneTiming:(const s3g::clap_gui::Style&)style
    labels:(NSDictionary*)labels values:(NSDictionary*)values
{
    const std::size_t lane = std::min<std::size_t>(
        _instance->selectedLane.load(std::memory_order_acquire), 3u);
    s3g::clap_gui::drawPanelFrame(NSMinX(kLaneTimingPanel),
        NSMinY(kLaneTimingPanel), NSWidth(kLaneTimingPanel),
        NSHeight(kLaneTimingPanel), style);
    NSString* title = [NSString stringWithFormat:
        @"LANE %zu TIMING / STRETCH KEEPS PITCH", lane + 1u];
    s3g::clap_gui::drawPanelHeader(title, true,
        NSMinX(kLaneTimingPanel), NSMinY(kLaneTimingPanel),
        NSWidth(kLaneTimingPanel), 27.0, labels, style);
    const struct TimingControl {
        clap_id id;
        const char* label;
        NSRect panel;
        CGFloat y;
    } controls[] = {
        { laneSpeedParamId(lane), "SPEED", kTimingSpeedColumn,
            kTimingSpeedY },
        { laneStretchParamId(lane), "STRETCH", kTimingStretchColumn,
            kTimingStretchY },
        { laneNudgeParamId(lane), "WRAP NUDGE", kTimingNudgeColumn,
            kTimingNudgeY },
    };
    for (const auto& control : controls) {
        const auto* def = paramDef(control.id);
        if (!def) continue;
        s3g::clap_gui::drawProcessorSlider(
            [NSString stringWithUTF8String:control.label],
            [self parameterText:control.id],
            normalizedParam(*def, paramValue(*_instance, control.id)),
            control.y, NSMinX(control.panel), NSWidth(control.panel),
            labels, values, style);
    }
}

- (void)drawWaveform:(std::size_t)lane rect:(NSRect)rect
    style:(const s3g::clap_gui::Style&)style
{
    std::shared_ptr<const SampleAsset> asset = currentAsset(*_instance, lane);
    const double start = paramValue(*_instance, kStartParamId);
    const double end = paramValue(*_instance, kEndParamId);
    const double span = end - start;
    const double nudge = paramValue(*_instance, laneNudgeParamId(lane));
    [s3g::clap_gui::color(0x161616) setFill]; NSRectFill(rect);
    [style.grid setStroke]; NSFrameRect(rect);
    [s3g::clap_gui::color(0x343434) setStroke];
    NSBezierPath* zero = [NSBezierPath bezierPath];
    [zero moveToPoint:NSMakePoint(NSMinX(rect), NSMidY(rect))];
    [zero lineToPoint:NSMakePoint(NSMaxX(rect), NSMidY(rect))];
    [zero stroke];
    if (asset && asset->frameCount() != 0u) {
        const auto& samples = asset->channels[0u];
        NSBezierPath* waveform = [NSBezierPath bezierPath];
        [waveform setLineWidth:0.8];
        const std::size_t columns = std::max<std::size_t>(1u,
            static_cast<std::size_t>(NSWidth(rect)));
        const auto accumulateRange = [&](double firstNormalized,
                                         double lastNormalized,
                                         float& low, float& high) {
            firstNormalized = std::clamp(firstNormalized, 0.0, 1.0);
            lastNormalized = std::clamp(lastNormalized, 0.0, 1.0);
            if (lastNormalized < firstNormalized)
                std::swap(firstNormalized, lastNormalized);
            const double maximumFrame = static_cast<double>(
                samples.size() - 1u);
            const std::size_t first = std::min(samples.size() - 1u,
                static_cast<std::size_t>(std::floor(
                    firstNormalized * maximumFrame)));
            const std::size_t last = std::min(samples.size(),
                std::max(first + 1u, static_cast<std::size_t>(std::ceil(
                    lastNormalized * maximumFrame)) + 1u));
            for (std::size_t frame = first; frame < last; ++frame) {
                low = std::min(low, samples[frame]);
                high = std::max(high, samples[frame]);
            }
        };
        for (std::size_t column = 0u; column < columns; ++column) {
            const double visualFirst = static_cast<double>(column) / columns;
            const double visualLast = static_cast<double>(column + 1u)
                / columns;
            const double visualCenter = 0.5 * (visualFirst + visualLast);
            float low = 1.0f;
            float high = -1.0f;
            if (visualCenter >= start && visualCenter <= end
                && span > 0.0 && std::abs(nudge) > 1.0e-9) {
                const double timelineFirst = std::clamp(
                    (visualFirst - start) / span, 0.0, 1.0);
                const double timelineLast = std::clamp(
                    (visualLast - start) / span, 0.0, 1.0);
                const double sourceWidth = timelineLast - timelineFirst;
                if (sourceWidth >= 1.0) {
                    accumulateRange(start, end, low, high);
                } else {
                    const double sourceFirst = s3g::sample::laneSourcePhase(
                        timelineFirst, nudge);
                    const double sourceLast = sourceFirst + sourceWidth;
                    if (sourceLast <= 1.0) {
                        accumulateRange(start + sourceFirst * span,
                            start + sourceLast * span, low, high);
                    } else {
                        accumulateRange(start + sourceFirst * span, end,
                            low, high);
                        accumulateRange(start,
                            start + (sourceLast - 1.0) * span, low, high);
                    }
                }
            } else {
                accumulateRange(visualFirst, visualLast, low, high);
            }
            const CGFloat x = NSMinX(rect) + static_cast<CGFloat>(column);
            const CGFloat y1 = NSMidY(rect) - low * NSHeight(rect) * 0.43;
            const CGFloat y2 = NSMidY(rect) - high * NSHeight(rect) * 0.43;
            [waveform moveToPoint:NSMakePoint(x, y1)];
            [waveform lineToPoint:NSMakePoint(x, y2)];
        }
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
        [s3g::clap_gui::color(0x555555) setStroke];
#else
        [style.dim setStroke];
#endif
        [waveform stroke];
    }
    if (span > 0.0 && std::abs(nudge) > 1.0e-6) {
        const double seamPhase = s3g::sample::wrapLanePhase(-nudge);
        const CGFloat seamX = NSMinX(rect)
            + (start + seamPhase * span) * NSWidth(rect);
        [style.accent setStroke];
        NSBezierPath* seam = [NSBezierPath bezierPath];
        [seam setLineWidth:1.0];
        CGFloat dash[] { 3.0, 3.0 };
        [seam setLineDash:dash count:2 phase:0.0];
        [seam moveToPoint:NSMakePoint(seamX, NSMinY(rect))];
        [seam lineToPoint:NSMakePoint(seamX, NSMaxY(rect))];
        [seam stroke];
        [style.accent setFill];
        NSBezierPath* flag = [NSBezierPath bezierPath];
        [flag moveToPoint:NSMakePoint(seamX - 4.0, NSMaxY(rect) - 1.0)];
        [flag lineToPoint:NSMakePoint(seamX + 4.0, NSMaxY(rect) - 1.0)];
        [flag lineToPoint:NSMakePoint(seamX, NSMaxY(rect) - 7.0)];
        [flag closePath];
        [flag fill];
    }
    for (const auto& pair : { std::pair<double, NSColor*>(start, style.accent),
            std::pair<double, NSColor*>(end, style.text) }) {
        [pair.second setStroke];
        const CGFloat x = NSMinX(rect) + pair.first * NSWidth(rect);
        NSBezierPath* line = [NSBezierPath bezierPath];
        [line moveToPoint:NSMakePoint(x, NSMinY(rect))];
        [line lineToPoint:NSMakePoint(x, NSMaxY(rect))];
        [line stroke];
    }
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    if (span > 0.0) {
        const InstrumentSettings settings = settingsSnapshot(*_instance);
        const uint32_t grainCount = std::min<uint32_t>(
            _instance->grainCursorCount.load(std::memory_order_acquire),
            static_cast<uint32_t>(
                s3g::sample::kMaximumPublishedGrainCursors));
        const auto displayPosition = [&](double source) {
            double sourcePhase = (source - start) / span;
            sourcePhase -= std::floor(sourcePhase);
            return start + s3g::sample::laneTimelinePhase(
                sourcePhase, nudge) * span;
        };
        [NSGraphicsContext saveGraphicsState];
        NSRectClip(rect);
        for (uint32_t slot = 0u; slot < grainCount; ++slot) {
            const float weight = _instance->grainCursorLaneWeights[slot][lane]
                .load(std::memory_order_relaxed);
            const double grainStart =
                _instance->grainCursorLaneSourcePositions[slot][lane].load(
                    std::memory_order_relaxed);
            const double grainSpan =
                _instance->grainCursorLaneSourceSpans[slot][lane].load(
                    std::memory_order_relaxed);
            if (!(weight > 0.01f) || grainStart < 0.0) continue;
            const float currentPhase = std::clamp(
                _instance->grainCursorPhases[slot].load(
                    std::memory_order_relaxed), 0.0f, 1.0f);
            const float currentWindow = grainDisplayWindow(
                settings.grainEnvelope, currentPhase);
            const float baseGain = std::max(0.0f,
                _instance->grainCursorGains[slot].load(
                    std::memory_order_relaxed));
            const CGFloat activity = std::sqrt(std::clamp<CGFloat>(
                baseGain * currentWindow, 0.0, 1.0));
            const CGFloat weightLevel = std::sqrt(
                std::clamp<CGFloat>(weight, 0.0, 1.0));
            const CGFloat lineWidth = 0.75 + 1.65 * weightLevel;
            const auto pointsAtPhase = [&](float phase, NSPoint& top,
                                             NSPoint& bottom) {
                double source = grainStart + grainSpan * phase;
                source = start + (source - start
                    - std::floor((source - start) / span) * span);
                const CGFloat x = NSMinX(rect)
                    + displayPosition(source) * NSWidth(rect);
                const CGFloat amplitude = (4.0 + 8.0 * weight)
                    * grainDisplayWindow(settings.grainEnvelope, phase);
                top = NSMakePoint(x, NSMidY(rect) - amplitude);
                bottom = NSMakePoint(x,
                    NSMidY(rect) + amplitude);
            };

            std::vector<NSPoint> fillTop;
            std::vector<NSPoint> fillBottom;
            const auto fillBand = [&]() {
                if (fillTop.size() >= 2u) {
                    NSBezierPath* band = [NSBezierPath bezierPath];
                    [band moveToPoint:fillTop.front()];
                    for (std::size_t index = 1u; index < fillTop.size();
                         ++index)
                        [band lineToPoint:fillTop[index]];
                    for (std::size_t index = fillBottom.size(); index-- > 0u;)
                        [band lineToPoint:fillBottom[index]];
                    [band closePath];
                    [band fill];
                }
                fillTop.clear();
                fillBottom.clear();
            };
            [[s3g::clap_gui::color(0xd0d0d0) colorWithAlphaComponent:
                std::clamp<CGFloat>(0.035 + 0.12 * activity * weightLevel,
                    0.035, 0.155)] setFill];
            CGFloat previousX = 0.0;
            constexpr uint32_t windowPoints = 48u;
            for (uint32_t point = 0u; point <= windowPoints; ++point) {
                const float phase = static_cast<float>(point)
                    / static_cast<float>(windowPoints);
                NSPoint top;
                NSPoint bottom;
                pointsAtPhase(phase, top, bottom);
                if (point != 0u
                    && std::abs(top.x - previousX) > NSWidth(rect) * 0.5)
                    fillBand();
                fillTop.push_back(top);
                fillBottom.push_back(bottom);
                previousX = top.x;
            }
            fillBand();

            const auto strokeContour = [&](float firstPhase,
                                             float lastPhase,
                                             NSColor* color) {
                if (!(lastPhase - firstPhase > 1.0e-5f)) return;
                NSBezierPath* upper = [NSBezierPath bezierPath];
                NSBezierPath* lower = [NSBezierPath bezierPath];
                [upper setLineWidth:lineWidth];
                [lower setLineWidth:lineWidth];
                CGFloat lastX = 0.0;
                constexpr uint32_t contourPoints = 28u;
                for (uint32_t point = 0u; point <= contourPoints; ++point) {
                    const float phase = firstPhase
                        + (lastPhase - firstPhase)
                            * static_cast<float>(point)
                            / static_cast<float>(contourPoints);
                    NSPoint top;
                    NSPoint bottom;
                    pointsAtPhase(phase, top, bottom);
                    const bool discontinuity = point == 0u
                        || std::abs(top.x - lastX) > NSWidth(rect) * 0.5;
                    if (discontinuity) {
                        [upper moveToPoint:top];
                        [lower moveToPoint:bottom];
                    } else {
                        [upper lineToPoint:top];
                        [lower lineToPoint:bottom];
                    }
                    lastX = top.x;
                }
                [color setStroke];
                [upper stroke];
                [lower stroke];
            };
            const CGFloat weightOpacity = 0.55 + 0.45 * weightLevel;
            strokeContour(0.0f, currentPhase,
                [s3g::clap_gui::color(0x8a8a8a) colorWithAlphaComponent:
                    (0.28 + 0.28 * activity) * weightOpacity]);
            strokeContour(currentPhase, 1.0f,
                [s3g::clap_gui::color(0xe0e0e0) colorWithAlphaComponent:
                    (0.62 + 0.32 * activity) * weightOpacity]);

            double currentSource = grainStart + grainSpan * currentPhase;
            currentSource = start + (currentSource - start
                - std::floor((currentSource - start) / span) * span);
            const CGFloat currentX = NSMinX(rect)
                + displayPosition(currentSource) * NSWidth(rect);
            const CGFloat radius = 2.0 + 2.0
                * currentWindow;
            [[s3g::clap_gui::color(0x080808) colorWithAlphaComponent:0.92]
                setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
                currentX - radius - 1.5, NSMidY(rect) - radius - 1.5,
                radius * 2.0 + 3.0, radius * 2.0 + 3.0)] fill];
            [[s3g::clap_gui::color(0xf5f5f5) colorWithAlphaComponent:
                0.76 + 0.24 * activity] setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
                currentX - radius, NSMidY(rect) - radius,
                radius * 2.0, radius * 2.0)] fill];
        }
        [NSGraphicsContext restoreGraphicsState];
    }
#else
    const uint32_t count = std::min<uint32_t>(
        _instance->cursorCount.load(std::memory_order_acquire),
        static_cast<uint32_t>(s3g::sample::kMaximumLaneVoices));
    for (uint32_t slot = 0u; slot < count; ++slot) {
        const float weight = _instance->cursorLaneWeights[slot][lane].load(
            std::memory_order_relaxed);
        if (!(weight > 0.01f)) continue;
        const float source = _instance->cursorLaneSourcePositions[slot][lane]
            .load(std::memory_order_relaxed);
        double timelinePosition = source;
        if (span > 0.0 && std::abs(nudge) > 1.0e-9
            && source >= start && source <= end) {
            const double sourcePhase = (source - start) / span;
            timelinePosition = start + s3g::sample::laneTimelinePhase(
                sourcePhase, nudge) * span;
        }
        const CGFloat x = NSMinX(rect)
            + timelinePosition * NSWidth(rect);
        [[style.accent colorWithAlphaComponent:std::clamp<CGFloat>(
            weight, 0.15, 1.0)] setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            x - 3.0, NSMidY(rect) - 3.0, 6.0, 6.0)] fill];
    }
#endif
}

- (void)drawPathScope:(const s3g::clap_gui::Style&)style
    labels:(NSDictionary*)labels values:(NSDictionary*)values
{
    s3g::clap_gui::drawPanelFrame(NSMinX(kPathScope), NSMinY(kPathScope),
        NSWidth(kPathScope), NSHeight(kPathScope), style);
    s3g::clap_gui::drawPanelHeader(
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
        @"READ HEAD PATH / LIVE GRAIN EVENTS",
#else
        @"READ HEAD PATH / PATH CLOCK × FILE LANE",
#endif
        true, NSMinX(kPathScope), NSMinY(kPathScope), NSWidth(kPathScope),
        27.0, labels, style);
    [@"X  PATH CLOCK" drawAtPoint:NSMakePoint(30.0, 401.0)
        withAttributes:labels];
    [@"Y  FILE LANE" drawAtPoint:NSMakePoint(30.0, 418.0)
        withAttributes:labels];
    NSString* rule = paramValue(*_instance, kBlendParamId) < 0.5
        ? @"LANE CHANGE: EQUAL-POWER"
        : @"LANE CHANGE: SMOOTHED JUMP";
    [rule drawAtPoint:NSMakePoint(30.0, 451.0) withAttributes:values];
    [@"EMPTY: USE NEAREST LOADED" drawAtPoint:NSMakePoint(30.0, 468.0)
        withAttributes:values];
    const LanePath pathMode = static_cast<LanePath>(static_cast<uint8_t>(
        std::lround(paramValue(*_instance, kPathParamId))));
    NSString* pathRule = pathMode == LanePath::Manual
        ? @"ADD / DRAG • RIGHT-CLICK DELETE"
        : pathMode == LanePath::Random
        ? @"RANDOM: SEEDED 8-STEP PATH"
        : @"SHAPE + CLOCK MODIFIERS APPLY";
    [pathRule drawAtPoint:NSMakePoint(30.0, 485.0) withAttributes:values];
    if (pathMode == LanePath::Manual)
        [@"DIRECT PATH: MODIFIERS BYPASSED"
            drawAtPoint:NSMakePoint(30.0, 502.0) withAttributes:values];
    [s3g::clap_gui::color(0x141414) setFill]; NSRectFill(kPathGraph);
    [style.grid setStroke]; NSFrameRect(kPathGraph);
    for (std::size_t lane = 0u; lane < 4u; ++lane) {
        const CGFloat y = NSMinY(kPathGraph)
            + static_cast<CGFloat>(lane) * NSHeight(kPathGraph) / 3.0;
        [s3g::clap_gui::color(0x353535) setStroke];
        NSBezierPath* guide = [NSBezierPath bezierPath];
        [guide moveToPoint:NSMakePoint(NSMinX(kPathGraph), y)];
        [guide lineToPoint:NSMakePoint(NSMaxX(kPathGraph), y)];
        [guide stroke];
        [[NSString stringWithFormat:@"%zu", lane + 1u]
            drawAtPoint:NSMakePoint(NSMinX(kPathGraph) - 18.0, y - 6.0)
            withAttributes:values];
    }
    const SampleLanesSettings settings = settingsSnapshot(*_instance);
    NSBezierPath* path = [NSBezierPath bezierPath];
    [path setLineWidth:1.4];
    constexpr std::size_t points = 240u;
    for (std::size_t index = 0u; index < points; ++index) {
        const double horizontal = static_cast<double>(index)
            / static_cast<double>(points - 1u);
        const double phase = settings.path == LanePath::Manual
            ? horizontal
            : horizontal * settings.pathCycles + settings.pathOffset;
        const double lane = s3g::sample::sampleLanePathUnit(phase, settings);
        const NSPoint point = NSMakePoint(NSMinX(kPathGraph)
                + horizontal * NSWidth(kPathGraph),
            NSMinY(kPathGraph) + lane * NSHeight(kPathGraph));
        if (index == 0u) [path moveToPoint:point];
        else [path lineToPoint:point];
    }
    [style.accent setStroke]; [path stroke];
    if (settings.path == LanePath::Manual) {
        ManualPathPoints manualPoints {};
        const uint32_t manualPointCount = editableManualPath(
            *_instance, manualPoints);
        for (uint32_t index = 0u; index < manualPointCount; ++index) {
            const CGFloat x = NSMinX(kPathGraph)
                + manualPoints[index].phase * NSWidth(kPathGraph);
            const CGFloat y = NSMinY(kPathGraph)
                + manualPoints[index].lane * NSHeight(kPathGraph);
            [(static_cast<NSInteger>(index) == _dragPathPoint
                    ? style.text : style.accent) setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
                x - 4.0, y - 4.0, 8.0, 8.0)] fill];
            [s3g::clap_gui::color(0x141414) setStroke];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
                x - 4.0, y - 4.0, 8.0, 8.0)] stroke];
        }
    }
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    const uint32_t emitterCount = std::min<uint32_t>(
        _instance->cursorCount.load(std::memory_order_acquire),
        static_cast<uint32_t>(s3g::sample::kMaximumLaneVoices));
    for (uint32_t slot = 0u; slot < emitterCount; ++slot) {
        const double horizontal = std::clamp(
            static_cast<double>(_instance->cursorSourcePositions[slot].load(
                std::memory_order_relaxed)), 0.0, 1.0);
        const double lane = _instance->cursorLanePositions[slot].load(
            std::memory_order_relaxed);
        [style.dim setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            NSMinX(kPathGraph) + horizontal * NSWidth(kPathGraph) - 2.5,
            NSMinY(kPathGraph) + lane * NSHeight(kPathGraph) - 2.5,
            5.0, 5.0)] fill];
    }
    const uint32_t grainCount = std::min<uint32_t>(
        _instance->grainCursorCount.load(std::memory_order_acquire),
        static_cast<uint32_t>(s3g::sample::kMaximumPublishedGrainCursors));
    const auto grainEnvelope = static_cast<GrainEnvelope>(
        static_cast<uint8_t>(std::lround(
            paramValue(*_instance, kGrainEnvelopeParamId))));
    for (uint32_t slot = 0u; slot < grainCount; ++slot) {
        const double horizontal = std::clamp(static_cast<double>(
            _instance->grainCursorSourcePositions[slot].load(
                std::memory_order_relaxed)), 0.0, 1.0);
        const double lane = std::clamp(static_cast<double>(
            _instance->grainCursorLanePositions[slot].load(
                std::memory_order_relaxed)), 0.0, 1.0);
        const float phase = std::clamp(
            _instance->grainCursorPhases[slot].load(
                std::memory_order_relaxed), 0.0f, 1.0f);
        const CGFloat radius = 2.5 + 2.5
            * grainDisplayWindow(grainEnvelope, phase);
        [[style.accent colorWithAlphaComponent:0.78] setStroke];
        NSBezierPath* marker = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(NSMinX(kPathGraph)
                    + horizontal * NSWidth(kPathGraph) - radius,
                NSMinY(kPathGraph) + lane * NSHeight(kPathGraph) - radius,
                2.0 * radius, 2.0 * radius)];
        [marker setLineWidth:1.1];
        [marker stroke];
    }
#else
    const uint32_t count = std::min<uint32_t>(
        _instance->cursorCount.load(std::memory_order_acquire),
        static_cast<uint32_t>(s3g::sample::kMaximumLaneVoices));
    for (uint32_t slot = 0u; slot < count; ++slot) {
        const double source = _instance->cursorSourcePositions[slot].load(
            std::memory_order_relaxed);
        const double horizontal = std::clamp((source - settings.start)
            / (settings.end - settings.start), 0.0, 1.0);
        const double lane = _instance->cursorLanePositions[slot].load(
            std::memory_order_relaxed);
        [style.text setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            NSMinX(kPathGraph) + horizontal * NSWidth(kPathGraph) - 4.0,
            NSMinY(kPathGraph) + lane * NSHeight(kPathGraph) - 4.0,
            8.0, 8.0)] fill];
    }
#endif
}


- (NSRect)menuRectForParam:(clap_id)id
{
    for (const auto& row : kRows) if (row.id == id
        && [self rowIsVisible:row])
        return NSMakeRect(row.panel.origin.x + 104.0, row.y - 2.0,
            row.panel.size.width - 116.0, 18.0);
    return NSZeroRect;
}

- (NSRect)openMenuRect
{
    constexpr CGFloat height = 20.0;
    if (_presetMenuOpen) {
        const NSRect anchor = s3g::clap_gui::cocoaRect(
            kLanesTitleBand.presetMenu);
        return NSMakeRect(NSMinX(anchor), NSMaxY(anchor), NSWidth(anchor),
            height * std::size(kPresetItems));
    }
    const auto spec = lanesMenuSpec(_openMenuParam);
    const NSRect anchor = [self menuRectForParam:_openMenuParam];
    const uint32_t rows = spec.count == 0u ? 0u
        : (spec.count + spec.columns - 1u) / spec.columns;
    const CGFloat menuHeight = height * rows;
    const CGFloat below = NSMaxY(anchor);
    const CGFloat y = below + menuHeight <= kGuiHeight - 8.0
        ? below : NSMinY(anchor) - menuHeight;
    return NSMakeRect(NSMinX(anchor), y, NSWidth(anchor), menuHeight);
}

- (void)openSample:(std::size_t)lane
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setAllowedFileTypes:@[ @"wav", @"aif", @"aiff", @"caf",
        @"m4a", @"mp3", @"flac" ]];
    if ([panel runModal] == NSModalResponseOK) {
        _instance->selectedLane.store(static_cast<uint8_t>(lane),
            std::memory_order_release);
        queueSampleLoad(*_instance, lane, std::string([[[panel URL] path]
            UTF8String]));
    }
}

- (void)applyPreset:(NSInteger)index
{
    _presetIndex = std::clamp<NSInteger>(index, 0,
        static_cast<NSInteger>(kPresetNames.size() - 1u));
    const auto set = [&](clap_id id, double value) {
        queueGuiParamGesture(*_instance, id, value);
    };
    for (const auto& def : kParamDefs) set(def.id, def.defaultValue);
    ManualPathPoints manualPoints {};
    uint32_t manualPointCount = 0u;
    switch (_presetIndex) {
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    case 1:
        set(kGrainSourceModeParamId, 2.0); set(kGrainDensityParamId, 32.0);
        set(kGrainSizeParamId, 180.0); set(kPositionSprayParamId, 0.22);
        set(kGrainEnvelopeParamId, 4.0); break;
    case 2:
        set(kGrainSourceModeParamId, 1.0); set(kSourcePositionParamId, 0.5);
        set(kGrainSizeParamId, 300.0); set(kPitchSprayParamId, 0.16);
        set(kPathParamId, 3.0); set(kCyclesParamId, 0.25); break;
    case 3:
        set(kGrainSourceModeParamId, 3.0); set(kGrainMutateParamId, 1.0);
        set(kMutateAmountParamId, 1.0); set(kRegionCountParamId, 16.0);
        set(kPathParamId, 0.0); break;
    case 4:
        set(kGrainMutateParamId, 2.0); set(kMutateAmountParamId, 0.72);
        set(kGrainDensityParamId, 18.0); set(kPathParamId, 4.0); break;
    case 5:
        set(kGrainMutateParamId, 3.0); set(kMutateAmountParamId, 0.68);
        set(kGrainSizeParamId, 140.0); set(kPathParamId, 6.0); break;
    case 6:
        set(kGrainMutateParamId, 4.0); set(kGrainTimingParamId, 1.0);
        set(kSourceTimeSyncParamId, 1.0); set(kTraversalParamId, 4.0);
        set(kOutputModeParamId, 1.0); break;
    case 7:
        set(kPathParamId, 7.0); set(kManualLaneParamId, 0.5);
        manualPoints[0u] = { 0.0f, 0.5f };
        manualPoints[1u] = { 0.28f, 0.08f };
        manualPoints[2u] = { 0.62f, 0.92f };
        manualPoints[3u] = { 1.0f, 0.5f };
        manualPointCount = 4u;
        break;
#else
    case 1: break;
    case 2:
        set(kBlendParamId, 1.0); set(kLaneSlewParamId, 0.008); break;
    case 3:
        set(kPathParamId, 4.0); set(kBlendParamId, 1.0);
        set(kLaneSlewParamId, 0.004); break;
    case 4:
        set(kPathParamId, 3.0); set(kShapeParamId, 1.0);
        set(kCyclesParamId, 2.0); break;
    case 5:
        set(kPathParamId, 6.0); set(kBlendParamId, 1.0);
        set(kCyclesParamId, 4.0); set(kLaneSlewParamId, 0.003);
        set(kSeedParamId, 4312.0); break;
    case 6:
        set(kTransportParamId, 2.0); set(kPathParamId, 2.0);
        set(kShapeParamId, 1.0); break;
    case 7:
        set(kPathParamId, 7.0); set(kManualLaneParamId, 0.5);
        manualPoints[0u] = { 0.0f, 0.5f };
        manualPoints[1u] = { 0.28f, 0.08f };
        manualPoints[2u] = { 0.62f, 0.92f };
        manualPoints[3u] = { 1.0f, 0.5f };
        manualPointCount = 4u;
        break;
#endif
    default: break;
    }
    publishManualPath(*_instance, manualPoints, manualPointCount);
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        kPresetNames[static_cast<std::size_t>(_presetIndex)]);
    markStateDirty(*_instance);
}

- (clap_id)sliderAtPoint:(NSPoint)point panel:(NSRect*)panel
{
#if !defined(S3G_SAMPLE_GRAINS_VARIANT)
    const std::size_t lane = std::min<std::size_t>(
        _instance->selectedLane.load(std::memory_order_acquire), 3u);
    const struct TimingHit { clap_id id; NSRect rect; CGFloat y; } timing[] = {
        { laneSpeedParamId(lane), kTimingSpeedColumn, kTimingSpeedY },
        { laneStretchParamId(lane), kTimingStretchColumn, kTimingStretchY },
        { laneNudgeParamId(lane), kTimingNudgeColumn, kTimingNudgeY },
    };
    for (const auto& control : timing) {
        const NSRect hit = NSMakeRect(control.rect.origin.x + 8.0,
            control.y - 9.0, control.rect.size.width - 16.0, 20.0);
        if (NSPointInRect(point, hit)) {
            if (panel) *panel = control.rect;
            return control.id;
        }
    }
#endif
    for (const auto& row : kRows) {
        if (row.menu || ![self rowIsVisible:row]) continue;
        const NSRect hit = NSMakeRect(row.panel.origin.x + 8.0,
            row.y - 9.0, row.panel.size.width - 16.0, 20.0);
        if (NSPointInRect(point, hit)) {
            if (panel) *panel = row.panel;
            return row.id;
        }
    }
    return CLAP_INVALID_ID;
}

- (void)setDraggedParam:(clap_id)id point:(NSPoint)point panel:(NSRect)panel
{
    const auto* def = paramDef(id);
    if (!def) return;
    const CGFloat x = static_cast<CGFloat>(
        s3g::gui_layout::processorControlX(NSMinX(panel)));
    const CGFloat width = static_cast<CGFloat>(
        s3g::gui_layout::processorTrackWidth(NSWidth(panel)));
    const double normalized = std::clamp(static_cast<double>(
        (point.x - x) / width), 0.0, 1.0);
    queueGuiParamValue(*_instance, id,
        paramFromNormalized(*def, normalized));
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    const auto style = s3g::clap_gui::softTextStyle();
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    [style.bg setFill]; NSRectFill(self.bounds);
    s3g::clap_gui::drawProcessorTitleBand(
        [NSString stringWithUTF8String:_instance->plugin.desc->name],
        [NSString stringWithUTF8String:_presetName],
        s3g::clap_gui::peakDbText(_instance->outputPeak.load(
            std::memory_order_relaxed)), kLanesTitleBand,
        s3g::clap_gui::softTitleAttrs(), labels, values, style);

    s3g::clap_gui::drawPanelFrame(NSMinX(kSamplePanel), NSMinY(kSamplePanel),
        NSWidth(kSamplePanel), NSHeight(kSamplePanel), style);
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    s3g::clap_gui::drawPanelHeader(@"FOUR SAMPLE LANES / SHARED SOURCE WINDOW",
#else
    s3g::clap_gui::drawPanelHeader(@"FOUR LOOP LANES / SHARED START + END",
#endif
        true, NSMinX(kSamplePanel), NSMinY(kSamplePanel), NSWidth(kSamplePanel),
        27.0, labels, style);
    StorageMode mode;
    {
        std::lock_guard<std::mutex> lock(_instance->statusMutex);
        mode = _instance->storageMode;
    }
    NSString* storage = [NSString stringWithFormat:@"STORE %s",
        s3g::sample_storage::storageModeName(mode)];
    s3g::clap_gui::drawHeaderButton(kStorageButton, kSamplePanel, storage,
        mode != StorageMode::Link, labels, style);
    const std::size_t selected = std::min<std::size_t>(
        _instance->selectedLane.load(std::memory_order_acquire), 3u);
    for (std::size_t lane = 0u; lane < 4u; ++lane) {
        const NSRect row = laneRowRect(lane);
        [s3g::clap_gui::color(lane == selected ? 0x242424 : 0x1a1a1a)
            setFill]; NSRectFill(row);
        [(lane == selected ? style.accent : style.grid) setStroke];
        NSFrameRect(row);
        [[NSString stringWithFormat:@"LANE %zu", lane + 1u]
            drawAtPoint:NSMakePoint(NSMinX(row) + 8.0, NSMinY(row) + 8.0)
            withAttributes:labels];
        std::string statusText;
        {
            std::lock_guard<std::mutex> lock(_instance->statusMutex);
            statusText = _instance->statuses[lane];
        }
        const std::string prefix = "LANE " + std::to_string(lane + 1u)
            + " / ";
        if (statusText.rfind(prefix, 0u) == 0u)
            statusText.erase(0u, prefix.size());
        [NSGraphicsContext saveGraphicsState];
        NSRectClip(NSMakeRect(NSMinX(row) + 8.0, NSMinY(row) + 27.0,
            80.0, 18.0));
        [[NSString stringWithUTF8String:statusText.c_str()]
            drawAtPoint:NSMakePoint(NSMinX(row) + 8.0, NSMinY(row) + 28.0)
            withAttributes:values];
        [NSGraphicsContext restoreGraphicsState];
        [self drawWaveform:lane rect:laneWaveRect(lane) style:style];
        s3g::clap_gui::drawHeaderButton(laneLoadRect(lane), row, @"LOAD",
            lane == selected, labels, style);
        s3g::clap_gui::drawHeaderButton(laneClearRect(lane), row, @"CLEAR",
            currentAsset(*_instance, lane) != nullptr, labels, style);
    }
#if !defined(S3G_SAMPLE_GRAINS_VARIANT)
    [self drawLaneTiming:style labels:labels values:values];
#endif
    [self drawPathScope:style labels:labels values:values];
    NSString* voiceTitle = @"VOICE / PITCH / MIDI";
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    if (_instance->outputChannelCount == 2u)
        voiceTitle = @"VOICE / PITCH / MIDI / STEREO";
#endif
    NSString* outputTitle = [NSString stringWithFormat:@"%d  %@",
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
        5,
#else
        3,
#endif
        _outputRoutingPage ? @"OUTPUT / ROUTING" : voiceTitle];
    const struct Panel { NSRect rect; NSString* title; } panels[] = {
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
        { kLoopPanel, @"1  SOURCE SCAN / NORMAL RATE IS FILE SPEED" },
        { kGrainPanel, @"2  GRAIN SOURCE / WINDOW + TEXTURE" },
        { kPathPanel, @"3  SOURCE FIELD PATH / WHICH FILE LANE" },
        { kGrainEventPanel, @"4  GRAIN PROCESS / TIMING + PATTERN" },
        { kVoicePanel, outputTitle },
#else
        { kLoopPanel, @"1  LOOP TRANSPORT" },
        { kPathPanel, @"2  SOURCE FIELD PATH" },
        { kVoicePanel, outputTitle },
#endif
    };
    for (const auto& panel : panels) {
        s3g::clap_gui::drawPanelFrame(NSMinX(panel.rect), NSMinY(panel.rect),
            NSWidth(panel.rect), NSHeight(panel.rect), style);
        s3g::clap_gui::drawPanelHeader(panel.title, true,
            NSMinX(panel.rect), NSMinY(panel.rect), NSWidth(panel.rect),
            27.0, labels, style);
    }
    if (_instance->outputChannelCount > 2u)
        drawVoiceFooterButton(kOutputPageButton,
            _outputRoutingPage ? @"VOICE / PITCH" : @"ROUTING",
            _outputRoutingPage, labels, style);
    for (const auto& row : kRows) {
        if (![self rowIsVisible:row]) continue;
        if (row.menu) [self drawMenu:row style:style labels:labels values:values];
        else [self drawSlider:row style:style labels:labels values:values];
    }
    drawVoiceFooterButton(kPreviewButton, @"PLAY",
        (_feedbackMask & kActionPreview) != 0u, values, style);
    drawVoiceFooterButton(kStopButton,
        @"STOP / KILL ALL", (_feedbackMask & kActionStopAll) != 0u,
        values, style);
    if (_presetMenuOpen) {
        s3g::clap_gui::drawDropdownMenu([self openMenuRect], 20.0,
            kPresetItems, static_cast<uint32_t>(std::size(kPresetItems)),
            static_cast<int>(_presetIndex), _menuHover, values, style);
    } else if (_openMenuParam != CLAP_INVALID_ID) {
        const auto spec = lanesMenuSpec(_openMenuParam);
        const int selectedIndex = lanesMenuIndex(_openMenuParam,
            paramValue(*_instance, _openMenuParam));
        if (spec.columns > 1u)
            s3g::clap_gui::drawMultiColumnDropdownMenu([self openMenuRect],
                20.0, spec.items, spec.count, spec.columns, selectedIndex,
                _menuHover, values, style);
        else s3g::clap_gui::drawDropdownMenu([self openMenuRect], 20.0,
            spec.items, spec.count, selectedIndex, _menuHover, values, style);
    }
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (_presetMenuOpen || _openMenuParam != CLAP_INVALID_ID) {
        const bool preset = _presetMenuOpen;
        const auto spec = preset
            ? LanesMenuSpec { kPresetItems,
                static_cast<uint32_t>(std::size(kPresetItems)), 1u }
            : lanesMenuSpec(_openMenuParam);
        const int selected = spec.columns > 1u
            ? s3g::clap_gui::multiColumnDropdownHitIndex(point,
                [self openMenuRect], 20.0, spec.count, spec.columns)
            : s3g::clap_gui::dropdownHitIndex(point,
                [self openMenuRect], 20.0, spec.count);
        const clap_id id = _openMenuParam;
        _presetMenuOpen = NO;
        _openMenuParam = CLAP_INVALID_ID;
        _menuHover = -1;
        if (selected >= 0) {
            if (preset) [self applyPreset:selected];
            else queueGuiParamGesture(*_instance, id,
                lanesMenuValue(id, selected));
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
            kLanesTitleBand.presetMenu))) {
        _presetMenuOpen = YES;
        [self setNeedsDisplay:YES];
        return;
    }
    if (s3g::clap_gui::handleProcessorTitleClick(point,
            &_instance->plugin, [NSString stringWithUTF8String:
                _instance->plugin.desc->name], kLanesTitleBand,
            _presetName, sizeof(_presetName))) {
        _presetIndex = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (_instance->outputChannelCount > 2u
        && NSPointInRect(point, kOutputPageButton)) {
        _outputRoutingPage = !_outputRoutingPage;
        _presetMenuOpen = NO;
        _openMenuParam = CLAP_INVALID_ID;
        _menuHover = -1;
        _dragParam = CLAP_INVALID_ID;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, kStorageButton)) {
        StorageMode mode;
        {
            std::lock_guard<std::mutex> lock(_instance->statusMutex);
            mode = _instance->storageMode;
        }
        setStorageMode(*_instance, static_cast<StorageMode>(
            (static_cast<uint8_t>(mode) + 1u) % 3u));
        [self setNeedsDisplay:YES];
        return;
    }
    for (std::size_t lane = 0u; lane < 4u; ++lane) {
        if (NSPointInRect(point, laneLoadRect(lane))) {
            [self openSample:lane]; return;
        }
        if (NSPointInRect(point, laneClearRect(lane))) {
            clearLane(*_instance, lane); return;
        }
        if (NSPointInRect(point, laneRowRect(lane))) {
            _instance->selectedLane.store(static_cast<uint8_t>(lane),
                std::memory_order_release);
            if (NSPointInRect(point, laneWaveRect(lane))) {
                const NSRect wave = laneWaveRect(lane);
                const double normalized = std::clamp(static_cast<double>(
                    (point.x - NSMinX(wave)) / NSWidth(wave)), 0.0, 1.0);
                const double start = paramValue(*_instance, kStartParamId);
                const double end = paramValue(*_instance, kEndParamId);
                const double hitRadius = 10.0 / NSWidth(wave);
                const double startDistance = std::abs(normalized - start);
                const double endDistance = std::abs(normalized - end);
                if (std::min(startDistance, endDistance) <= hitRadius) {
                    _dragParam = startDistance < endDistance
                        ? kStartParamId : kEndParamId;
                    _dragPanel = wave;
                    queueGuiParamBegin(*_instance, _dragParam);
                    queueGuiParamValue(*_instance, _dragParam, normalized);
                }
            }
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (NSPointInRect(point, kPathGraph)) {
        queueGuiParamGesture(*_instance, kPathParamId,
            static_cast<double>(LanePath::Manual));
        ManualPathPoints points {};
        uint32_t count = editableManualPath(*_instance, points);
        const float phase = static_cast<float>(std::clamp(static_cast<double>(
            (point.x - NSMinX(kPathGraph)) / NSWidth(kPathGraph)), 0.0, 1.0));
        const float lane = static_cast<float>(std::clamp(static_cast<double>(
            (point.y - NSMinY(kPathGraph)) / NSHeight(kPathGraph)), 0.0, 1.0));
        NSInteger selected = -1;
        double selectedDistance = 12.0 * 12.0;
        for (uint32_t index = 0u; index < count; ++index) {
            const double x = NSMinX(kPathGraph)
                + points[index].phase * NSWidth(kPathGraph);
            const double y = NSMinY(kPathGraph)
                + points[index].lane * NSHeight(kPathGraph);
            const double distance = (point.x - x) * (point.x - x)
                + (point.y - y) * (point.y - y);
            if (distance <= selectedDistance) {
                selected = static_cast<NSInteger>(index);
                selectedDistance = distance;
            }
        }
        if (selected < 0 && count < points.size()) {
            uint32_t insert = 1u;
            while (insert < count && points[insert].phase < phase) ++insert;
            const bool nearStart = phase <= 10.0 / NSWidth(kPathGraph);
            const bool nearEnd = phase >= 1.0
                - 10.0 / NSWidth(kPathGraph);
            if (nearStart || nearEnd) {
                insert = nearStart ? 0u : count - 1u;
                points[insert].lane = lane;
            } else {
                for (uint32_t index = count; index > insert; --index)
                    points[index] = points[index - 1u];
                points[insert] = { phase, lane };
                ++count;
            }
            selected = static_cast<NSInteger>(insert);
        } else if (selected >= 0) {
            points[static_cast<std::size_t>(selected)].lane = lane;
        }
        publishManualPath(*_instance, points, count);
        _dragPathPoint = selected;
        [self setNeedsDisplay:YES];
        return;
    }
    for (const auto& row : kRows) if ([self rowIsVisible:row] && row.menu
        && NSPointInRect(point, [self menuRectForParam:row.id])) {
        _openMenuParam = row.id;
        _menuHover = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, kPreviewButton)) {
        requestAction(*_instance, kActionPreview); return;
    }
    if (NSPointInRect(point, kStopButton)) {
        requestAction(*_instance, kActionStopAll); return;
    }
    NSRect panel {};
    const clap_id id = [self sliderAtPoint:point panel:&panel];
    if (id == CLAP_INVALID_ID) return;
    double defaultValue = 0.0;
    if (s3g::clap_gui::sliderDoubleClickDefault(event, &_instance->plugin,
            id, &defaultValue)) {
        queueGuiParamGesture(*_instance, id, defaultValue);
        return;
    }
    _dragParam = id;
    _dragPanel = panel;
    queueGuiParamBegin(*_instance, id);
    [self setDraggedParam:id point:point panel:panel];
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (_dragPathPoint >= 0) {
        ManualPathPoints points {};
        const uint32_t count = manualPathSnapshot(*_instance, points);
        const uint32_t index = static_cast<uint32_t>(_dragPathPoint);
        if (count < 2u || index >= count) return;
        points[index].lane = static_cast<float>(std::clamp(static_cast<double>(
            (point.y - NSMinY(kPathGraph)) / NSHeight(kPathGraph)), 0.0, 1.0));
        if (index != 0u && index + 1u != count) {
            const float phase = static_cast<float>(std::clamp(static_cast<double>(
                (point.x - NSMinX(kPathGraph)) / NSWidth(kPathGraph)), 0.0, 1.0));
            constexpr float minimumGap = 0.002f;
            points[index].phase = std::clamp(phase,
                points[index - 1u].phase + minimumGap,
                points[index + 1u].phase - minimumGap);
        }
        publishManualPath(*_instance, points, count);
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragParam == CLAP_INVALID_ID) return;
    if (_dragPanel.size.width > 600.0) {
        const double normalized = std::clamp(static_cast<double>(
            (point.x - NSMinX(_dragPanel)) / NSWidth(_dragPanel)), 0.0, 1.0);
        queueGuiParamValue(*_instance, _dragParam, normalized);
    } else [self setDraggedParam:_dragParam point:point panel:_dragPanel];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragPathPoint = -1;
    if (_dragParam != CLAP_INVALID_ID)
        queueGuiParamEnd(*_instance, _dragParam);
    _dragParam = CLAP_INVALID_ID;
}

- (void)rightMouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (!NSPointInRect(point, kPathGraph)
        || static_cast<LanePath>(static_cast<uint8_t>(std::lround(
            paramValue(*_instance, kPathParamId)))) != LanePath::Manual)
        return;
    ManualPathPoints points {};
    uint32_t count = manualPathSnapshot(*_instance, points);
    if (count <= 2u) return;
    NSInteger selected = -1;
    double selectedDistance = 12.0 * 12.0;
    for (uint32_t index = 1u; index + 1u < count; ++index) {
        const double x = NSMinX(kPathGraph)
            + points[index].phase * NSWidth(kPathGraph);
        const double y = NSMinY(kPathGraph)
            + points[index].lane * NSHeight(kPathGraph);
        const double distance = (point.x - x) * (point.x - x)
            + (point.y - y) * (point.y - y);
        if (distance <= selectedDistance) {
            selected = static_cast<NSInteger>(index);
            selectedDistance = distance;
        }
    }
    if (selected < 0) return;
    const uint32_t remove = static_cast<uint32_t>(selected);
    for (uint32_t index = remove; index + 1u < count; ++index)
        points[index] = points[index + 1u];
    publishManualPath(*_instance, points, count - 1u);
    [self setNeedsDisplay:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (!_presetMenuOpen && _openMenuParam == CLAP_INVALID_ID) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    const auto spec = _presetMenuOpen
        ? LanesMenuSpec { kPresetItems,
            static_cast<uint32_t>(std::size(kPresetItems)), 1u }
        : lanesMenuSpec(_openMenuParam);
    const int hover = spec.columns > 1u
        ? s3g::clap_gui::multiColumnDropdownHitIndex(point,
            [self openMenuRect], 20.0, spec.count, spec.columns)
        : s3g::clap_gui::dropdownHitIndex(point,
            [self openMenuRect], 20.0, spec.count);
    if (hover != _menuHover) {
        _menuHover = hover;
        [self setNeedsDisplay:YES];
    }
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
    const NSPoint point = [self convertPoint:[sender draggingLocation]
        fromView:nil];
    std::size_t firstLane = _instance->selectedLane.load(
        std::memory_order_acquire);
    for (std::size_t lane = 0u; lane < 4u; ++lane)
        if (NSPointInRect(point, laneRowRect(lane))) firstLane = lane;
    const NSUInteger count = std::min<NSUInteger>([urls count], 4u);
    for (NSUInteger index = 0u; index < count; ++index) {
        const std::size_t lane = (firstLane + index) % 4u;
        queueSampleLoad(*_instance, lane, std::string([[[urls
            objectAtIndex:index] path] UTF8String]));
    }
    _instance->selectedLane.store(static_cast<uint8_t>(firstLane),
        std::memory_order_release);
    return YES;
}

@end
