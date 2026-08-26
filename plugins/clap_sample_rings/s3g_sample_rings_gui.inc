@interface S3GSampleRingsView : NSView <NSDraggingDestination> {
    Plugin* _instance;
    NSTimer* _timer;
    clap_id _dragParam;
    NSRect _dragPanel;
    NSRect _dragTrack;
    clap_id _openMenuParam;
    BOOL _presetMenuOpen;
    NSRect _menuAnchor;
    int _menuHover;
    uint32_t _selectedHead;
    uint32_t _soloRestoreMask;
    int32_t _soloHead;
    BOOL _polarDrag;
    clap_id _polarRingParam;
    clap_id _polarPhaseParam;
    char _presetName[64];
}
- (id)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (BOOL)loadDocumentationSources;
- (void)openPresetMenu;
- (void)applyFactoryPreset:(uint32_t)index;
- (void)markPresetEdited;
@end

namespace {

const NSRect kRingPanel = NSMakeRect(18.0, 42.0, 820.0, 680.0);
const NSRect kOutputBar = NSMakeRect(18.0, 732.0, 820.0, 60.0);
const NSRect kCapturePanel = NSMakeRect(850.0, 42.0, 488.0, 134.0);
const NSRect kMotionPanel = NSMakeRect(850.0, 186.0, 488.0, 254.0);
const NSRect kHeadPanel = NSMakeRect(850.0, 450.0, 488.0, 166.0);
const NSRect kSlotPanel = NSMakeRect(850.0, 626.0, 488.0, 192.0);
constexpr CGFloat kPanelHeaderHeight = 27.0;
constexpr CGFloat kControlTop = 10.0;
constexpr CGFloat kRowStep = 27.0;
constexpr CGFloat kColumnGap = 10.0;
constexpr double kTwoPi = 6.28318530717958647692;

NSPoint ringCenter()
{ return NSMakePoint(NSMidX(kRingPanel), NSMinY(kRingPanel) + 336.0); }
constexpr CGFloat kInnerRingRadius = 34.0;
constexpr CGFloat kOuterRingRadius = 292.0;

struct GuiRingRef {
    uint8_t slot = 0u;
    uint8_t channel = 0u;
};

struct GuiRingCatalog {
    std::array<GuiRingRef, s3g::sample::kSampleRingsMaximumRings> rings {};
    std::array<int, kSourceCount> first {{ -1, -1, -1, -1 }};
    std::array<int, kSourceCount> last {{ -1, -1, -1, -1 }};
    uint32_t count = 0u;
};

GuiRingCatalog makeGuiRingCatalog(const Plugin& plugin)
{
    GuiRingCatalog result;
    for (uint8_t slot = 0u; slot < kSourceCount; ++slot) {
        const auto asset = plugin.sources[slot];
        if (!asset) continue;
        result.first[slot] = static_cast<int>(result.count);
        const uint8_t width = std::min<uint8_t>(asset->channelCount, 8u);
        for (uint8_t channel = 0u; channel < width
             && result.count < result.rings.size(); ++channel)
            result.rings[result.count++] = { slot, channel };
        result.last[slot] = static_cast<int>(result.count) - 1;
    }
    return result;
}

CGFloat ringRadius(uint32_t ring, uint32_t count)
{
    if (count <= 1u) return (kInnerRingRadius + kOuterRingRadius) * 0.5;
    return kInnerRingRadius + (kOuterRingRadius - kInnerRingRadius)
        * static_cast<CGFloat>(ring) / static_cast<CGFloat>(count - 1u);
}

CGFloat ringPitch(uint32_t count)
{
    return count <= 1u ? 20.0
        : (kOuterRingRadius - kInnerRingRadius)
            / static_cast<CGFloat>(count - 1u);
}

NSPoint polarPoint(CGFloat radius, float phase)
{
    const CGFloat angle = static_cast<CGFloat>(phase * kTwoPi);
    const NSPoint center = ringCenter();
    return NSMakePoint(center.x + std::sin(angle) * radius,
        center.y - std::cos(angle) * radius);
}

CGFloat pointRadius(NSPoint point)
{
    const NSPoint center = ringCenter();
    return std::hypot(point.x - center.x, point.y - center.y);
}

float pointPhase(NSPoint point)
{
    const NSPoint center = ringCenter();
    double phase = std::atan2(point.x - center.x, center.y - point.y)
        / kTwoPi;
    if (phase < 0.0) phase += 1.0;
    return static_cast<float>(phase);
}

uint8_t guiFormationSize(uint32_t formation)
{
    switch (static_cast<SampleRingsHeadFormation>(std::min(formation, 3u))) {
    case SampleRingsHeadFormation::Pairs: return 2u;
    case SampleRingsHeadFormation::Quads: return 4u;
    case SampleRingsHeadFormation::Field8: return 8u;
    case SampleRingsHeadFormation::Free:
    default: return 1u;
    }
}

uint8_t guiFormationLeader(uint32_t head, uint32_t formation)
{
    const uint8_t size = guiFormationSize(formation);
    return static_cast<uint8_t>(head - head % size);
}

int findGuiRing(const GuiRingCatalog& catalog, uint8_t slot,
    uint8_t channel)
{
    for (uint32_t ring = 0u; ring < catalog.count; ++ring)
        if (catalog.rings[ring].slot == slot
            && catalog.rings[ring].channel == channel)
            return static_cast<int>(ring);
    return -1;
}

NSColor* slotColor(uint32_t slot, bool selected)
{
    constexpr int colors[] { 0x989898, 0x909090, 0x888888, 0x808080 };
    return s3g::clap_gui::color(selected ? 0xd8d8d8
        : colors[std::min(slot, 3u)]);
}

NSColor* headColor(uint32_t head, bool selected)
{
    constexpr int colors[] {
        0xf4f4f4, 0xdedede, 0xc8c8c8, 0xb2b2b2,
        0x9d9d9d, 0x898989, 0x767676, 0x646464,
    };
    return selected ? s3g::clap_gui::color(0xffffff)
        : s3g::clap_gui::color(colors[std::min(head, 7u)]);
}

NSRect captureButtonRect()
{ return NSMakeRect(NSMaxX(kCapturePanel) - 100.0,
    NSMinY(kCapturePanel) + 4.0, 88.0, 19.0); }
NSRect playButtonRect()
{ return NSMakeRect(NSMaxX(kRingPanel) - 232.0,
    NSMinY(kRingPanel) + 4.0, 52.0, 19.0); }
NSRect pauseButtonRect()
{ return NSMakeRect(NSMaxX(kRingPanel) - 174.0,
    NSMinY(kRingPanel) + 4.0, 58.0, 19.0); }
NSRect stopButtonRect()
{ return NSMakeRect(NSMaxX(kRingPanel) - 110.0,
    NSMinY(kRingPanel) + 4.0, 48.0, 19.0); }
NSRect syncButtonRect()
{ return NSMakeRect(NSMaxX(kRingPanel) - 56.0,
    NSMinY(kRingPanel) + 4.0, 44.0, 19.0); }
NSRect storageButtonRect()
{ return NSMakeRect(NSMaxX(kRingPanel) - 368.0,
    NSMinY(kRingPanel) + 4.0, 130.0, 19.0); }
NSRect slotLoadRect()
{ return NSMakeRect(NSMaxX(kSlotPanel) - 232.0,
    NSMinY(kSlotPanel) + 4.0, 66.0, 19.0); }
NSRect slotClearRect()
{ return NSMakeRect(NSMaxX(kSlotPanel) - 160.0,
    NSMinY(kSlotPanel) + 4.0, 66.0, 19.0); }
NSRect reverseButtonRect()
{ return NSMakeRect(NSMaxX(kSlotPanel) - 88.0,
    NSMinY(kSlotPanel) + 4.0, 76.0, 19.0); }
NSRect radialReverseButtonRect()
{ return NSMakeRect(NSMaxX(kMotionPanel) - 180.0,
    NSMinY(kMotionPanel) + 4.0, 80.0, 19.0); }
NSRect angularReverseButtonRect()
{ return NSMakeRect(NSMaxX(kMotionPanel) - 92.0,
    NSMinY(kMotionPanel) + 4.0, 80.0, 19.0); }
NSRect headMuteButtonRect()
{ return NSMakeRect(NSMaxX(kHeadPanel) - 140.0,
    NSMinY(kHeadPanel) + 4.0, 62.0, 19.0); }
NSRect headSoloButtonRect()
{ return NSMakeRect(NSMaxX(kHeadPanel) - 72.0,
    NSMinY(kHeadPanel) + 4.0, 60.0, 19.0); }

NSRect outputGainCell()
{ return NSMakeRect(NSMinX(kOutputBar) + 68.0,
    NSMinY(kOutputBar) + 8.0, 240.0, 24.0); }
NSRect outputFormatCell()
{ return NSMakeRect(NSMinX(kOutputBar) + 316.0,
    NSMinY(kOutputBar) + 8.0, 240.0, 24.0); }
NSRect outputRotateCell()
{ return NSMakeRect(NSMinX(kOutputBar) + 564.0,
    NSMinY(kOutputBar) + 8.0, 240.0, 24.0); }
NSRect outputMidiCell()
{ return NSMakeRect(NSMinX(kOutputBar) + 68.0,
    NSMinY(kOutputBar) + 34.0, 240.0, 24.0); }
NSRect outputRootCell()
{ return NSMakeRect(NSMinX(kOutputBar) + 316.0,
    NSMinY(kOutputBar) + 34.0, 240.0, 24.0); }

NSRect slotBadgeRect(uint32_t slot)
{
    const CGFloat width = 185.0;
    return NSMakeRect(NSMinX(kRingPanel) + 24.0 + slot * 196.0,
        NSMaxY(kRingPanel) - 31.0, width, 22.0);
}

NSRect headButtonRect(uint32_t head)
{
    const CGFloat width = 52.0;
    return NSMakeRect(NSMinX(kHeadPanel) + 10.0 + head * 58.0,
        NSMinY(kHeadPanel) + 34.0, width, 21.0);
}

NSRect controlCell(NSRect panel, uint32_t column, uint32_t row,
    CGFloat top = kControlTop)
{
    const CGFloat width = (NSWidth(panel) - kColumnGap) * 0.5;
    return NSMakeRect(NSMinX(panel) + column * (width + kColumnGap),
        NSMinY(panel) + kPanelHeaderHeight + top + row * kRowStep,
        width, 24.0);
}

struct GuiRow {
    clap_id id;
    const char* label;
    NSRect panel;
    uint8_t column;
    uint8_t row;
    bool menu;
};

const std::array<GuiRow, 22u> kGlobalRows {{
    { kCaptureTargetParamId, "TARGET", kCapturePanel, 0u, 0u, true },
    { kCaptureModeParamId, "MODE", kCapturePanel, 1u, 0u, true },
    { kInputWidthParamId, "INPUT WIDTH", kCapturePanel, 0u, 1u, true },
    { kCaptureQuantizeParamId, "QUANTIZE", kCapturePanel, 1u, 1u, true },
    { kMonitorParamId, "MONITOR", kCapturePanel, 0u, 2u, true },
    { kOverdubFeedbackParamId, "OVERDUB FB", kCapturePanel, 1u, 2u, false },

    { kRingPathParamId, "RADIAL PATH", kMotionPanel, 0u, 0u, true },
    { kFormationParamId, "FORMATION", kMotionPanel, 1u, 0u, true },
    { kRingPositionParamId, "RING POSITION", kMotionPanel, 0u, 1u, false },
    { kRelationshipParamId, "ANGULAR MODEL", kMotionPanel, 1u, 1u, true },
    { kRadialRatioParamId, "RADIAL RATIO", kMotionPanel, 0u, 2u, false },
    { kPlaybackRateParamId, "MASTER RATE", kMotionPanel, 1u, 2u, false },
    { kPathDepthParamId, "PATH DEPTH", kMotionPanel, 0u, 3u, false },
    { kRelationshipAmountParamId, "ANGLE AMOUNT", kMotionPanel, 1u, 3u, false },
    { kPathOffsetParamId, "PATH OFFSET", kMotionPanel, 0u, 4u, false },
    { kCenterParamId, "HEAD PIVOT", kMotionPanel, 1u, 4u, false },
    { kPathSpreadParamId, "FORM SPREAD", kMotionPanel, 0u, 5u, false },
    { kDriftParamId, "ANGLE DRIFT", kMotionPanel, 1u, 5u, false },
    { kRingBlendParamId, "RING BLEND", kMotionPanel, 0u, 6u, false },
    { kGlideParamId, "ANGLE GLIDE", kMotionPanel, 1u, 6u, false },
    { kPathSlewParamId, "PATH SLEW", kMotionPanel, 0u, 7u, false },
    { kSeedParamId, "MOTION SEED", kMotionPanel, 1u, 7u, false },
}};

const std::array<const char*, 9u> kSlotLabels {{
    "START", "END", "SPEED", "STRETCH", "PITCH", "WRAP NUDGE",
    "GAIN", "LOOP JOIN", "SEAM DUCK",
}};

NSRect slotControlCell(uint32_t row)
{
    constexpr uint8_t columns[] { 0u, 1u, 0u, 1u, 0u, 1u, 0u, 1u, 0u };
    constexpr uint8_t rows[] { 0u, 0u, 1u, 1u, 2u, 2u, 3u, 3u, 4u };
    return controlCell(kSlotPanel, columns[row], rows[row]);
}

clap_id selectedSlotParam(const Plugin& plugin, uint32_t row)
{
    if (row == 7u) return kLoopJoinParamId;
    if (row == 8u) return kSeamDuckParamId;
    const uint32_t slot = std::min(plugin.targets.selectedSlot.load(), 3u);
    return slotParamId(slot, row);
}

NSRect headControlCell(uint32_t index)
{
    constexpr uint8_t columns[] { 0u, 1u, 0u, 1u, 0u, 1u };
    constexpr uint8_t rows[] { 0u, 0u, 1u, 1u, 2u, 2u };
    return controlCell(kHeadPanel, columns[index], rows[index], 38.0);
}

clap_id headControlParam(uint32_t head, uint32_t index)
{
    if (index == 0u) return kManualRingParamBase + head;
    if (index == 1u) return kManualPhaseParamBase + head;
    return kManualRateParamBase + head;
}

struct MenuItems {
    std::array<NSString*, 9u> items {};
    uint32_t count = 0u;
};

struct FactoryPresetItems {
    std::array<NSString*, 11u> items {{
        @"INIT FIELD", @"SLOW FIELD", @"FOUR LOOP PAIRS",
        @"QUAD BREATHING", @"OUTWARD CANON", @"COUNTER SPIRAL",
        @"STEPPED RELAY", @"RANDOM CONSTELLATION", @"ORBITAL DRIFT",
        @"REVERSE BLOOM", @"STEREO FOLD",
    }};
};

const FactoryPresetItems& factoryPresetItems()
{
    static const FactoryPresetItems presets;
    return presets;
}

MenuItems menuItems(clap_id id)
{
    MenuItems result;
    const auto fill = [&](std::initializer_list<NSString*> values) {
        for (NSString* value : values) result.items[result.count++] = value;
    };
    if (id == kCaptureTargetParamId || id == kSelectedSlotParamId)
        fill({ @"A", @"B", @"C", @"D" });
    else if (id == kCaptureModeParamId)
        fill({ @"REPLACE", @"OVERDUB", @"PUNCH" });
    else if (id == kInputWidthParamId)
        fill({ @"AUTO", @"1", @"2", @"3", @"4", @"5", @"6", @"7", @"8" });
    else if (id == kCaptureQuantizeParamId)
        fill({ @"FREE", @"BEAT", @"BAR", @"2 BARS", @"4 BARS", @"8 BARS" });
    else if (id == kMonitorParamId)
        fill({ @"LOOP", @"INPUT", @"BOTH" });
    else if (id == kRingPathParamId)
        fill({ @"FIXED RING", @"OUTWARD", @"INWARD", @"BOUNCE",
            @"SINE SWEEP", @"STEPPED BANDS", @"RANDOM WALK", @"MANUAL" });
    else if (id == kFormationParamId)
        fill({ @"FREE HEADS", @"LINKED PAIRS", @"LINKED QUADS", @"FIELD 8" });
    else if (id == kRelationshipParamId)
        fill({ @"UNISON", @"CANON", @"FAN", @"RATIO", @"SHEAR", @"ORBIT", @"DRIFT", @"MANUAL" });
    else if (id == kOutputFormatParamId)
        fill({ @"8 DIRECT", @"QUAD RING", @"STEREO RING" });
    else if (id == kMidiModeParamId)
        fill({ @"OFF", @"GATE", @"TRIGGER" });
    return result;
}

bool anySource(const Plugin& plugin)
{
    return std::any_of(plugin.sources.begin(), plugin.sources.end(),
        [](const std::shared_ptr<const SampleAsset>& source) {
            return source != nullptr;
        });
}

struct ControlPresentation {
    bool active = true;
    bool emphasized = false;
    NSString* reason = nil;
};

ControlPresentation controlPresentation(const Plugin& plugin, clap_id id)
{
    const auto ringPath = static_cast<SampleRingsRingPath>(std::min(
        plugin.targets.ringPath.load(), 7u));
    const auto relationship = static_cast<SampleRingsRelationship>(std::min(
        plugin.targets.relationship.load(), 7u));
    const auto formation = static_cast<SampleRingsHeadFormation>(std::min(
        plugin.targets.formation.load(), 3u));
    const bool radialManual = ringPath == SampleRingsRingPath::Manual;
    const bool radialMoving = ringPath != SampleRingsRingPath::Fixed
        && !radialManual;
    const bool angularManual = relationship
        == SampleRingsRelationship::Manual;
    const auto inactive = [](NSString* reason) {
        return ControlPresentation { false, false, reason };
    };

    uint32_t slot = 0u;
    clap_id offset = 0u;
    if (decodeSlotParam(id, slot, offset)) {
        if (!plugin.sources[slot]) return inactive(@"LOAD SLOT");
        return {};
    }
    if (id >= kManualRingParamBase
        && id < kManualRingParamBase + kHeadCount)
        return radialManual ? ControlPresentation { true, true, nil }
            : inactive(@"RAD MANUAL");
    if ((id >= kManualPhaseParamBase
            && id < kManualPhaseParamBase + kHeadCount)
        || (id >= kManualRateParamBase
            && id < kManualRateParamBase + kHeadCount))
        return angularManual ? ControlPresentation { true, true, nil }
            : inactive(@"ANG MANUAL");

    switch (id) {
    case kOverdubFeedbackParamId:
        return plugin.targets.captureMode.load()
                == static_cast<uint32_t>(CaptureMode::Overdub)
            ? ControlPresentation {} : inactive(@"OVERDUB");
    case kRingPositionParamId:
        return radialManual ? inactive(@"RAD MANUAL")
            : ControlPresentation {};
    case kRadialRatioParamId:
    case kPathDepthParamId:
    case kPathOffsetParamId:
    case kRadialReverseParamId:
        return radialMoving ? ControlPresentation {}
            : inactive(radialManual ? @"RAD MANUAL" : @"FIXED");
    case kPathSpreadParamId:
        if (radialManual) return inactive(@"RAD MANUAL");
        return formation == SampleRingsHeadFormation::Field8
            ? inactive(@"FIELD 8") : ControlPresentation {};
    case kRelationshipAmountParamId:
        return relationship == SampleRingsRelationship::Unison
                || angularManual
            ? inactive(angularManual ? @"ANG MANUAL" : @"UNISON")
            : ControlPresentation {};
    case kCenterParamId:
        return relationship == SampleRingsRelationship::Fan
                || relationship == SampleRingsRelationship::Shear
            ? ControlPresentation {} : inactive(@"FAN / SHEAR");
    case kGlideParamId:
        return relationship == SampleRingsRelationship::Fan
                || relationship == SampleRingsRelationship::Ratio
                || relationship == SampleRingsRelationship::Shear
                || relationship == SampleRingsRelationship::Orbit
                || relationship == SampleRingsRelationship::Drift
            ? ControlPresentation {} : inactive(@"NO RATE MOD");
    case kSeedParamId:
        return ringPath == SampleRingsRingPath::Random
                || relationship == SampleRingsRelationship::Drift
                || plugin.targets.drift.load() > 0.0f
            ? ControlPresentation {} : inactive(@"RANDOM / DRIFT");
    case kRingBlendParamId:
        return makeGuiRingCatalog(plugin).count > 1u
            ? ControlPresentation {} : inactive(@"1 RING");
    case kLoopJoinParamId:
        return anySource(plugin) ? ControlPresentation {}
            : inactive(@"NO SOURCE");
    case kSeamDuckParamId:
        if (!anySource(plugin)) return inactive(@"NO SOURCE");
        return plugin.targets.loopJoin.load() > 0.0f
            ? ControlPresentation {} : inactive(@"JOIN 0");
    case kOutputRotationParamId:
        return plugin.targets.outputFormat.load() == 0u
            ? inactive(@"8 DIRECT") : ControlPresentation {};
    case kMidiRootParamId:
        return plugin.targets.midiMode.load() == 0u
            ? inactive(@"MIDI OFF") : ControlPresentation {};
    default: return {};
    }
}

bool queueFactoryPreset(Plugin& plugin, uint32_t index)
{
    if (index >= factoryPresetItems().items.size()) return false;
    const auto& definitions = paramDefs();
    std::vector<std::pair<clap_id, double>> values;
    values.reserve(definitions.size());
    for (const auto& definition : definitions)
        values.emplace_back(definition.id, definition.defaultValue);
    const auto set = [&](clap_id id, double value) {
        const auto found = std::find_if(values.begin(), values.end(),
            [id](const auto& item) { return item.first == id; });
        if (found != values.end()) found->second = value;
    };

    // Factory motion changes should never produce an output-level jump.
    set(kOutputGainParamId, plugin.targets.outputGain.load());
    switch (index) {
    case 0u: break;
    case 1u: // Slow Field
        set(kRingPathParamId, 4.0); set(kFormationParamId, 3.0);
        set(kRadialRatioParamId, 0.35); set(kPathDepthParamId, 0.82);
        set(kRelationshipParamId, 1.0); set(kRelationshipAmountParamId, 0.25);
        set(kPlaybackRateParamId, 0.8); set(kPathSlewParamId, 220.0);
        break;
    case 2u: // Four Loop Pairs
        set(kRingPathParamId, 7.0); set(kFormationParamId, 1.0);
        set(kRelationshipParamId, 7.0); set(kRingBlendParamId, 0.72);
        set(kLoopJoinParamId, 0.06);
        for (uint32_t pair = 0u; pair < 4u; ++pair) {
            const double ring = static_cast<double>(pair) / 3.0;
            set(kManualRingParamBase + pair * 2u, ring);
            set(kManualRingParamBase + pair * 2u + 1u, ring);
        }
        for (uint32_t head = 0u; head < kHeadCount; ++head) {
            set(kManualPhaseParamBase + head,
                static_cast<double>(head) / kHeadCount);
            set(kManualRateParamBase + head, 1.0);
        }
        break;
    case 3u: // Quad Breathing
        set(kRingPathParamId, 4.0); set(kFormationParamId, 2.0);
        set(kRadialRatioParamId, 0.45); set(kPathDepthParamId, 0.84);
        set(kPathSpreadParamId, 0.55); set(kRelationshipParamId, 2.0);
        set(kRelationshipAmountParamId, 0.35); set(kGlideParamId, 350.0);
        set(kRingBlendParamId, 0.85);
        break;
    case 4u: // Outward Canon
        set(kRingPathParamId, 1.0); set(kFormationParamId, 0.0);
        set(kRadialRatioParamId, 0.75); set(kPathSpreadParamId, 0.45);
        set(kRelationshipParamId, 1.0); set(kRelationshipAmountParamId, 0.88);
        set(kRingBlendParamId, 0.65); set(kPathSlewParamId, 70.0);
        break;
    case 5u: // Counter Spiral
        set(kRingPathParamId, 2.0); set(kFormationParamId, 0.0);
        set(kRadialRatioParamId, 0.60); set(kPathSpreadParamId, 0.62);
        set(kRelationshipParamId, 2.0); set(kRelationshipAmountParamId, -0.85);
        set(kAngularReverseParamId, 1.0); set(kRingBlendParamId, 0.78);
        break;
    case 6u: // Stepped Relay
        set(kRingPathParamId, 5.0); set(kFormationParamId, 1.0);
        set(kRadialRatioParamId, 0.75); set(kPathSpreadParamId, 0.40);
        set(kRelationshipParamId, 4.0); set(kRelationshipAmountParamId, 0.65);
        set(kPathSlewParamId, 25.0); set(kRingBlendParamId, 0.25);
        break;
    case 7u: // Random Constellation
        set(kRingPathParamId, 6.0); set(kFormationParamId, 0.0);
        set(kRadialRatioParamId, 0.55); set(kPathDepthParamId, 0.90);
        set(kPathSpreadParamId, 0.70); set(kPathSlewParamId, 320.0);
        set(kRelationshipParamId, 6.0); set(kRelationshipAmountParamId, 0.65);
        set(kDriftParamId, 0.35); set(kSeedParamId, 8249.0);
        break;
    case 8u: // Orbital Drift
        set(kRingPathParamId, 4.0); set(kFormationParamId, 0.0);
        set(kRadialRatioParamId, 0.28); set(kPathDepthParamId, 0.75);
        set(kPathSpreadParamId, 0.85); set(kRelationshipParamId, 5.0);
        set(kRelationshipAmountParamId, 0.80); set(kDriftParamId, 0.20);
        set(kGlideParamId, 600.0);
        break;
    case 9u: // Reverse Bloom
        set(kRingPathParamId, 3.0); set(kFormationParamId, 2.0);
        set(kRadialRatioParamId, 0.42); set(kPathDepthParamId, 0.80);
        set(kPathSpreadParamId, 0.70); set(kRelationshipParamId, 1.0);
        set(kRelationshipAmountParamId, -0.75);
        set(kAngularReverseParamId, 1.0);
        break;
    case 10u: // Stereo Fold
        set(kRingPathParamId, 3.0); set(kFormationParamId, 1.0);
        set(kRadialRatioParamId, 0.50); set(kRelationshipParamId, 5.0);
        set(kRelationshipAmountParamId, 0.55); set(kOutputFormatParamId, 2.0);
        set(kOutputRotationParamId, 22.5);
        break;
    default: break;
    }

    using Kind = s3g::clap_gui::ParamEventKind;
    std::vector<s3g::clap_gui::ParamEvent> events;
    events.reserve(values.size() * 3u);
    for (const auto& value : values) {
        events.push_back({ Kind::GestureBegin, value.first, 0.0 });
        events.push_back({ Kind::Value, value.first, value.second });
        events.push_back({ Kind::GestureEnd, value.first, 0.0 });
    }
    if (!plugin.guiParamEvents.pushBatch(events.data(),
            static_cast<uint32_t>(events.size()))) return false;
    for (const auto& value : values) setParam(plugin, value.first, value.second);
    requestGuiParamService(plugin);
    markDirty(plugin);
    return true;
}

NSString* valueText(Plugin& plugin, clap_id id)
{
    char text[64] {};
    paramsValueToText(&plugin.plugin, id, paramValue(plugin, id), text,
        sizeof(text));
    return [NSString stringWithUTF8String:text];
}

void drawControl(Plugin& plugin, const char* label, clap_id id, NSRect cell,
    bool menu, NSDictionary* labels, NSDictionary* values,
    const s3g::clap_gui::Style& style,
    ControlPresentation presentation = {})
{
    auto controlStyle = style;
    NSDictionary* controlLabels = labels;
    NSDictionary* controlValues = values;
    NSString* displayedValue = valueText(plugin, id);
    if (!presentation.active) {
        controlStyle.strip = s3g::clap_gui::color(0x111111);
        controlStyle.grid = s3g::clap_gui::color(0x303030);
        controlStyle.fill = s3g::clap_gui::color(0x292929);
        controlStyle.text = s3g::clap_gui::color(0x5d5d5d);
        controlLabels = s3g::clap_gui::textAttrs(
            s3g::clap_gui::color(0x626262), 10.0);
        controlValues = s3g::clap_gui::textAttrs(
            s3g::clap_gui::color(0x686868), 10.0);
        displayedValue = presentation.reason ? presentation.reason : @"N/A";
    } else if (presentation.emphasized) {
        controlStyle.grid = s3g::clap_gui::color(0x668078);
        controlStyle.fill = s3g::clap_gui::color(0x78958c);
        controlLabels = s3g::clap_gui::textAttrs(
            s3g::clap_gui::color(0xc3d2cd), 10.0);
        controlValues = s3g::clap_gui::textAttrs(
            s3g::clap_gui::color(0xb8cbc5), 10.0);
    }
    NSString* name = [NSString stringWithUTF8String:label];
    if (menu) s3g::clap_gui::drawProcessorMenu(name,
        displayedValue, NSMinY(cell), NSMinX(cell), NSWidth(cell),
        controlLabels, controlValues, controlStyle);
    else {
        const auto* definition = paramDef(id);
        const double normalized = definition
            ? std::clamp((paramValue(plugin, id) - definition->minimum)
                / (definition->maximum - definition->minimum), 0.0, 1.0)
            : 0.0;
        s3g::clap_gui::drawProcessorSlider(name, displayedValue,
            normalized, NSMinY(cell), NSMinX(cell), NSWidth(cell), controlLabels,
            controlValues, controlStyle);
    }
}

void drawReadOnlyControl(NSString* label, NSString* value, NSRect cell,
    NSDictionary* labels, NSDictionary* values,
    const s3g::clap_gui::Style& style)
{
    auto readStyle = style;
    readStyle.strip = s3g::clap_gui::color(0x171717);
    readStyle.grid = s3g::clap_gui::color(0x414141);
    NSDictionary* readValues = s3g::clap_gui::textAttrs(
        s3g::clap_gui::color(0xb4b4b4), 10.0);
    s3g::clap_gui::drawReadOnlyValue(label, value, NSMinY(cell), labels,
        readValues, readStyle,
        s3g::gui_layout::processorLabelX(NSMinX(cell)),
        s3g::gui_layout::processorControlX(NSMinX(cell)),
        s3g::gui_layout::processorMenuWidth(NSWidth(cell)));
    (void)values;
}

void drawCircularWaveform(const SampleAsset& asset, uint8_t channel,
    CGFloat baseRadius, CGFloat pitch, float nudge, NSColor* color)
{
    if (channel >= asset.channelCount || asset.frameCount() == 0u) return;
    constexpr uint32_t segments = 320u;
    constexpr uint32_t samplesPerBin = 12u;
    struct Bin {
        float peak = 0.0f;
        float rms = 0.0f;
        float mean = 0.0f;
    };
    std::array<Bin, segments + 1u> bins {};
    const auto& samples = asset.channels[channel];
    const std::size_t window = std::max<std::size_t>(1u,
        samples.size() / segments);
    float channelPeak = 1.0e-8f;
    for (uint32_t point = 0u; point < segments; ++point) {
        double sourcePhase = static_cast<double>(point) / segments + nudge;
        sourcePhase -= std::floor(sourcePhase);
        const std::size_t start = std::min(samples.size() - 1u,
            static_cast<std::size_t>(sourcePhase * samples.size()));
        double sum = 0.0;
        double squareSum = 0.0;
        float peak = 0.0f;
        for (uint32_t tap = 0u; tap < samplesPerBin; ++tap) {
            const std::size_t offset = window * tap / samplesPerBin;
            const float sample = samples[(start + offset) % samples.size()];
            sum += sample;
            squareSum += static_cast<double>(sample) * sample;
            peak = std::max(peak, std::abs(sample));
        }
        bins[point].peak = peak;
        bins[point].rms = static_cast<float>(std::sqrt(
            squareSum / samplesPerBin));
        bins[point].mean = static_cast<float>(sum / samplesPerBin);
        channelPeak = std::max(channelPeak, peak);
    }
    bins[segments] = bins[0u];
    const CGFloat amplitude = std::clamp(pitch * 0.38, 1.6, 4.6);

    const auto drawEnvelope = [&](bool usePeak, CGFloat alpha) {
        NSBezierPath* envelope = [NSBezierPath bezierPath];
        for (uint32_t point = 0u; point <= segments; ++point) {
            const float level = (usePeak ? bins[point].peak : bins[point].rms)
                / channelPeak;
            const NSPoint position = polarPoint(baseRadius
                + amplitude * std::clamp(level, 0.0f, 1.0f),
                static_cast<float>(point) / segments);
            point == 0u ? [envelope moveToPoint:position]
                : [envelope lineToPoint:position];
        }
        for (uint32_t point = segments + 1u; point-- > 0u;) {
            const float level = (usePeak ? bins[point].peak : bins[point].rms)
                / channelPeak;
            [envelope lineToPoint:polarPoint(baseRadius
                - amplitude * std::clamp(level, 0.0f, 1.0f),
                static_cast<float>(point) / segments)];
        }
        [envelope closePath];
        [[color colorWithAlphaComponent:alpha] setFill];
        [envelope fill];
    };
    drawEnvelope(true, 0.10);
    drawEnvelope(false, 0.24);

    NSBezierPath* contour = [NSBezierPath bezierPath];
    [contour setLineWidth:0.72];
    for (uint32_t point = 0u; point <= segments; ++point) {
        const float mean = std::clamp(bins[point].mean / channelPeak,
            -1.0f, 1.0f);
        const NSPoint position = polarPoint(baseRadius
            + static_cast<CGFloat>(mean) * amplitude * 0.70,
            static_cast<float>(point) / segments);
        point == 0u ? [contour moveToPoint:position]
            : [contour lineToPoint:position];
    }
    [[color colorWithAlphaComponent:0.72] setStroke];
    [contour stroke];
}

void previewRouting(Plugin& plugin, uint8_t leader, uint8_t size,
    float radial, const GuiRingCatalog& catalog)
{
    if (catalog.count == 0u) return;
    const auto formation = static_cast<SampleRingsHeadFormation>(
        std::min(plugin.targets.formation.load(), 3u));
    if (formation == SampleRingsHeadFormation::Free) {
        const double position = radial * (catalog.count - 1u);
        const uint32_t first = std::min(catalog.count - 1u,
            static_cast<uint32_t>(std::floor(position)));
        const uint32_t second = std::min(catalog.count - 1u, first + 1u);
        const float mix = static_cast<float>(position - std::floor(position));
        plugin.headRings[leader].store(first);
        plugin.headRingTargets[leader].store(second);
        plugin.headSlots[leader].store(catalog.rings[first].slot);
        plugin.headSlotTargets[leader].store(catalog.rings[second].slot);
        plugin.headChannels[leader].store(catalog.rings[first].channel);
        plugin.headChannelTargets[leader].store(catalog.rings[second].channel);
        plugin.headRingMixes[leader].store(mix);
        plugin.headRadialPositions[leader].store(radial);
        return;
    }
    std::array<uint8_t, kSourceCount> active {};
    uint32_t activeCount = 0u;
    for (uint8_t slot = 0u; slot < kSourceCount; ++slot)
        if (plugin.sources[slot]) active[activeCount++] = slot;
    if (activeCount == 0u) return;
    const double position = radial * std::max(1u, activeCount - 1u);
    const uint32_t first = std::min(activeCount - 1u,
        static_cast<uint32_t>(std::floor(position)));
    const uint32_t second = std::min(activeCount - 1u, first + 1u);
    const float mix = static_cast<float>(position - std::floor(position));
    for (uint8_t member = 0u; member < size; ++member) {
        const uint8_t head = leader + member;
        const uint8_t slotA = active[first];
        const uint8_t slotB = active[second];
        const uint8_t widthA = std::min<uint8_t>(
            plugin.sources[slotA]->channelCount, 8u);
        const uint8_t widthB = std::min<uint8_t>(
            plugin.sources[slotB]->channelCount, 8u);
        const uint8_t channelA = head % widthA;
        const uint8_t channelB = head % widthB;
        const int ringA = findGuiRing(catalog, slotA, channelA);
        const int ringB = findGuiRing(catalog, slotB, channelB);
        if (ringA < 0 || ringB < 0) continue;
        plugin.headRings[head].store(static_cast<uint32_t>(ringA));
        plugin.headRingTargets[head].store(static_cast<uint32_t>(ringB));
        plugin.headSlots[head].store(slotA);
        plugin.headSlotTargets[head].store(slotB);
        plugin.headChannels[head].store(channelA);
        plugin.headChannelTargets[head].store(channelB);
        plugin.headRingMixes[head].store(mix);
        plugin.headRadialPositions[head].store(radial);
        plugin.headFormationLeaders[head].store(leader);
    }
}

} // namespace

@implementation S3GSampleRingsView

- (id)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiW, kGuiH)];
    if (self) {
        _instance = plugin;
        _timer = nil;
        _dragParam = CLAP_INVALID_ID;
        _dragTrack = NSZeroRect;
        _openMenuParam = CLAP_INVALID_ID;
        _presetMenuOpen = NO;
        _menuHover = -1;
        _selectedHead = 0u;
        _soloRestoreMask = 0xffu;
        _soloHead = -1;
        _polarDrag = NO;
        _polarRingParam = CLAP_INVALID_ID;
        _polarPhaseParam = CLAP_INVALID_ID;
        std::snprintf(_presetName, sizeof(_presetName), "%s", "CURRENT");
        [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 30.0 target:self
        selector:@selector(refresh:) userInfo:nil repeats:YES];
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
    serviceLoads(*_instance);
    if (_instance->captureCallbackPending.load(std::memory_order_acquire))
        finalizeCaptures(*_instance);
    if (![self isHidden] && s3g::clap_support::hostAppIsActive())
        [self setNeedsDisplay:YES];
}

- (NSRect)openMenuRect
{
    const uint32_t count = _presetMenuOpen
        ? static_cast<uint32_t>(factoryPresetItems().items.size())
        : menuItems(_openMenuParam).count;
    const CGFloat width = std::max<CGFloat>(190.0, NSWidth(_menuAnchor));
    const CGFloat height = 20.0 * count;
    CGFloat y = NSMaxY(_menuAnchor) + 2.0;
    if (y + height > kGuiH - 5.0) y = NSMinY(_menuAnchor) - height - 2.0;
    return NSMakeRect(NSMinX(_menuAnchor), y, width, height);
}

- (void)openMenu:(clap_id)id anchor:(NSRect)anchor
{
    _presetMenuOpen = NO;
    _openMenuParam = id;
    _menuAnchor = anchor;
    _menuHover = -1;
    [self setNeedsDisplay:YES];
}

- (void)openPresetMenu
{
    const auto band = s3g::clap_gui::encoderTitleBand(kGuiW, kGuiH);
    _openMenuParam = CLAP_INVALID_ID;
    _presetMenuOpen = YES;
    _menuAnchor = s3g::clap_gui::cocoaRect(band.presetMenu);
    _menuHover = -1;
    [self setNeedsDisplay:YES];
}

- (void)applyFactoryPreset:(uint32_t)index
{
    if (!queueFactoryPreset(*_instance, index)) {
        NSBeep();
        return;
    }
    NSString* name = factoryPresetItems().items[index];
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        [name UTF8String]);
    _soloHead = -1;
    [self setNeedsDisplay:YES];
}

- (void)markPresetEdited
{
    const std::size_t length = std::strlen(_presetName);
    if (length > 0u && _presetName[length - 1u] == '*') return;
    if (length + 1u < sizeof(_presetName)) {
        _presetName[length] = '*';
        _presetName[length + 1u] = '\0';
    } else if (sizeof(_presetName) > 1u) {
        _presetName[sizeof(_presetName) - 2u] = '*';
        _presetName[sizeof(_presetName) - 1u] = '\0';
    }
}

- (void)setDraggedValue:(NSPoint)point
{
    const auto* definition = paramDef(_dragParam);
    if (!definition) return;
    const bool direct = NSWidth(_dragTrack) > 0.0;
    const CGFloat x = direct ? NSMinX(_dragTrack)
        : s3g::gui_layout::processorControlX(NSMinX(_dragPanel));
    const CGFloat width = direct ? NSWidth(_dragTrack)
        : s3g::gui_layout::processorTrackWidth(NSWidth(_dragPanel));
    const double normalized = std::clamp(static_cast<double>(
        (point.x - x) / width), 0.0, 1.0);
    queueGuiParamValue(*_instance, _dragParam, definition->minimum
        + normalized * (definition->maximum - definition->minimum));
    [self markPresetEdited];
    markDirty(*_instance);
    [self setNeedsDisplay:YES];
}

- (void)setPolarValue:(NSPoint)point
{
    const GuiRingCatalog catalog = makeGuiRingCatalog(*_instance);
    if (catalog.count == 0u) return;
    const uint32_t formation = std::min(_instance->targets.formation.load(),
        3u);
    const uint8_t size = guiFormationSize(formation);
    const uint8_t leader = guiFormationLeader(_selectedHead, formation);
    const CGFloat radius = std::clamp(pointRadius(point), kInnerRingRadius,
        kOuterRingRadius);
    float radial = static_cast<float>((radius - kInnerRingRadius)
        / (kOuterRingRadius - kInnerRingRadius));
    if (formation != static_cast<uint32_t>(SampleRingsHeadFormation::Free)) {
        std::array<CGFloat, kSourceCount> mappedRadii {};
        uint32_t count = 0u;
        for (uint8_t slot = 0u; slot < kSourceCount; ++slot) {
            const auto asset = _instance->sources[slot];
            if (!asset) continue;
            const uint8_t width = std::min<uint8_t>(asset->channelCount, 8u);
            const int ring = findGuiRing(catalog, slot,
                static_cast<uint8_t>(_selectedHead % width));
            if (ring >= 0) mappedRadii[count++] = ringRadius(
                static_cast<uint32_t>(ring), catalog.count);
        }
        if (count <= 1u) radial = 0.0f;
        else if (radius <= mappedRadii[0u]) radial = 0.0f;
        else if (radius >= mappedRadii[count - 1u]) radial = 1.0f;
        else {
            for (uint32_t index = 0u; index + 1u < count; ++index) {
                if (radius < mappedRadii[index]
                    || radius > mappedRadii[index + 1u]) continue;
                const CGFloat local = (radius - mappedRadii[index])
                    / std::max<CGFloat>(1.0,
                        mappedRadii[index + 1u] - mappedRadii[index]);
                radial = static_cast<float>((index + local) / (count - 1u));
                break;
            }
        }
    }
    const float phase = pointPhase(point);
    queueGuiParamValue(*_instance, _polarRingParam, radial);
    queueGuiParamValue(*_instance, _polarPhaseParam, phase);
    _instance->headPhases[_selectedHead].store(phase);
    _instance->headPhaseTargets[_selectedHead].store(phase);
    previewRouting(*_instance, leader, size, radial, catalog);
    [self markPresetEdited];
    markDirty(*_instance);
    [self setNeedsDisplay:YES];
}

- (void)openSource:(uint32_t)slot
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.allowedFileTypes = @[@"wav", @"aif", @"aiff", @"caf", @"mp3", @"m4a"];
    if ([panel runModal] != NSModalResponseOK) return;
    const char* path = panel.URL.path.fileSystemRepresentation;
    if (path) queueSourceLoad(*_instance, slot, path);
    [self setNeedsDisplay:YES];
}

- (BOOL)loadDocumentationSources
{
    constexpr const char* names[] {
        "S3G_GUI_DOCUMENTATION_SAMPLE_PATH",
        "S3G_GUI_DOCUMENTATION_SAMPLE_PATH_2",
        "S3G_GUI_DOCUMENTATION_SAMPLE_PATH_3",
        "S3G_GUI_DOCUMENTATION_SAMPLE_PATH_4",
    };
    bool loaded = false;
    for (uint32_t slot = 0u; slot < kSourceCount; ++slot) {
        const char* path = std::getenv(names[slot]);
        if (!path || !path[0] || !loadSourceFromPath(*_instance, slot, path))
            continue;
        loaded = true;
    }
    if (!loaded) return NO;
    setParam(*_instance, kRingPathParamId,
        static_cast<double>(SampleRingsRingPath::Bounce));
    setParam(*_instance, kFormationParamId,
        static_cast<double>(SampleRingsHeadFormation::Free));
    setParam(*_instance, kRelationshipParamId,
        static_cast<double>(SampleRingsRelationship::Orbit));
    setParam(*_instance, kRelationshipAmountParamId, 0.68);
    setParam(*_instance, kRingPositionParamId, 0.52);
    setParam(*_instance, kPathDepthParamId, 0.82);
    setParam(*_instance, kPathSpreadParamId, 0.38);
    setParam(*_instance, kRingBlendParamId, 0.82);
    setParam(*_instance, kDriftParamId, 0.18);
    setParam(*_instance, slotParamId(1u, kSlotSpeed), 0.75);
    setParam(*_instance, slotParamId(1u, kSlotNudge), -0.12);
    setParam(*_instance, slotParamId(2u, kSlotStretch), 1.35);
    setParam(*_instance, slotParamId(2u, kSlotPitch), 7.0);
    setParam(*_instance, slotParamId(3u, kSlotSpeed), 1.5);
    setParam(*_instance, slotParamId(3u, kSlotNudge), 0.19);
    const GuiRingCatalog catalog = makeGuiRingCatalog(*_instance);
    for (uint32_t head = 0u; head < kHeadCount; ++head) {
        const float radial = static_cast<float>(head)
            / static_cast<float>(kHeadCount - 1u);
        _instance->headPhases[head].store(static_cast<float>(head)
            / static_cast<float>(kHeadCount));
        _instance->headPhaseTargets[head].store(std::fmod(
            0.18f + static_cast<float>(head) / kHeadCount, 1.0f));
        _instance->headRates[head].store(0.65f
            + static_cast<float>(head) * 0.15f);
        previewRouting(*_instance, static_cast<uint8_t>(head), 1u, radial,
            catalog);
    }
    return YES;
}

- (void)drawRingField:(NSDictionary*)labels values:(NSDictionary*)values
    style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawPanelFrame(NSMinX(kRingPanel), NSMinY(kRingPanel),
        NSWidth(kRingPanel), NSHeight(kRingPanel), style);
    s3g::clap_gui::drawPanelHeader(@"CONCENTRIC SOURCE FIELD / 32 RINGS MAX",
        true, NSMinX(kRingPanel), NSMinY(kRingPanel), NSWidth(kRingPanel),
        kPanelHeaderHeight, labels, style);
    const bool playing = _instance->targets.playing.load() != 0u;
    const bool stopped = _instance->transportStopped.load();
    s3g::clap_gui::drawToolboxHeaderButton(playButtonRect(), kRingPanel,
        @"PLAY", playing, labels, style);
    s3g::clap_gui::drawToolboxHeaderButton(pauseButtonRect(), kRingPanel,
        @"PAUSE", !playing && !stopped, labels, style);
    s3g::clap_gui::drawToolboxHeaderButton(stopButtonRect(), kRingPanel,
        @"STOP", stopped, labels, style);
    s3g::clap_gui::drawToolboxHeaderButton(syncButtonRect(), kRingPanel,
        @"SYNC", false, labels, style);
    NSString* storage = [NSString stringWithFormat:@"STORE %s",
        s3g::sample_storage::storageModeName(_instance->storageMode)];
    s3g::clap_gui::drawToolboxHeaderButton(storageButtonRect(), kRingPanel,
        storage, _instance->storageMode != StorageMode::Link, labels, style);

    const GuiRingCatalog catalog = makeGuiRingCatalog(*_instance);
    const uint32_t selectedSlot = std::min(
        _instance->targets.selectedSlot.load(), 3u);
    const NSPoint center = ringCenter();
    const CGFloat pitch = ringPitch(catalog.count);
    [s3g::clap_gui::color(0x0e0e0e) setFill];
    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
        center.x - kOuterRingRadius - 10.0,
        center.y - kOuterRingRadius - 10.0,
        (kOuterRingRadius + 10.0) * 2.0,
        (kOuterRingRadius + 10.0) * 2.0)] fill];

    for (uint32_t ring = 0u; ring < catalog.count; ++ring) {
        const auto ref = catalog.rings[ring];
        const CGFloat radius = ringRadius(ring, catalog.count);
        NSBezierPath* guide = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(center.x - radius, center.y - radius,
                radius * 2.0, radius * 2.0)];
        [guide setLineWidth:0.45];
        [s3g::clap_gui::color(0x303030) setStroke];
        [guide stroke];
        const auto asset = _instance->sources[ref.slot];
        if (asset) drawCircularWaveform(*asset, ref.channel, radius, pitch,
            _instance->targets.slots[ref.slot].nudge.load(),
            [slotColor(ref.slot, ref.slot == selectedSlot)
                colorWithAlphaComponent:0.44]);
    }

    for (uint8_t slot = 0u; slot < kSourceCount; ++slot) {
        if (catalog.first[slot] < 0) continue;
        const CGFloat inner = ringRadius(
            static_cast<uint32_t>(catalog.first[slot]), catalog.count);
        const CGFloat outer = ringRadius(
            static_cast<uint32_t>(catalog.last[slot]), catalog.count);
        if (catalog.first[slot] > 0) {
            const CGFloat boundary = inner - pitch * 0.5;
            NSBezierPath* separator = [NSBezierPath bezierPathWithOvalInRect:
                NSMakeRect(center.x - boundary, center.y - boundary,
                    boundary * 2.0, boundary * 2.0)];
            [separator setLineWidth:3.0];
            [s3g::clap_gui::color(0x4b6961, 0.92) setStroke];
            [separator stroke];
            [separator setLineWidth:1.0];
            [s3g::clap_gui::color(0x7fa397, 0.98) setStroke];
            [separator stroke];
        }
        NSString* band = [NSString stringWithFormat:@"%c  %d–%d",
            'A' + slot, catalog.first[slot] + 1, catalog.last[slot] + 1];
        const CGFloat labelRadius = (inner + outer) * 0.5;
        [band drawAtPoint:NSMakePoint(center.x + labelRadius + 5.0,
            center.y - 7.0) withAttributes:values];
    }

    if (catalog.count == 0u) {
        [@"LOAD OR CAPTURE AUDIO TO ADD CHANNEL RINGS"
            drawAtPoint:NSMakePoint(center.x - 151.0, center.y - 7.0)
            withAttributes:values];
    } else {
        NSString* summary = [NSString stringWithFormat:@"%u RINGS  /  %@",
            catalog.count, valueText(*_instance, kFormationParamId)];
        [summary drawAtPoint:NSMakePoint(center.x - 88.0, center.y - 7.0)
            withAttributes:values];
    }

    const uint32_t formation = std::min(_instance->targets.formation.load(),
        3u);
    const uint8_t groupSize = guiFormationSize(formation);
    const uint32_t mask = _instance->targets.headMask.load();
    constexpr float kVisibleBlendFloor = 0.015f;
    if (catalog.count > 0u && groupSize > 1u) {
        for (uint8_t leader = 0u; leader < kHeadCount;
             leader = static_cast<uint8_t>(leader + groupSize)) {
            for (uint32_t side = 0u; side < 2u; ++side) {
                NSBezierPath* rake = [NSBezierPath bezierPath];
                bool started = false;
                float alpha = 1.0f;
                for (uint8_t member = 0u; member < groupSize; ++member) {
                    const uint8_t head = leader + member;
                    if ((mask & (1u << head)) == 0u) continue;
                    const float mix = std::clamp(
                        _instance->headRingMixes[head].load(), 0.0f, 1.0f);
                    if ((side == 0u && mix >= 1.0f - kVisibleBlendFloor)
                        || (side == 1u && mix <= kVisibleBlendFloor))
                        continue;
                    const uint32_t ring = std::min(catalog.count - 1u,
                        side == 0u ? _instance->headRings[head].load()
                            : _instance->headRingTargets[head].load());
                    const float phase = side == 0u
                        ? _instance->headPhases[head].load()
                        : _instance->headPhaseTargets[head].load();
                    const NSPoint point = polarPoint(ringRadius(ring,
                        catalog.count), phase);
                    started ? [rake lineToPoint:point]
                        : [rake moveToPoint:point];
                    started = true;
                    alpha = side == 0u ? 1.0f - mix : mix;
                }
                if (started) {
                    [rake setLineWidth:1.4];
                    [[headColor(leader, leader == _selectedHead)
                        colorWithAlphaComponent:std::max(0.16f, alpha * 0.7f)]
                        setStroke];
                    [rake stroke];
                }
            }
        }
    }

    for (uint32_t head = 0u; head < kHeadCount && catalog.count > 0u;
         ++head) {
        if ((mask & (1u << head)) == 0u) continue;
        const float mix = std::clamp(_instance->headRingMixes[head].load(),
            0.0f, 1.0f);
        const uint32_t ringA = std::min(catalog.count - 1u,
            _instance->headRings[head].load());
        const uint32_t ringB = std::min(catalog.count - 1u,
            _instance->headRingTargets[head].load());
        const NSPoint pointA = polarPoint(ringRadius(ringA, catalog.count),
            _instance->headPhases[head].load());
        const NSPoint pointB = polarPoint(ringRadius(ringB, catalog.count),
            _instance->headPhaseTargets[head].load());
        const bool separateReads = ringA != ringB
            || std::abs(pointA.x - pointB.x) > 1.0
            || std::abs(pointA.y - pointB.y) > 1.0;
        const bool audibleCrossfade = separateReads
            && mix > kVisibleBlendFloor
            && mix < 1.0f - kVisibleBlendFloor;
        if (audibleCrossfade) {
            NSBezierPath* bridge = [NSBezierPath bezierPath];
            [bridge moveToPoint:pointA]; [bridge lineToPoint:pointB];
            [bridge setLineWidth:0.8];
            [[headColor(head, head == _selectedHead)
                colorWithAlphaComponent:0.38] setStroke];
            [bridge stroke];
        }
        const auto drawDot = [&](NSPoint point, CGFloat alpha,
            bool showLabel) {
            [s3g::clap_gui::color(0x050505) setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(point.x - 6.0,
                point.y - 6.0, 12.0, 12.0)] fill];
            [[headColor(head, head == _selectedHead)
                colorWithAlphaComponent:std::max<CGFloat>(0.18, alpha)]
                setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(point.x - 3.5,
                point.y - 3.5, 7.0, 7.0)] fill];
            if (showLabel)
                [[NSString stringWithFormat:@"H%u", head + 1u]
                    drawAtPoint:NSMakePoint(point.x + 7.0, point.y - 8.0)
                    withAttributes:values];
        };
        if (mix >= 1.0f - kVisibleBlendFloor)
            drawDot(pointB, 1.0f, true);
        else {
            drawDot(pointA, audibleCrossfade ? 1.0f - mix : 1.0f, true);
        }
        if (audibleCrossfade)
            drawDot(pointB, mix, false);
    }

    for (uint32_t slot = 0u; slot < kSourceCount; ++slot) {
        const NSRect badge = slotBadgeRect(slot);
        [s3g::clap_gui::color(slot == selectedSlot ? 0x2b2b2b : 0x171717)
            setFill]; NSRectFill(badge);
        [(slot == selectedSlot ? style.accent : style.grid) setStroke];
        NSFrameRect(badge);
        const auto asset = _instance->sources[slot];
        NSString* text = asset ? [NSString stringWithFormat:@"%c  %uCH  %.2fs",
            'A' + slot, std::min<uint8_t>(asset->channelCount, 8u),
            static_cast<double>(asset->frameCount()) / asset->sampleRate]
            : [NSString stringWithFormat:@"%c  EMPTY", 'A' + slot];
        [text drawAtPoint:NSMakePoint(NSMinX(badge) + 8.0,
            NSMinY(badge) + 4.0) withAttributes:values];
    }
}

- (void)drawOutput:(NSDictionary*)labels values:(NSDictionary*)values
    style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawPanelFrame(NSMinX(kOutputBar), NSMinY(kOutputBar),
        NSWidth(kOutputBar), NSHeight(kOutputBar), style);
    [@"OUTPUT" drawAtPoint:NSMakePoint(NSMinX(kOutputBar) + 10.0,
        NSMinY(kOutputBar) + 25.0) withAttributes:labels];
    drawControl(*_instance, "GAIN", kOutputGainParamId, outputGainCell(), false,
        labels, values, style);
    drawControl(*_instance, "FORMAT", kOutputFormatParamId,
        outputFormatCell(), true,
        labels, values, style);
    drawControl(*_instance, "ROTATE", kOutputRotationParamId,
        outputRotateCell(), false, labels, values, style,
        controlPresentation(*_instance, kOutputRotationParamId));
    drawControl(*_instance, "MIDI", kMidiModeParamId, outputMidiCell(), true,
        labels, values, style);
    drawControl(*_instance, "ROOT", kMidiRootParamId, outputRootCell(), false,
        labels, values, style,
        controlPresentation(*_instance, kMidiRootParamId));
}

- (void)drawRightPanels:(NSDictionary*)labels values:(NSDictionary*)values
    style:(const s3g::clap_gui::Style&)style
{
    const uint32_t selectedSlot = std::min(
        _instance->targets.selectedSlot.load(), 3u);
    NSString* headTitle = [NSString stringWithFormat:
        @"3  HEAD H%u / EFFECTIVE READ", _selectedHead + 1u];
    const struct Panel { NSRect rect; NSString* title; } panels[] {
        { kCapturePanel, @"1  INPUT / CAPTURE" },
        { kMotionPanel, @"2  FIELD PATH  /  HEAD CLOCKS" },
        { kHeadPanel, headTitle },
        { kSlotPanel, [NSString stringWithFormat:@"4  SLOT %c / LOOP CLOCK",
            'A' + selectedSlot] },
    };
    for (const auto& panel : panels) {
        s3g::clap_gui::drawPanelFrame(NSMinX(panel.rect), NSMinY(panel.rect),
            NSWidth(panel.rect), NSHeight(panel.rect), style);
        s3g::clap_gui::drawPanelHeader(panel.title, true,
            NSMinX(panel.rect), NSMinY(panel.rect), NSWidth(panel.rect),
            kPanelHeaderHeight, labels, style);
    }
    NSBezierPath* motionDivider = [NSBezierPath bezierPath];
    [motionDivider moveToPoint:NSMakePoint(NSMidX(kMotionPanel),
        NSMinY(kMotionPanel) + kPanelHeaderHeight + 5.0)];
    [motionDivider lineToPoint:NSMakePoint(NSMidX(kMotionPanel),
        NSMaxY(kMotionPanel) - 8.0)];
    [motionDivider setLineWidth:0.6];
    [s3g::clap_gui::color(0x343434) setStroke];
    [motionDivider stroke];
    const bool recording = _instance->targets.captureGate.load() != 0u;
    s3g::clap_gui::drawToolboxHeaderButton(captureButtonRect(), kCapturePanel,
        recording ? @"STOP" : @"RECORD", recording, labels, style);
    const bool radialReverse = _instance->targets.radialReverse.load() != 0u;
    const bool radialMoves = controlPresentation(*_instance,
        kRadialReverseParamId).active;
    if (radialMoves)
        s3g::clap_gui::drawToolboxHeaderButton(radialReverseButtonRect(),
            kMotionPanel, @"RAD REV", radialReverse, labels, style);
    else
        s3g::clap_gui::drawHeaderButton(radialReverseButtonRect(),
            kMotionPanel, @"RAD REV —", false, labels, style);
    const bool angularReverse = _instance->targets.angularReverse.load() != 0u;
    s3g::clap_gui::drawToolboxHeaderButton(angularReverseButtonRect(),
        kMotionPanel, @"ANG REV", angularReverse, labels, style);
    const uint32_t mask = _instance->targets.headMask.load();
    const uint32_t selectedBit = 1u << _selectedHead;
    const bool muted = (mask & selectedBit) == 0u;
    const bool soloed = mask == selectedBit;
    s3g::clap_gui::drawToolboxHeaderButton(headMuteButtonRect(), kHeadPanel,
        muted ? @"UNMUTE" : @"MUTE", muted, labels, style);
    s3g::clap_gui::drawToolboxHeaderButton(headSoloButtonRect(), kHeadPanel,
        soloed ? @"UNSOLO" : @"SOLO", soloed, labels, style);
    const bool reverse = _instance->targets.slots[selectedSlot].reverse.load()
        != 0u;
    s3g::clap_gui::drawToolboxHeaderButton(slotLoadRect(), kSlotPanel,
        @"LOAD", true, labels, style);
    s3g::clap_gui::drawToolboxHeaderButton(slotClearRect(), kSlotPanel,
        @"CLEAR", _instance->sources[selectedSlot] != nullptr, labels, style);
    const bool slotLoaded = _instance->sources[selectedSlot] != nullptr;
    if (slotLoaded)
        s3g::clap_gui::drawToolboxHeaderButton(reverseButtonRect(), kSlotPanel,
            @"REVERSE", reverse, labels, style);
    else
        s3g::clap_gui::drawHeaderButton(reverseButtonRect(), kSlotPanel,
            @"REVERSE —", false, labels, style);

    for (const auto& row : kGlobalRows)
        drawControl(*_instance, row.label, row.id,
            controlCell(row.panel, row.column, row.row), row.menu, labels,
            values, style, controlPresentation(*_instance, row.id));

    for (uint32_t head = 0u; head < kHeadCount; ++head) {
        const bool enabled = (mask & (1u << head)) != 0u;
        s3g::clap_gui::drawHeaderButton(headButtonRect(head), kHeadPanel,
            [NSString stringWithFormat:@"H%u", head + 1u],
            enabled && head == _selectedHead, values, style);
        if (!enabled) {
            [s3g::clap_gui::color(0x101010) setFill];
            NSRectFill(NSInsetRect(headButtonRect(head), 2.0, 2.0));
            [style.grid setStroke]; NSFrameRect(headButtonRect(head));
            [[NSString stringWithFormat:@"H%u", head + 1u]
                drawAtPoint:NSMakePoint(NSMinX(headButtonRect(head)) + 16.0,
                    NSMinY(headButtonRect(head)) + 4.0)
                withAttributes:values];
        }
    }
    const uint8_t leader = guiFormationLeader(_selectedHead,
        _instance->targets.formation.load());
    const bool radialManual = _instance->targets.ringPath.load()
        == static_cast<uint32_t>(SampleRingsRingPath::Manual);
    const bool angularManual = _instance->targets.relationship.load()
        == static_cast<uint32_t>(SampleRingsRelationship::Manual);
    const float headMix = std::clamp(
        _instance->headRingMixes[_selectedHead].load(), 0.0f, 1.0f);
    const uint32_t headRingA = _instance->headRings[_selectedHead].load();
    const uint32_t headRingB = _instance->headRingTargets[_selectedHead].load();
    const uint32_t headSlotA = std::min(
        _instance->headSlots[_selectedHead].load(), 3u);
    const uint32_t headSlotB = std::min(
        _instance->headSlotTargets[_selectedHead].load(), 3u);
    const uint32_t headChannelA = _instance->headChannels[_selectedHead].load();
    const uint32_t headChannelB
        = _instance->headChannelTargets[_selectedHead].load();
    const bool hasSource = anySource(*_instance);
    const bool splitRead = hasSource
        && (headRingA != headRingB || headSlotA != headSlotB
            || headChannelA != headChannelB)
        && headMix > 0.015f && headMix < 0.985f;
    NSString* ringRead = !hasSource ? @"NO SOURCE"
        : splitRead
            ? [NSString stringWithFormat:@"R%u > R%u", headRingA + 1u,
                headRingB + 1u]
            : [NSString stringWithFormat:@"R%u",
                (headMix >= 0.5f ? headRingB : headRingA) + 1u];
    NSString* sourceRead = !hasSource ? @"NO SOURCE"
        : splitRead
            ? [NSString stringWithFormat:@"%c%u > %c%u", 'A' + headSlotA,
                headChannelA + 1u, 'A' + headSlotB, headChannelB + 1u]
            : [NSString stringWithFormat:@"%c%u",
                'A' + (headMix >= 0.5f ? headSlotB : headSlotA),
                (headMix >= 0.5f ? headChannelB : headChannelA) + 1u];
    float effectivePhase = _instance->headPhases[_selectedHead].load();
    effectivePhase += (_instance->headPhaseTargets[_selectedHead].load()
        - effectivePhase) * headMix;
    effectivePhase -= std::floor(effectivePhase);
    float degrees = effectivePhase * 360.0f;
    if (degrees > 180.0f) degrees -= 360.0f;
    NSString* phaseRead = hasSource
        ? [NSString stringWithFormat:@"%+.1f deg", degrees] : @"—";
    NSString* rateRead = hasSource
        ? [NSString stringWithFormat:@"%+.3fx",
            _instance->headRates[_selectedHead].load()] : @"—";
    NSString* blendRead = !hasSource ? @"NO SOURCE" : splitRead
        ? [NSString stringWithFormat:@"%.0f / %.0f%%",
            (1.0f - headMix) * 100.0f, headMix * 100.0f]
        : @"HARD READ";
    const uint8_t groupSize = guiFormationSize(
        _instance->targets.formation.load());
    NSString* groupRead = groupSize == 1u
        ? [NSString stringWithFormat:@"FREE H%u", _selectedHead + 1u]
        : [NSString stringWithFormat:@"H%u–H%u", leader + 1u,
            leader + groupSize];

    if (radialManual)
        drawControl(*_instance, "MANUAL RING",
            kManualRingParamBase + leader, headControlCell(0u), false,
            labels, values, style,
            controlPresentation(*_instance, kManualRingParamBase + leader));
    else
        drawReadOnlyControl(@"RING READ", ringRead, headControlCell(0u),
            labels, values, style);
    if (angularManual)
        drawControl(*_instance, "MANUAL PHASE",
            kManualPhaseParamBase + _selectedHead, headControlCell(1u), false,
            labels, values, style, controlPresentation(*_instance,
                kManualPhaseParamBase + _selectedHead));
    else
        drawReadOnlyControl(@"PHASE", phaseRead, headControlCell(1u),
            labels, values, style);
    if (angularManual)
        drawControl(*_instance, "MANUAL RATE",
            kManualRateParamBase + _selectedHead, headControlCell(2u), false,
            labels, values, style, controlPresentation(*_instance,
                kManualRateParamBase + _selectedHead));
    else
        drawReadOnlyControl(@"RATE", rateRead, headControlCell(2u),
            labels, values, style);
    drawReadOnlyControl(@"SOURCE", sourceRead, headControlCell(3u), labels,
        values, style);
    drawReadOnlyControl(@"XFADE", blendRead, headControlCell(4u), labels,
        values, style);
    drawReadOnlyControl(@"GROUP", groupRead, headControlCell(5u), labels,
        values, style);

    for (uint32_t row = 0u; row < kSlotLabels.size(); ++row)
        drawControl(*_instance, kSlotLabels[row],
            selectedSlotParam(*_instance, row), slotControlCell(row), false,
            labels, values, style, controlPresentation(*_instance,
                selectedSlotParam(*_instance, row)));
    const auto asset = _instance->sources[selectedSlot];
    NSString* detail = asset ? [NSString stringWithFormat:@"%@  /  %uCH  /  %.2fs",
        [NSString stringWithUTF8String:
            _instance->sourceStatuses[selectedSlot].c_str()], asset->channelCount,
        static_cast<double>(asset->frameCount()) / asset->sampleRate]
        : [NSString stringWithUTF8String:
            _instance->sourceStatuses[selectedSlot].c_str()];
    [detail drawAtPoint:NSMakePoint(NSMinX(kSlotPanel) + 10.0,
        NSMaxY(kSlotPanel) - 20.0) withAttributes:values];
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    const auto style = s3g::clap_gui::softTextStyle();
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    [style.bg setFill]; NSRectFill(self.bounds);
    s3g::clap_gui::drawProcessorTitleBand(
        [NSString stringWithUTF8String:_instance->plugin.desc->name],
        [NSString stringWithUTF8String:_presetName],
        s3g::clap_gui::peakDbText(_instance->outputPeak.load()),
        s3g::clap_gui::encoderTitleBand(kGuiW, kGuiH),
        s3g::clap_gui::softTitleAttrs(), labels, values, style);
    [self drawRingField:labels values:values style:style];
    [self drawOutput:labels values:values style:style];
    [self drawRightPanels:labels values:values style:style];
    if (_presetMenuOpen) {
        const auto& presets = factoryPresetItems();
        s3g::clap_gui::drawDropdownMenu([self openMenuRect], 20.0,
            presets.items.data(), static_cast<uint32_t>(presets.items.size()),
            -1, _menuHover,
            values, style);
    } else if (_openMenuParam != CLAP_INVALID_ID) {
        const MenuItems items = menuItems(_openMenuParam);
        s3g::clap_gui::drawDropdownMenu([self openMenuRect], 20.0,
            items.items.data(), items.count,
            static_cast<int>(std::lround(paramValue(*_instance,
                _openMenuParam))), _menuHover, values, style);
    }
}

- (void)beginSlider:(clap_id)id cell:(NSRect)cell event:(NSEvent*)event
    point:(NSPoint)point
{
    double defaultValue = 0.0;
    if (s3g::clap_gui::sliderDoubleClickDefault(event, &_instance->plugin,
            id, &defaultValue)) {
        queueGuiParamGesture(*_instance, id, defaultValue);
        [self markPresetEdited];
        [self setNeedsDisplay:YES];
        return;
    }
    _dragParam = id;
    _dragPanel = cell;
    _dragTrack = NSZeroRect;
    queueGuiParamBegin(*_instance, id);
    [self setDraggedValue:point];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:event.locationInWindow
        fromView:nil];
    if (_presetMenuOpen || _openMenuParam != CLAP_INVALID_ID) {
        const uint32_t count = _presetMenuOpen
            ? static_cast<uint32_t>(factoryPresetItems().items.size())
            : menuItems(_openMenuParam).count;
        const int hit = s3g::clap_gui::dropdownHitIndex(point,
            [self openMenuRect], 20.0, count);
        const clap_id id = _openMenuParam;
        const bool factoryPreset = _presetMenuOpen;
        _presetMenuOpen = NO;
        _openMenuParam = CLAP_INVALID_ID;
        _menuHover = -1;
        if (hit >= 0 && factoryPreset) {
            [self applyFactoryPreset:static_cast<uint32_t>(hit)];
        } else if (hit >= 0) {
            queueGuiParamGesture(*_instance, id, hit);
            if (id == kCaptureTargetParamId)
                queueGuiParamGesture(*_instance, kSelectedSlotParamId, hit);
            if (id != kCaptureTargetParamId
                && id != kSelectedSlotParamId)
                [self markPresetEdited];
            markDirty(*_instance);
        }
        [self setNeedsDisplay:YES];
        return;
    }
    const auto titleBand = s3g::clap_gui::encoderTitleBand(kGuiW, kGuiH);
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.presetMenu))) {
        [self openPresetMenu];
        return;
    }
    if (s3g::clap_gui::handleProcessorTitleClick(point,
            &_instance->plugin, @"Sample Rings",
            titleBand, _presetName,
            sizeof(_presetName), kOutputGainParamId)) {
        [self setNeedsDisplay:YES]; return;
    }
    if (NSPointInRect(point, playButtonRect())) {
        _instance->transportStopped.store(false);
        queueGuiParamGesture(*_instance, kPlayingParamId, 1.0);
        [self setNeedsDisplay:YES]; return;
    }
    if (NSPointInRect(point, pauseButtonRect())) {
        _instance->transportStopped.store(false);
        queueGuiParamGesture(*_instance, kPlayingParamId, 0.0);
        [self setNeedsDisplay:YES]; return;
    }
    if (NSPointInRect(point, stopButtonRect())) {
        _instance->transportStopped.store(true);
        queueGuiParamGesture(*_instance, kPlayingParamId, 0.0);
        requestTransportCommand(*_instance, TransportCommand::Stop);
        [self setNeedsDisplay:YES]; return;
    }
    if (NSPointInRect(point, syncButtonRect())) {
        requestTransportCommand(*_instance, TransportCommand::Relaunch);
        [self setNeedsDisplay:YES]; return;
    }
    if (NSPointInRect(point, storageButtonRect())) {
        setStorageMode(*_instance, static_cast<StorageMode>(
            (static_cast<uint8_t>(_instance->storageMode) + 1u) % 3u));
        [self setNeedsDisplay:YES]; return;
    }
    if (NSPointInRect(point, radialReverseButtonRect())) {
        if (!controlPresentation(*_instance,
                kRadialReverseParamId).active) return;
        queueGuiParamGesture(*_instance, kRadialReverseParamId,
            _instance->targets.radialReverse.load() ? 0.0 : 1.0);
        [self markPresetEdited];
        markDirty(*_instance); [self setNeedsDisplay:YES]; return;
    }
    if (NSPointInRect(point, angularReverseButtonRect())) {
        queueGuiParamGesture(*_instance, kAngularReverseParamId,
            _instance->targets.angularReverse.load() ? 0.0 : 1.0);
        [self markPresetEdited];
        markDirty(*_instance); [self setNeedsDisplay:YES]; return;
    }
    for (uint32_t slot = 0u; slot < kSourceCount; ++slot) {
        if (!NSPointInRect(point, slotBadgeRect(slot))) continue;
        queueGuiParamGesture(*_instance, kSelectedSlotParamId, slot);
        [self setNeedsDisplay:YES]; return;
    }
    if (NSPointInRect(point, captureButtonRect())) {
        const bool recording = _instance->targets.captureGate.load() != 0u;
        if (!recording) prepareCaptureOnMainThread(*_instance);
        queueGuiParamGesture(*_instance, kCaptureGateParamId,
            recording ? 0.0 : 1.0);
        markDirty(*_instance); [self setNeedsDisplay:YES]; return;
    }
    const uint32_t selectedSlot = std::min(
        _instance->targets.selectedSlot.load(), 3u);
    if (NSPointInRect(point, slotLoadRect())) {
        [self openSource:selectedSlot]; return;
    }
    if (NSPointInRect(point, slotClearRect())) {
        if (!_instance->sources[selectedSlot]) return;
        clearSource(*_instance, selectedSlot);
        [self setNeedsDisplay:YES]; return;
    }
    if (NSPointInRect(point, reverseButtonRect())) {
        if (!_instance->sources[selectedSlot]) return;
        queueGuiParamGesture(*_instance,
            slotParamId(selectedSlot, kSlotReverse),
            _instance->targets.slots[selectedSlot].reverse.load()
                ? 0.0 : 1.0);
        [self markPresetEdited];
        markDirty(*_instance); [self setNeedsDisplay:YES]; return;
    }
    if (NSPointInRect(point, headMuteButtonRect())) {
        uint32_t mask = _instance->targets.headMask.load();
        mask ^= 1u << _selectedHead;
        _soloHead = -1;
        queueGuiParamGesture(*_instance, kHeadMaskParamId, mask);
        [self markPresetEdited];
        markDirty(*_instance); [self setNeedsDisplay:YES]; return;
    }
    if (NSPointInRect(point, headSoloButtonRect())) {
        const uint32_t selectedBit = 1u << _selectedHead;
        const uint32_t mask = _instance->targets.headMask.load();
        uint32_t next = selectedBit;
        if (mask == selectedBit) {
            next = _soloHead == static_cast<int32_t>(_selectedHead)
                ? _soloRestoreMask : 0xffu;
            _soloHead = -1;
        } else {
            if (_soloHead < 0
                || mask != (1u << static_cast<uint32_t>(_soloHead)))
                _soloRestoreMask = mask;
            _soloHead = static_cast<int32_t>(_selectedHead);
        }
        queueGuiParamGesture(*_instance, kHeadMaskParamId, next);
        [self markPresetEdited];
        markDirty(*_instance); [self setNeedsDisplay:YES]; return;
    }

    for (uint32_t head = 0u; head < kHeadCount; ++head) {
        if (!NSPointInRect(point, headButtonRect(head))) continue;
        if ((event.modifierFlags & NSEventModifierFlagOption) != 0u) {
            uint32_t mask = _instance->targets.headMask.load();
            mask ^= 1u << head;
            _soloHead = -1;
            queueGuiParamGesture(*_instance, kHeadMaskParamId, mask);
            [self markPresetEdited];
        } else _selectedHead = head;
        [self setNeedsDisplay:YES]; return;
    }

    for (const auto& row : kGlobalRows) {
        const NSRect cell = controlCell(row.panel, row.column, row.row);
        if (!NSPointInRect(point, cell)) continue;
        if (!controlPresentation(*_instance, row.id).active) return;
        if (row.menu) [self openMenu:row.id anchor:cell];
        else [self beginSlider:row.id cell:cell event:event point:point];
        return;
    }
    for (uint32_t row = 0u; row < kSlotLabels.size(); ++row) {
        const NSRect cell = slotControlCell(row);
        if (!NSPointInRect(point, cell)) continue;
        const clap_id id = selectedSlotParam(*_instance, row);
        if (!controlPresentation(*_instance, id).active) return;
        [self beginSlider:id cell:cell
            event:event point:point];
        return;
    }
    const uint8_t leader = guiFormationLeader(_selectedHead,
        _instance->targets.formation.load());
    for (uint32_t index = 0u; index < 3u; ++index) {
        const NSRect cell = headControlCell(index);
        if (!NSPointInRect(point, cell)) continue;
        const clap_id id = headControlParam(
            index == 0u ? leader : _selectedHead, index);
        if (!controlPresentation(*_instance, id).active) return;
        [self beginSlider:id cell:cell
            event:event point:point];
        return;
    }

    if (NSPointInRect(point, outputFormatCell())) {
        [self openMenu:kOutputFormatParamId anchor:outputFormatCell()]; return;
    }
    if (NSPointInRect(point, outputMidiCell())) {
        [self openMenu:kMidiModeParamId anchor:outputMidiCell()]; return;
    }
    if (NSPointInRect(point, outputGainCell())) {
        [self beginSlider:kOutputGainParamId cell:outputGainCell() event:event
            point:point]; return;
    }
    if (NSPointInRect(point, outputRotateCell())) {
        if (!controlPresentation(*_instance,
                kOutputRotationParamId).active) return;
        [self beginSlider:kOutputRotationParamId cell:outputRotateCell() event:event
            point:point]; return;
    }
    if (NSPointInRect(point, outputRootCell())) {
        if (!controlPresentation(*_instance, kMidiRootParamId).active) return;
        [self beginSlider:kMidiRootParamId cell:outputRootCell() event:event
            point:point]; return;
    }

    const GuiRingCatalog catalog = makeGuiRingCatalog(*_instance);
    const CGFloat radius = pointRadius(point);
    if (catalog.count > 0u && radius >= kInnerRingRadius - 15.0
        && radius <= kOuterRingRadius + 15.0) {
        CGFloat nearest = 15.0;
        for (uint32_t head = 0u; head < kHeadCount; ++head) {
            if ((_instance->targets.headMask.load() & (1u << head)) == 0u)
                continue;
            const uint32_t ring = std::min(catalog.count - 1u,
                _instance->headRings[head].load());
            const NSPoint headPoint = polarPoint(ringRadius(ring,
                catalog.count), _instance->headPhases[head].load());
            const CGFloat distance = std::hypot(point.x - headPoint.x,
                point.y - headPoint.y);
            if (distance < nearest) { nearest = distance; _selectedHead = head; }
        }
        const uint32_t formation = _instance->targets.formation.load();
        const uint8_t polarLeader = guiFormationLeader(_selectedHead,
            formation);
        queueGuiParamGesture(*_instance, kRingPathParamId,
            static_cast<double>(SampleRingsRingPath::Manual));
        queueGuiParamGesture(*_instance, kRelationshipParamId,
            static_cast<double>(SampleRingsRelationship::Manual));
        _polarRingParam = kManualRingParamBase + polarLeader;
        _polarPhaseParam = kManualPhaseParamBase + _selectedHead;
        queueGuiParamBegin(*_instance, _polarRingParam);
        queueGuiParamBegin(*_instance, _polarPhaseParam);
        _polarDrag = YES;
        [self setPolarValue:point];
        return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:event.locationInWindow
        fromView:nil];
    if (_polarDrag) { [self setPolarValue:point]; return; }
    if (_dragParam != CLAP_INVALID_ID) [self setDraggedValue:point];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (_polarDrag) {
        queueGuiParamEnd(*_instance, _polarRingParam);
        queueGuiParamEnd(*_instance, _polarPhaseParam);
    }
    _polarDrag = NO;
    _polarRingParam = CLAP_INVALID_ID;
    _polarPhaseParam = CLAP_INVALID_ID;
    if (_dragParam != CLAP_INVALID_ID)
        queueGuiParamEnd(*_instance, _dragParam);
    _dragParam = CLAP_INVALID_ID;
    _dragTrack = NSZeroRect;
}

- (void)mouseMoved:(NSEvent*)event
{
    if (!_presetMenuOpen && _openMenuParam == CLAP_INVALID_ID) return;
    const uint32_t count = _presetMenuOpen
        ? static_cast<uint32_t>(factoryPresetItems().items.size())
        : menuItems(_openMenuParam).count;
    const int next = s3g::clap_gui::dropdownHitIndex(
        [self convertPoint:event.locationInWindow fromView:nil],
        [self openMenuRect], 20.0, count);
    if (next != _menuHover) { _menuHover = next; [self setNeedsDisplay:YES]; }
}

- (void)updateTrackingAreas
{
    for (NSTrackingArea* area in self.trackingAreas)
        [self removeTrackingArea:area];
    [super updateTrackingAreas];
    NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:NSZeroRect
        options:NSTrackingMouseMoved | NSTrackingActiveInKeyWindow
            | NSTrackingInVisibleRect owner:self userInfo:nil];
    [self addTrackingArea:[area autorelease]];
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
    NSArray<NSURL*>* urls = [sender.draggingPasteboard
        readObjectsForClasses:@[ [NSURL class] ]
        options:@{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }];
    for (NSURL* url in urls) if (isSupportedAudioURL(url))
        return NSDragOperationCopy;
    return NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    NSArray<NSURL*>* urls = [sender.draggingPasteboard
        readObjectsForClasses:@[ [NSURL class] ]
        options:@{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }];
    uint32_t slot = std::min(_instance->targets.selectedSlot.load(), 3u);
    bool loaded = false;
    for (NSURL* url in urls) {
        if (!isSupportedAudioURL(url) || slot >= kSourceCount) continue;
        const char* path = url.path.fileSystemRepresentation;
        if (!path) continue;
        queueSourceLoad(*_instance, slot, path);
        loaded = true; ++slot;
    }
    [self setNeedsDisplay:YES];
    return loaded ? YES : NO;
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api,
    bool isFloating)
{ return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }

bool guiGetPreferredApi(const clap_plugin_t*, const char** api,
    bool* isFloating)
{
    if (!api || !isFloating) return false;
    *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) return false;
    auto& instance = *self(plugin);
    if (instance.guiView) return true;
    instance.guiView = [[S3GSampleRingsView alloc]
        initWithPlugin:&instance];
    if (!instance.guiView) return false;
    if (std::getenv("S3G_GUI_DOCUMENTATION_CAPTURE"))
        [static_cast<S3GSampleRingsView*>(instance.guiView)
            loadDocumentationSources];
    if (!s3g::clap_gui::createResponsiveViewport(instance.guiViewport,
            static_cast<NSView*>(instance.guiView), kGuiW, kGuiH,
            kGuiW, 360u)) {
        [static_cast<NSView*>(instance.guiView) release];
        instance.guiView = nullptr;
        return false;
    }
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    if (!instance.guiView) return;
    instance.guiVisible.store(false);
    [static_cast<S3GSampleRingsView*>(instance.guiView)
        stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(instance.guiViewport,
        instance.guiView);
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{ return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport,
    kGuiW, kGuiH, width, height, kGuiW, 360u); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{ return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width,
    uint32_t* height)
{ return s3g::clap_gui::adjustResponsiveViewportSize(
    self(plugin)->guiViewport, kGuiW, kGuiH, width, height,
    kGuiW, 360u); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{ return s3g::clap_gui::setResponsiveViewportSize(
    self(plugin)->guiViewport, width, height); }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) return false;
    auto& instance = *self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(instance.guiViewport,
        static_cast<NSView*>(window->cocoa), instance.host);
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*)
{ return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    if (!instance.guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            instance.guiViewport, false)) return false;
    instance.guiVisible.store(true);
    [static_cast<S3GSampleRingsView*>(instance.guiView)
        startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    if (!instance.guiView) return false;
    instance.guiVisible.store(false);
    [static_cast<S3GSampleRingsView*>(instance.guiView)
        stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(instance.guiViewport,
        true);
}

const clap_plugin_gui_t guiExtension {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy,
    guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize,
    guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow,
    guiHide,
};

} // namespace
