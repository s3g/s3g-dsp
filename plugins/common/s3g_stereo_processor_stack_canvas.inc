// Literal Cocoa adaptation: preserve layout, drawing order and hit maps.
namespace stereo_processor_canvas {
namespace drawing = s3g::portable_gui::stereo_drawing;
namespace foundation = s3g::portable_gui::foundation;

constexpr clap_id kFactoryPresetMenuId = 0x7ffffff0u;

struct StackUiRow {
    clap_id id;
    const char* label;
    double panelX;
    double panelWidth;
    double y;
    uint8_t page;
};

struct StackUiPanel {
    const char* name;
    uint8_t page;
    s3g::gui_layout::Panel panel;
};

namespace layout = s3g::gui_layout;

constexpr double kLeftPanelX = 16.0;
constexpr double kRightPanelX = 506.0;
constexpr double kPanelWidth = 458.0;
constexpr double kMixerColumnGap = 8.0;
constexpr double kMixerColumnWidth =
    (kPanelWidth - kMixerColumnGap) * 0.5;
constexpr double kMixerBPanelX =
    kLeftPanelX + kMixerColumnWidth + kMixerColumnGap;
constexpr double kScoreRelationBPanelX =
    kRightPanelX + kMixerColumnWidth + kMixerColumnGap;
constexpr uint8_t kAllPages = 0xffu;
constexpr double kContentTop =
    s3g::gui_layout::kStandardMetrics.contentTop;
constexpr double kPageLeftTop = kContentTop
    + s3g::gui_layout::toolboxHeightForRows(1u)
    + s3g::gui_layout::kStandardMetrics.panelGap;
constexpr double kPatternPanelHeight = 132.0;
constexpr layout::Canvas kStackCanvas {
    static_cast<double>(kGuiWidth), static_cast<double>(kGuiHeight)
};
constexpr layout::Column kOutputColumn {
    kLeftPanelX, kPanelWidth, kContentTop
};
constexpr layout::Column kLeftColumn {
    kLeftPanelX, kPanelWidth, kPageLeftTop
};
constexpr layout::Column kRightColumn {
    kRightPanelX, kPanelWidth, kContentTop
};

constexpr auto kOutputPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Output,
    kOutputColumn, kContentTop, 1u);
constexpr auto kPlayPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Engine,
    kLeftColumn, kPageLeftTop, 12u);
constexpr auto kPairPanel = layout::fittedStackPanel(
    layout::PanelRole::Engine, kPlayPanel, 7u);
constexpr auto kRoutingPanel = layout::fittedStackPanel(
    layout::PanelRole::Utility, kPairPanel, 2u);
constexpr auto kArpAPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::EventTiming,
    kRightColumn, kContentTop, 6u);
constexpr auto kPatternAPanel = layout::stackPanel(
    layout::PanelRole::EventTiming, kArpAPanel,
    kPatternPanelHeight, 0u);
constexpr auto kArpBPanel = layout::fittedStackPanel(
    layout::PanelRole::EventTiming, kPatternAPanel, 8u);
constexpr auto kPatternBPanel = layout::stackPanel(
    layout::PanelRole::EventTiming, kArpBPanel,
    kPatternPanelHeight, 0u);

constexpr auto kMaterialAPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Engine,
    kLeftColumn, kPageLeftTop, 2u);
constexpr auto kPedalAPanel = layout::fittedStackPanel(
    layout::PanelRole::Projection, kMaterialAPanel, 4u);
constexpr auto kAmplifierAPanel = layout::fittedStackPanel(
    layout::PanelRole::Topology, kPedalAPanel, 7u);
constexpr auto kLoopAPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Output,
    kRightColumn, kContentTop, 12u);

constexpr auto kLinksPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Utility,
    kLeftColumn, kPageLeftTop, 3u);
constexpr auto kMaterialBPanel = layout::fittedStackPanel(
    layout::PanelRole::Engine, kLinksPanel, 2u);
constexpr auto kPedalBPanel = layout::fittedStackPanel(
    layout::PanelRole::Projection, kMaterialBPanel, 4u);
constexpr auto kAmplifierBPanel = layout::fittedStackPanel(
    layout::PanelRole::Topology, kPedalBPanel, 7u);
constexpr auto kLoopBPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Motion,
    kRightColumn, kContentTop, 12u);
constexpr auto kScoreTimingPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::EventTiming,
    kLeftColumn, kPageLeftTop, 4u);
constexpr auto kScoreRelationPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Utility,
    kRightColumn, kContentTop, 4u);

constexpr std::array kOutputPanels { kOutputPanel };
constexpr std::array kPlayLeftPanels {
    kPlayPanel, kPairPanel, kRoutingPanel,
};
constexpr std::array kPlayRightPanels {
    kArpAPanel, kPatternAPanel, kArpBPanel, kPatternBPanel,
};
constexpr std::array kRigALeftPanels {
    kMaterialAPanel, kPedalAPanel, kAmplifierAPanel,
};
constexpr std::array kRigARightPanels { kLoopAPanel };
constexpr std::array kRigBLeftPanels {
    kLinksPanel, kMaterialBPanel, kPedalBPanel, kAmplifierBPanel,
};
constexpr std::array kRigBRightPanels { kLoopBPanel };
constexpr std::array kScoreLeftPanels { kScoreTimingPanel };
constexpr std::array kScoreRightPanels { kScoreRelationPanel };

static_assert(layout::validateColumn(kOutputPanels, kStackCanvas));
static_assert(layout::validateColumn(kPlayLeftPanels, kStackCanvas, false));
static_assert(layout::validateColumn(kPlayRightPanels, kStackCanvas, false));
static_assert(layout::validateColumn(kRigALeftPanels, kStackCanvas, false));
static_assert(layout::validateColumn(kRigARightPanels, kStackCanvas, false));
static_assert(layout::validateColumn(kRigBLeftPanels, kStackCanvas, false));
static_assert(layout::validateColumn(kRigBRightPanels, kStackCanvas, false));
static_assert(layout::validateColumn(kScoreLeftPanels, kStackCanvas, false));
static_assert(layout::validateColumn(kScoreRightPanels, kStackCanvas, false));

constexpr std::array<StackUiPanel, 19u> kUiPanels {{
    { "OUTPUT", kAllPages, kOutputPanel },
    { "PLAY / ENVELOPE", 0u, kPlayPanel },
    { "PAIR / STEREO / OUTPUT MIXER", 0u, kPairPanel },
    { "ROUTING", 0u, kRoutingPanel },
    { "ARPEGGIATOR A", 0u, kArpAPanel },
    { "PATTERN A · RIGHT-CLICK REST", 0u, kPatternAPanel },
    { "ARPEGGIATOR B", 0u, kArpBPanel },
    { "PATTERN B · RIGHT-CLICK REST", 0u, kPatternBPanel },
    { "MATERIAL A", 1u, kMaterialAPanel },
    { "PEDAL A", 1u, kPedalAPanel },
    { "AMPLIFIER / SPEAKER A", 1u, kAmplifierAPanel },
    { "MIC FEEDBACK LOOP A", 1u, kLoopAPanel },
    { "LINKS", 2u, kLinksPanel },
    { "MATERIAL B", 2u, kMaterialBPanel },
    { "PEDAL B", 2u, kPedalBPanel },
    { "AMPLIFIER / SPEAKER B", 2u, kAmplifierBPanel },
    { "MIC FEEDBACK LOOP B", 2u, kLoopBPanel },
    { "SCORE CLOCK / RANDOMIZE", 3u, kScoreTimingPanel },
    { "SCORE SCALES / A-B RELATIONSHIP", 3u, kScoreRelationPanel },
}};

constexpr std::array<StackUiRow, 102u> kUiRows {{
    { kOutputParamId, "OUT", kLeftPanelX, kPanelWidth,
        layout::rowY(kOutputPanel, 0u), kAllPages },
    { kModeParamId, "MODE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 0u), 0u },
    { kShapeParamId, "SHAPE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 1u), 0u },
    { kWireParamId, "STRING", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 2u), 0u },
    { kPickParamId, "PICK", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 3u), 0u },
    { kDampingParamId, "DAMP", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 4u), 0u },
    { kGlideParamId, "GLIDE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 5u), 0u },
    { kCrookedParamId, "CROOKED", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 6u), 0u },
    { kSpillParamId, "SPILL", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 7u), 0u },
    { kAttackParamId, "ATTACK", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 8u), 0u },
    { kDecayParamId, "DECAY", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 9u), 0u },
    { kSustainParamId, "SUSTAIN", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 10u), 0u },
    { kReleaseParamId, "RELEASE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 11u), 0u },

    { kPairAmountParamId, "DUAL", kLeftPanelX, kPanelWidth,
        layout::rowY(kPairPanel, 0u), 0u },
    { kPairRelationParamId, "RELATION", kLeftPanelX, kPanelWidth,
        layout::rowY(kPairPanel, 1u), 0u },
    { kPairLooseParamId, "LOOSE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPairPanel, 2u), 0u },
    { kPairSpreadParamId, "SPREAD", kLeftPanelX, kPanelWidth,
        layout::rowY(kPairPanel, 3u), 0u },
    { kRigLevelAParamId, "A LEVEL", kLeftPanelX, kMixerColumnWidth,
        layout::rowY(kPairPanel, 4u), 0u },
    { kRigLevelBParamId, "B LEVEL", kMixerBPanelX, kMixerColumnWidth,
        layout::rowY(kPairPanel, 4u), 0u },
    { kRigPanAParamId, "A PAN", kLeftPanelX, kMixerColumnWidth,
        layout::rowY(kPairPanel, 5u), 0u },
    { kRigPanBParamId, "B PAN", kMixerBPanelX, kMixerColumnWidth,
        layout::rowY(kPairPanel, 5u), 0u },
    { kRigMuteAParamId, "A MUTE", kLeftPanelX, kMixerColumnWidth,
        layout::rowY(kPairPanel, 6u), 0u },
    { kRigMuteBParamId, "B MUTE", kMixerBPanelX, kMixerColumnWidth,
        layout::rowY(kPairPanel, 6u), 0u },
    { kMidiReceiveParamId, "MIDI RECEIVE", kLeftPanelX, kPanelWidth,
        layout::rowY(kRoutingPanel, 0u), 0u },
    { kArpHostSyncParamId, "ARP SYNC", kLeftPanelX, kPanelWidth,
        layout::rowY(kRoutingPanel, 1u), 0u },

    { kArpPatternParamId, "PATTERN", kRightPanelX, kPanelWidth,
        layout::rowY(kArpAPanel, 0u), 0u },
    { kScaleParamId, "SCALE RULE", kRightPanelX, kPanelWidth,
        layout::rowY(kArpAPanel, 1u), 0u },
    { kArpRateParamId, "RATE", kRightPanelX, kPanelWidth,
        layout::rowY(kArpAPanel, 2u), 0u },
    { kArpOctavesParamId, "OCTAVES", kRightPanelX, kPanelWidth,
        layout::rowY(kArpAPanel, 3u), 0u },
    { kArpGateParamId, "GATE", kRightPanelX, kPanelWidth,
        layout::rowY(kArpAPanel, 4u), 0u },
    { kCustomLengthParamId, "LENGTH", kRightPanelX, kPanelWidth,
        layout::rowY(kArpAPanel, 5u), 0u },
    { kArpBRelationParamId, "RELATION", kRightPanelX, kPanelWidth,
        layout::rowY(kArpBPanel, 0u), 0u },
    { kArpPatternBParamId, "PATTERN", kRightPanelX, kPanelWidth,
        layout::rowY(kArpBPanel, 1u), 0u },
    { kScaleBParamId, "SCALE RULE", kRightPanelX, kPanelWidth,
        layout::rowY(kArpBPanel, 2u), 0u },
    { kArpRateBParamId, "RATE", kRightPanelX, kPanelWidth,
        layout::rowY(kArpBPanel, 3u), 0u },
    { kArpOctavesBParamId, "OCTAVES", kRightPanelX, kPanelWidth,
        layout::rowY(kArpBPanel, 4u), 0u },
    { kArpGateBParamId, "GATE", kRightPanelX, kPanelWidth,
        layout::rowY(kArpBPanel, 5u), 0u },
    { kArpPhaseBParamId, "PHASE", kRightPanelX, kPanelWidth,
        layout::rowY(kArpBPanel, 6u), 0u },
    { kCustomLengthBParamId, "LENGTH", kRightPanelX, kPanelWidth,
        layout::rowY(kArpBPanel, 7u), 0u },
    { kNeckAParamId, "NECK", kLeftPanelX, kPanelWidth,
        layout::rowY(kMaterialAPanel, 0u), 1u },
    { kBodyAParamId, "BODY", kLeftPanelX, kPanelWidth,
        layout::rowY(kMaterialAPanel, 1u), 1u },
    { kCircuitParamId, "CIRCUIT", kLeftPanelX, kPanelWidth,
        layout::rowY(kPedalAPanel, 0u), 1u },
    { kBiteParamId, "BITE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPedalAPanel, 1u), 1u },
    { kPedalToneParamId, "TONE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPedalAPanel, 2u), 1u },
    { kBiasParamId, "BIAS", kLeftPanelX, kPanelWidth,
        layout::rowY(kPedalAPanel, 3u), 1u },
    { kStackParamId, "STACK", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierAPanel, 0u), 1u },
    { kSagParamId, "SAG", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierAPanel, 1u), 1u },
    { kFocusParamId, "FOCUS", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierAPanel, 2u), 1u },
    { kSpeakerAParamId, "SPEAKER", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierAPanel, 3u), 1u },
    { kConeParamId, "CONE", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierAPanel, 4u), 1u },
    { kCabinetParamId, "CAB", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierAPanel, 5u), 1u },
    { kMicParamId, "MIC", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierAPanel, 6u), 1u },
    { kFeedbackParamId, "FEEDBACK", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 0u), 1u },
    { kProximityParamId, "PROXIMITY", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 1u), 1u },
    { kHarmonicParamId, "HARMONIC", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 2u), 1u },
    { kTrackingParamId, "TRACK", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 3u), 1u },
    { kPolarityParamId, "POLARITY", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 4u), 1u },
    { kRootParamId, "ROOT", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 5u), 1u },
    { kChaosParamId, "CHAOS", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 6u), 1u },
    { kPierceParamId, "PIERCE", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 7u), 1u },
    { kSelfListenParamId, "SELF LISTEN", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 8u), 1u },
    { kTargetGlitchParamId, "TARGET GLITCH", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 9u), 1u },
    { kGlitchRatchetParamId, "RATCHET", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 10u), 1u },
    { kOverloadMaskParamId, "OVERLOAD MASK", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopAPanel, 11u), 1u },

    { kLinkPedalParamId, "PEDALS", kLeftPanelX, kPanelWidth,
        layout::rowY(kLinksPanel, 0u), 2u },
    { kLinkAmplifierParamId, "AMPLIFIERS", kLeftPanelX, kPanelWidth,
        layout::rowY(kLinksPanel, 1u), 2u },
    { kLinkFeedbackParamId, "FEEDBACK", kLeftPanelX, kPanelWidth,
        layout::rowY(kLinksPanel, 2u), 2u },
    { kNeckBParamId, "NECK", kLeftPanelX, kPanelWidth,
        layout::rowY(kMaterialBPanel, 0u), 2u },
    { kBodyBParamId, "BODY", kLeftPanelX, kPanelWidth,
        layout::rowY(kMaterialBPanel, 1u), 2u },
    { kCircuitBParamId, "CIRCUIT", kLeftPanelX, kPanelWidth,
        layout::rowY(kPedalBPanel, 0u), 2u },
    { kBiteBParamId, "BITE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPedalBPanel, 1u), 2u },
    { kPedalToneBParamId, "TONE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPedalBPanel, 2u), 2u },
    { kBiasBParamId, "BIAS", kLeftPanelX, kPanelWidth,
        layout::rowY(kPedalBPanel, 3u), 2u },
    { kStackBParamId, "STACK", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierBPanel, 0u), 2u },
    { kSagBParamId, "SAG", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierBPanel, 1u), 2u },
    { kFocusBParamId, "FOCUS", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierBPanel, 2u), 2u },
    { kSpeakerBParamId, "SPEAKER", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierBPanel, 3u), 2u },
    { kConeBParamId, "CONE", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierBPanel, 4u), 2u },
    { kCabinetBParamId, "CAB", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierBPanel, 5u), 2u },
    { kMicBParamId, "MIC", kLeftPanelX, kPanelWidth,
        layout::rowY(kAmplifierBPanel, 6u), 2u },
    { kFeedbackBParamId, "FEEDBACK", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 0u), 2u },
    { kProximityBParamId, "PROXIMITY", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 1u), 2u },
    { kHarmonicBParamId, "HARMONIC", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 2u), 2u },
    { kTrackingBParamId, "TRACK", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 3u), 2u },
    { kPolarityBParamId, "POLARITY", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 4u), 2u },
    { kRootBParamId, "ROOT", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 5u), 2u },
    { kChaosBParamId, "CHAOS", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 6u), 2u },
    { kPierceBParamId, "PIERCE", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 7u), 2u },
    { kSelfListenBParamId, "SELF LISTEN", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 8u), 2u },
    { kTargetGlitchBParamId, "TARGET GLITCH", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 9u), 2u },
    { kGlitchRatchetBParamId, "RATCHET", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 10u), 2u },
    { kOverloadMaskBParamId, "OVERLOAD MASK", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopBPanel, 11u), 2u },
    { kScoreEnableParamId, "PLAYBACK", kLeftPanelX, kPanelWidth,
        layout::rowY(kScoreTimingPanel, 0u), 3u },
    { kScoreRateParamId, "ROW RATE", kLeftPanelX, kPanelWidth,
        layout::rowY(kScoreTimingPanel, 1u), 3u },
    { kScoreGateParamId, "ROW GATE", kLeftPanelX, kPanelWidth,
        layout::rowY(kScoreTimingPanel, 2u), 3u },
    { kScoreLengthParamId, "ARRANGE LENGTH", kLeftPanelX, kPanelWidth,
        layout::rowY(kScoreTimingPanel, 3u), 3u },
    { kScaleParamId, "SCALE A", kRightPanelX, kPanelWidth,
        layout::rowY(kScoreRelationPanel, 0u), 3u },
    { kScaleBParamId, "SCALE B", kRightPanelX, kPanelWidth,
        layout::rowY(kScoreRelationPanel, 1u), 3u },
    { kScoreBSourceParamId, "B SOURCE", kRightPanelX, kMixerColumnWidth,
        layout::rowY(kScoreRelationPanel, 2u), 3u },
    { kPairRelationParamId, "RELATION", kScoreRelationBPanelX,
        kMixerColumnWidth, layout::rowY(kScoreRelationPanel, 2u), 3u },
    { kPairAmountParamId, "DUAL", kRightPanelX, kMixerColumnWidth,
        layout::rowY(kScoreRelationPanel, 3u), 3u },
    { kPairSpreadParamId, "SPREAD", kScoreRelationBPanelX,
        kMixerColumnWidth, layout::rowY(kScoreRelationPanel, 3u), 3u },
}};

bool isUiMenuParam(clap_id id)
{
    return id == kModeParamId || id == kCircuitParamId
        || id == kCircuitBParamId
        || id == kSpeakerAParamId || id == kSpeakerBParamId
        || id == kMidiReceiveParamId || id == kArpHostSyncParamId
        || id == kScoreEnableParamId || id == kScoreRateParamId
        || id == kScoreLengthParamId || id == kScoreBSourceParamId
        || id == kArpPatternParamId
        || id == kArpPatternBParamId || id == kArpBRelationParamId
        || id == kScaleParamId || id == kScaleBParamId
        || id == kArpRateParamId || id == kArpRateBParamId
        || id == kArpOctavesParamId || id == kCustomLengthParamId
        || id == kArpOctavesBParamId || id == kCustomLengthBParamId
        || id == kPairRelationParamId
        || id == kNeckAParamId || id == kNeckBParamId
        || id == kBodyAParamId || id == kBodyBParamId
        || id == kLinkPedalParamId || id == kLinkAmplifierParamId
        || id == kLinkFeedbackParamId;
}

bool isUiToggleParam(clap_id id)
{
    return id == kRigMuteAParamId || id == kRigMuteBParamId;
}

uint32_t uiMenuItemCount(clap_id id)
{
    if (id == kModeParamId) return s3g::kProcessorStackModeCount;
    if (id == kCircuitParamId || id == kCircuitBParamId)
        return s3g::kProcessorStackCircuitCount;
    if (id == kSpeakerAParamId || id == kSpeakerBParamId)
        return s3g::kProcessorStackSpeakerProfileCount;
    if (id == kMidiReceiveParamId) return 17u;
    if (id == kArpHostSyncParamId) return 2u;
    if (id == kScoreEnableParamId || id == kScoreBSourceParamId) return 2u;
    if (id == kScoreRateParamId) return s3g::kProcessorStackArpRateCount;
    if (id == kScoreLengthParamId)
        return s3g::kProcessorStackScoreArrangementSlots;
    if (id == kArpBRelationParamId)
        return s3g::kProcessorStackArpRelationCount;
    if (id == kArpPatternParamId || id == kArpPatternBParamId)
        return s3g::kProcessorStackArpPatternCount;
    if (id == kScaleParamId || id == kScaleBParamId)
        return s3g::kProcessorStackScaleCount;
    if (id == kArpRateParamId || id == kArpRateBParamId)
        return s3g::kProcessorStackArpRateCount;
    if (id == kArpOctavesParamId || id == kArpOctavesBParamId) return 4u;
    if (id == kCustomLengthParamId || id == kCustomLengthBParamId) return 8u;
    if (id == kPairRelationParamId)
        return s3g::kProcessorStackPairRelationCount;
    if (id == kNeckAParamId || id == kNeckBParamId)
        return s3g::kProcessorStackNeckMaterialCount;
    if (id == kBodyAParamId || id == kBodyBParamId)
        return s3g::kProcessorStackBodyMaterialCount;
    if (id == kLinkPedalParamId || id == kLinkAmplifierParamId
        || id == kLinkFeedbackParamId) return 2u;
    return 0u;
}

bool isLogUiParam(clap_id id)
{
    return id == kGlideParamId || id == kAttackParamId
        || id == kDecayParamId || id == kReleaseParamId;
}

double uiNormalizedValue(clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return 0.0;
    if (isLogUiParam(id)) {
        return std::clamp(std::log1p(value - def->minimum)
            / std::log1p(def->maximum - def->minimum), 0.0, 1.0);
    }
    return std::clamp((value - def->minimum)
        / std::max(1.0e-12, def->maximum - def->minimum), 0.0, 1.0);
}

double uiValueFromNormalized(clap_id id, double normalized)
{
    const auto* def = paramDef(id);
    if (!def) return 0.0;
    normalized = std::clamp(normalized, 0.0, 1.0);
    const double value = isLogUiParam(id)
        ? def->minimum + std::expm1(
            std::log1p(def->maximum - def->minimum) * normalized)
        : def->minimum + (def->maximum - def->minimum) * normalized;
    return clampValue(*def, value);
}

s3g::ProcessorStackParams publishedParamsSnapshot(const Plugin& plugin)
{
    s3g::ProcessorStackParams params;
    params.mode = static_cast<s3g::ProcessorStackMode>(
        static_cast<uint32_t>(std::lround(paramValue(plugin, kModeParamId))));
    params.shape = static_cast<float>(paramValue(plugin, kShapeParamId));
    params.wire = static_cast<float>(paramValue(plugin, kWireParamId));
    params.pick = static_cast<float>(paramValue(plugin, kPickParamId));
    params.damping = static_cast<float>(paramValue(plugin, kDampingParamId));
    params.glideMs = static_cast<float>(paramValue(plugin, kGlideParamId));
    params.crooked = static_cast<float>(paramValue(plugin, kCrookedParamId));
    params.spill = static_cast<float>(paramValue(plugin, kSpillParamId));
    params.attackMs = static_cast<float>(paramValue(plugin, kAttackParamId));
    params.decayMs = static_cast<float>(paramValue(plugin, kDecayParamId));
    params.sustain = static_cast<float>(paramValue(plugin, kSustainParamId));
    params.releaseMs = static_cast<float>(paramValue(plugin, kReleaseParamId));
    params.pairAmount = static_cast<float>(
        paramValue(plugin, kPairAmountParamId));
    params.pairRelation = static_cast<s3g::ProcessorStackPairRelation>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kPairRelationParamId))));
    params.pairLoose = static_cast<float>(
        paramValue(plugin, kPairLooseParamId));
    params.pairSpread = static_cast<float>(
        paramValue(plugin, kPairSpreadParamId));
    params.rigLevelADb = static_cast<float>(
        paramValue(plugin, kRigLevelAParamId));
    params.rigLevelBDb = static_cast<float>(
        paramValue(plugin, kRigLevelBParamId));
    params.rigPanA = static_cast<float>(
        paramValue(plugin, kRigPanAParamId));
    params.rigPanB = static_cast<float>(
        paramValue(plugin, kRigPanBParamId));
    params.rigMuteA = paramValue(plugin, kRigMuteAParamId) >= 0.5;
    params.rigMuteB = paramValue(plugin, kRigMuteBParamId) >= 0.5;
    params.speaker = static_cast<s3g::ProcessorStackSpeakerProfile>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kSpeakerAParamId))));
    params.speakerB = static_cast<s3g::ProcessorStackSpeakerProfile>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kSpeakerBParamId))));
    params.neckA = static_cast<s3g::ProcessorStackNeckMaterial>(
        static_cast<uint32_t>(std::lround(paramValue(plugin, kNeckAParamId))));
    params.bodyA = static_cast<s3g::ProcessorStackBodyMaterial>(
        static_cast<uint32_t>(std::lround(paramValue(plugin, kBodyAParamId))));
    params.neckB = static_cast<s3g::ProcessorStackNeckMaterial>(
        static_cast<uint32_t>(std::lround(paramValue(plugin, kNeckBParamId))));
    params.bodyB = static_cast<s3g::ProcessorStackBodyMaterial>(
        static_cast<uint32_t>(std::lround(paramValue(plugin, kBodyBParamId))));
    params.arpHostSync = paramValue(plugin, kArpHostSyncParamId) >= 0.5;
    params.arpPattern = static_cast<s3g::ProcessorStackArpPattern>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kArpPatternParamId))));
    params.scale = static_cast<s3g::ProcessorStackScale>(
        static_cast<uint32_t>(std::lround(paramValue(plugin, kScaleParamId))));
    params.arpRate = static_cast<s3g::ProcessorStackArpRate>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kArpRateParamId))));
    params.arpOctaves = static_cast<uint32_t>(std::lround(
        paramValue(plugin, kArpOctavesParamId)));
    params.arpGate = static_cast<float>(paramValue(plugin, kArpGateParamId));
    params.customPatternLength = static_cast<uint32_t>(std::lround(
        paramValue(plugin, kCustomLengthParamId)));
    for (uint32_t index = 0u; index < params.customPattern.size(); ++index) {
        params.customPattern[index] = static_cast<int32_t>(std::lround(
            paramValue(plugin, kCustomStep1ParamId + index)));
    }
    params.pierce = static_cast<float>(paramValue(plugin, kPierceParamId));
    params.selfListen = static_cast<float>(
        paramValue(plugin, kSelfListenParamId));
    params.targetGlitch = static_cast<float>(
        paramValue(plugin, kTargetGlitchParamId));
    params.glitchRatchet = static_cast<float>(
        paramValue(plugin, kGlitchRatchetParamId));
    params.overloadMask = static_cast<float>(
        paramValue(plugin, kOverloadMaskParamId));
    params.circuit = static_cast<s3g::ProcessorStackCircuit>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kCircuitParamId))));
    params.bite = static_cast<float>(paramValue(plugin, kBiteParamId));
    params.pedalTone = static_cast<float>(
        paramValue(plugin, kPedalToneParamId));
    params.bias = static_cast<float>(paramValue(plugin, kBiasParamId));
    params.stack = static_cast<float>(paramValue(plugin, kStackParamId));
    params.sag = static_cast<float>(paramValue(plugin, kSagParamId));
    params.focus = static_cast<float>(paramValue(plugin, kFocusParamId));
    params.cone = static_cast<float>(paramValue(plugin, kConeParamId));
    params.cabinet = static_cast<float>(paramValue(plugin, kCabinetParamId));
    params.mic = static_cast<float>(paramValue(plugin, kMicParamId));
    params.feedback = static_cast<float>(
        paramValue(plugin, kFeedbackParamId));
    params.proximity = static_cast<float>(
        paramValue(plugin, kProximityParamId));
    params.harmonic = static_cast<float>(
        paramValue(plugin, kHarmonicParamId));
    params.tracking = static_cast<float>(paramValue(plugin, kTrackingParamId));
    params.polarity = static_cast<float>(paramValue(plugin, kPolarityParamId));
    params.root = static_cast<float>(paramValue(plugin, kRootParamId));
    params.chaos = static_cast<float>(paramValue(plugin, kChaosParamId));
    params.arpBRelation = static_cast<s3g::ProcessorStackArpRelation>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kArpBRelationParamId))));
    params.arpPatternB = static_cast<s3g::ProcessorStackArpPattern>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kArpPatternBParamId))));
    params.scaleB = static_cast<s3g::ProcessorStackScale>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kScaleBParamId))));
    params.arpRateB = static_cast<s3g::ProcessorStackArpRate>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kArpRateBParamId))));
    params.arpOctavesB = static_cast<uint32_t>(std::lround(
        paramValue(plugin, kArpOctavesBParamId)));
    params.arpGateB = static_cast<float>(
        paramValue(plugin, kArpGateBParamId));
    params.arpPhaseB = static_cast<float>(
        paramValue(plugin, kArpPhaseBParamId));
    params.customPatternLengthB = static_cast<uint32_t>(std::lround(
        paramValue(plugin, kCustomLengthBParamId)));
    for (uint32_t index = 0u; index < params.customPatternB.size(); ++index) {
        params.customPatternB[index] = static_cast<int32_t>(std::lround(
            paramValue(plugin, kCustomStepB1ParamId + index)));
    }
    params.linkPedal = paramValue(plugin, kLinkPedalParamId) >= 0.5;
    params.linkAmplifier = paramValue(plugin, kLinkAmplifierParamId) >= 0.5;
    params.linkFeedback = paramValue(plugin, kLinkFeedbackParamId) >= 0.5;
    params.circuitB = static_cast<s3g::ProcessorStackCircuit>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kCircuitBParamId))));
    params.biteB = static_cast<float>(paramValue(plugin, kBiteBParamId));
    params.pedalToneB = static_cast<float>(
        paramValue(plugin, kPedalToneBParamId));
    params.biasB = static_cast<float>(paramValue(plugin, kBiasBParamId));
    params.stackB = static_cast<float>(paramValue(plugin, kStackBParamId));
    params.sagB = static_cast<float>(paramValue(plugin, kSagBParamId));
    params.focusB = static_cast<float>(paramValue(plugin, kFocusBParamId));
    params.coneB = static_cast<float>(paramValue(plugin, kConeBParamId));
    params.cabinetB = static_cast<float>(
        paramValue(plugin, kCabinetBParamId));
    params.micB = static_cast<float>(paramValue(plugin, kMicBParamId));
    params.feedbackB = static_cast<float>(
        paramValue(plugin, kFeedbackBParamId));
    params.proximityB = static_cast<float>(
        paramValue(plugin, kProximityBParamId));
    params.harmonicB = static_cast<float>(
        paramValue(plugin, kHarmonicBParamId));
    params.trackingB = static_cast<float>(
        paramValue(plugin, kTrackingBParamId));
    params.polarityB = static_cast<float>(
        paramValue(plugin, kPolarityBParamId));
    params.rootB = static_cast<float>(paramValue(plugin, kRootBParamId));
    params.chaosB = static_cast<float>(paramValue(plugin, kChaosBParamId));
    params.pierceB = static_cast<float>(paramValue(plugin, kPierceBParamId));
    params.selfListenB = static_cast<float>(
        paramValue(plugin, kSelfListenBParamId));
    params.targetGlitchB = static_cast<float>(
        paramValue(plugin, kTargetGlitchBParamId));
    params.glitchRatchetB = static_cast<float>(
        paramValue(plugin, kGlitchRatchetBParamId));
    params.overloadMaskB = static_cast<float>(
        paramValue(plugin, kOverloadMaskBParamId));
    params.outputGainDb = static_cast<float>(
        paramValue(plugin, kOutputParamId));
    return s3g::sanitizeProcessorStackParams(params);
}

float randomUnit(uint32_t& state)
{
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return static_cast<float>(state & 0x00ffffffu) / 16777215.0f;
}

s3g::ProcessorStackParams safeRandomParams(
    const Plugin& plugin, uint32_t seed)
{
    auto params = publishedParamsSnapshot(plugin);
    // Preserve the performer's audition level and established two-guitar frame.
    // RANDOM varies what the rigs play and how they sound, not their pairing.
    params.mode = static_cast<s3g::ProcessorStackMode>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed) * 3.0f), 2u));
    params.shape = randomUnit(seed);
    params.wire = 0.34f + randomUnit(seed) * 0.64f;
    params.pick = 0.34f + randomUnit(seed) * 0.66f;
    params.damping = 0.18f + randomUnit(seed) * 0.68f;
    params.glideMs = randomUnit(seed) * randomUnit(seed) * 280.0f;
    params.crooked = randomUnit(seed) * 0.94f;
    params.spill = randomUnit(seed) * 0.78f;
    params.attackMs = randomUnit(seed) * randomUnit(seed) * 620.0f;
    params.decayMs = 24.0f + randomUnit(seed) * randomUnit(seed) * 2800.0f;
    params.sustain = 0.04f + randomUnit(seed) * 0.94f;
    params.releaseMs = 18.0f
        + randomUnit(seed) * randomUnit(seed) * 5200.0f;
    params.neckA = static_cast<s3g::ProcessorStackNeckMaterial>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackNeckMaterialCount)),
            s3g::kProcessorStackNeckMaterialCount - 1u));
    params.bodyA = static_cast<s3g::ProcessorStackBodyMaterial>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackBodyMaterialCount)),
            s3g::kProcessorStackBodyMaterialCount - 1u));
    params.neckB = static_cast<s3g::ProcessorStackNeckMaterial>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackNeckMaterialCount)),
            s3g::kProcessorStackNeckMaterialCount - 1u));
    params.bodyB = static_cast<s3g::ProcessorStackBodyMaterial>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackBodyMaterialCount)),
            s3g::kProcessorStackBodyMaterialCount - 1u));
    params.arpPattern = static_cast<s3g::ProcessorStackArpPattern>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackArpPatternCount)),
            s3g::kProcessorStackArpPatternCount - 1u));
    params.scale = static_cast<s3g::ProcessorStackScale>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackScaleCount)),
            s3g::kProcessorStackScaleCount - 1u));
    params.arpRate = static_cast<s3g::ProcessorStackArpRate>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackArpRateCount)),
            s3g::kProcessorStackArpRateCount - 1u));
    params.arpOctaves = 1u + std::min<uint32_t>(3u,
        static_cast<uint32_t>(randomUnit(seed) * 4.0f));
    params.arpGate = 0.22f + randomUnit(seed) * 0.68f;
    params.customPatternLength = 3u + std::min<uint32_t>(5u,
        static_cast<uint32_t>(randomUnit(seed) * 6.0f));
    for (auto& step : params.customPattern) {
        step = -4 + static_cast<int32_t>(randomUnit(seed) * 16.0f);
    }
    params.pierce = 0.34f + randomUnit(seed) * 0.66f;
    params.selfListen = 0.42f + randomUnit(seed) * 0.58f;
    params.targetGlitch = randomUnit(seed) * 0.88f;
    params.glitchRatchet = 0.16f + randomUnit(seed) * 0.76f;
    params.overloadMask = 0.58f + randomUnit(seed) * 0.42f;
    params.circuit = static_cast<s3g::ProcessorStackCircuit>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackCircuitCount)),
            s3g::kProcessorStackCircuitCount - 1u));
    params.bite = 0.22f + randomUnit(seed) * 0.70f;
    params.pedalTone = 0.20f + randomUnit(seed) * 0.68f;
    params.bias = 0.16f + randomUnit(seed) * 0.72f;
    params.stack = 0.34f + randomUnit(seed) * 0.58f;
    params.sag = randomUnit(seed) * 0.88f;
    params.focus = 0.16f + randomUnit(seed) * 0.76f;
    params.speaker = static_cast<s3g::ProcessorStackSpeakerProfile>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackSpeakerProfileCount)),
            s3g::kProcessorStackSpeakerProfileCount - 1u));
    params.cone = 0.28f + randomUnit(seed) * 0.68f;
    params.cabinet = randomUnit(seed);
    params.mic = randomUnit(seed) * 0.88f;
    params.feedback = 0.18f + randomUnit(seed) * 0.70f;
    params.proximity = 0.18f + randomUnit(seed) * 0.72f;
    params.harmonic = randomUnit(seed);
    params.tracking = 0.18f + randomUnit(seed) * 0.78f;
    params.polarity = 0.12f + randomUnit(seed) * 0.76f;
    params.root = randomUnit(seed) * 0.58f;
    params.chaos = randomUnit(seed) * 0.84f;
    params.arpBRelation = static_cast<s3g::ProcessorStackArpRelation>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackArpRelationCount)),
            s3g::kProcessorStackArpRelationCount - 1u));
    params.arpPatternB = static_cast<s3g::ProcessorStackArpPattern>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackArpPatternCount)),
            s3g::kProcessorStackArpPatternCount - 1u));
    params.scaleB = static_cast<s3g::ProcessorStackScale>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackScaleCount)),
            s3g::kProcessorStackScaleCount - 1u));
    params.arpRateB = static_cast<s3g::ProcessorStackArpRate>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackArpRateCount)),
            s3g::kProcessorStackArpRateCount - 1u));
    params.arpOctavesB = 1u + std::min<uint32_t>(3u,
        static_cast<uint32_t>(randomUnit(seed) * 4.0f));
    params.arpGateB = 0.22f + randomUnit(seed) * 0.68f;
    params.arpPhaseB = randomUnit(seed);
    params.customPatternLengthB = 3u + std::min<uint32_t>(5u,
        static_cast<uint32_t>(randomUnit(seed) * 6.0f));
    for (auto& step : params.customPatternB) {
        step = -4 + static_cast<int32_t>(randomUnit(seed) * 16.0f);
    }
    params.linkPedal = randomUnit(seed) > 0.62f;
    params.linkAmplifier = randomUnit(seed) > 0.62f;
    params.linkFeedback = randomUnit(seed) > 0.62f;
    params.circuitB = static_cast<s3g::ProcessorStackCircuit>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackCircuitCount)),
            s3g::kProcessorStackCircuitCount - 1u));
    params.biteB = 0.22f + randomUnit(seed) * 0.70f;
    params.pedalToneB = 0.20f + randomUnit(seed) * 0.68f;
    params.biasB = 0.16f + randomUnit(seed) * 0.72f;
    params.stackB = 0.34f + randomUnit(seed) * 0.58f;
    params.sagB = randomUnit(seed) * 0.88f;
    params.focusB = 0.16f + randomUnit(seed) * 0.76f;
    params.speakerB = static_cast<s3g::ProcessorStackSpeakerProfile>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackSpeakerProfileCount)),
            s3g::kProcessorStackSpeakerProfileCount - 1u));
    params.coneB = 0.28f + randomUnit(seed) * 0.68f;
    params.cabinetB = randomUnit(seed);
    params.micB = randomUnit(seed) * 0.88f;
    params.feedbackB = 0.18f + randomUnit(seed) * 0.70f;
    params.proximityB = 0.18f + randomUnit(seed) * 0.72f;
    params.harmonicB = randomUnit(seed);
    params.trackingB = 0.18f + randomUnit(seed) * 0.78f;
    params.polarityB = 0.12f + randomUnit(seed) * 0.76f;
    params.rootB = randomUnit(seed) * 0.58f;
    params.chaosB = randomUnit(seed) * 0.84f;
    params.pierceB = 0.34f + randomUnit(seed) * 0.66f;
    params.selfListenB = 0.42f + randomUnit(seed) * 0.58f;
    params.targetGlitchB = randomUnit(seed) * 0.88f;
    params.glitchRatchetB = 0.16f + randomUnit(seed) * 0.76f;
    params.overloadMaskB = 0.58f + randomUnit(seed) * 0.42f;
    return s3g::sanitizeProcessorStackParams(params);
}

drawing::Rect stackPageButtonRect(uint8_t page)
{
    const drawing::Rect panel = drawing::cocoaRect(kOutputPanel.frame);
    constexpr double width = 58.0;
    constexpr double height = 15.0;
    constexpr double gap = 4.0;
    constexpr double totalWidth = width * 4.0 + gap * 3.0;
    return drawing::makeRect(drawing::maxX(panel) - 8.0 - totalWidth
            + static_cast<double>(page) * (width + gap),
        panel.origin.y + 3.0, width, height);
}

drawing::Rect stackCopyButtonRect(uint8_t index)
{
    const drawing::Rect panel = drawing::cocoaRect(kLinksPanel.frame);
    constexpr double width = 88.0;
    constexpr double height = 15.0;
    constexpr double gap = 4.0;
    constexpr double totalWidth = width * 2.0 + gap;
    return drawing::makeRect(drawing::maxX(panel) - 8.0 - totalWidth
            + static_cast<double>(index) * (width + gap),
        panel.origin.y + 3.0, width, height);
}

constexpr uint32_t kStackScoreRandomActionCount = 5u;

drawing::Rect stackScoreRandomActionButtonRect(uint32_t action)
{
    const drawing::Rect panel = drawing::cocoaRect(kScoreTimingPanel.frame);
    constexpr double width = 50.0;
    constexpr double height = 15.0;
    constexpr double gap = 3.0;
    constexpr double totalWidth = width * kStackScoreRandomActionCount
        + gap * (kStackScoreRandomActionCount - 1u);
    return drawing::makeRect(drawing::maxX(panel) - 8.0 - totalWidth
            + static_cast<double>(std::min<uint32_t>(action,
                kStackScoreRandomActionCount - 1u)) * (width + gap),
        panel.origin.y + 3.0, width, height);
}

const layout::Panel& stackPatternPanel(uint8_t player)
{
    return player == 0u ? kPatternAPanel : kPatternBPanel;
}

drawing::Rect stackPatternFieldRect(uint8_t player)
{
    const drawing::Rect panel = drawing::cocoaRect(
        stackPatternPanel(player).frame);
    return drawing::makeRect(panel.origin.x + 16.0, panel.origin.y + 31.0,
        panel.size.width - 32.0, 72.0);
}

drawing::Rect stackPatternStepRect(uint8_t player, uint32_t step)
{
    const drawing::Rect field = stackPatternFieldRect(player);
    const double stepWidth = field.size.width / 8.0;
    return drawing::makeRect(field.origin.x + stepWidth
            * static_cast<double>(std::min<uint32_t>(step, 7u)),
        field.origin.y, stepWidth, field.size.height);
}

clap_id stackPatternStepParam(uint8_t player, uint32_t step)
{
    return (player == 0u ? kCustomStep1ParamId : kCustomStepB1ParamId)
        + std::min<uint32_t>(step, 7u);
}

clap_id stackPatternLengthParam(uint8_t player)
{
    return player == 0u ? kCustomLengthParamId : kCustomLengthBParamId;
}

int stackPatternStepAtX(uint8_t player, double x)
{
    const drawing::Rect field = stackPatternFieldRect(player);
    if (x < field.origin.x || x > drawing::maxX(field)) return -1;
    const double normalized = std::clamp(
        (x - field.origin.x) / field.size.width,
        static_cast<double>(0.0), static_cast<double>(0.999999));
    return static_cast<int>(std::floor(normalized * 8.0));
}

constexpr double kScoreGridTop = 252.0;
constexpr double kScoreGridLeft = 16.0;
constexpr double kScoreGridWidth = 948.0;
constexpr double kScoreGridHeaderHeight = 34.0;
constexpr double kScoreGridRowHeight = 24.0;
constexpr double kScoreRowNumberWidth = 38.0;
constexpr double kScorePlayerGap = 12.0;
constexpr double kScoreLockCellWidth = 43.0;

drawing::Rect stackScoreGridRect()
{
    return drawing::makeRect(kScoreGridLeft, kScoreGridTop, kScoreGridWidth,
        kScoreGridHeaderHeight
            + kScoreGridRowHeight
                * s3g::kProcessorStackScoreRowsPerSection);
}

double stackScorePlayerWidth()
{
    return (kScoreGridWidth - kScoreRowNumberWidth - kScorePlayerGap) * 0.5;
}

double stackScorePlayerX(uint32_t player)
{
    return kScoreGridLeft + kScoreRowNumberWidth
        + static_cast<double>(std::min(player, 1u))
            * (stackScorePlayerWidth() + kScorePlayerGap);
}

double stackScoreStringAreaWidth()
{
    return stackScorePlayerWidth()
        - kScoreLockCellWidth * s3g::kProcessorStackScoreLocksPerPlayer;
}

double stackScoreStringAreaX(uint32_t player)
{
    return stackScorePlayerX(player)
        + (player == 0u ? 0.0
            : kScoreLockCellWidth
                * s3g::kProcessorStackScoreLocksPerPlayer);
}

drawing::Rect stackScoreCellRect(uint32_t player, uint32_t string, uint32_t row)
{
    const double cellWidth = stackScoreStringAreaWidth()
        / s3g::kProcessorStackScoreStringCount;
    return drawing::makeRect(stackScoreStringAreaX(player)
            + static_cast<double>(string) * cellWidth,
        kScoreGridTop + kScoreGridHeaderHeight
            + static_cast<double>(row) * kScoreGridRowHeight,
        cellWidth, kScoreGridRowHeight);
}

drawing::Rect stackScoreLockRect(uint32_t player, uint32_t slot, uint32_t row)
{
    player = std::min(player, 1u);
    slot = std::min(slot, s3g::kProcessorStackScoreLocksPerPlayer - 1u);
    const double x = player == 0u
        ? stackScoreStringAreaX(player) + stackScoreStringAreaWidth()
            + static_cast<double>(slot) * kScoreLockCellWidth
        : stackScorePlayerX(player)
            + static_cast<double>(slot) * kScoreLockCellWidth;
    return drawing::makeRect(x,
        kScoreGridTop + kScoreGridHeaderHeight
            + static_cast<double>(row) * kScoreGridRowHeight,
        kScoreLockCellWidth, kScoreGridRowHeight);
}

drawing::Rect stackScoreRowRect(uint32_t row)
{
    return drawing::makeRect(kScoreGridLeft,
        kScoreGridTop + kScoreGridHeaderHeight
            + static_cast<double>(std::min(row,
                s3g::kProcessorStackScoreRowsPerSection - 1u))
                * kScoreGridRowHeight,
        kScoreGridWidth, kScoreGridRowHeight);
}

bool stackScoreCellAtPoint(drawing::Point point, uint32_t& player,
    uint32_t& string, uint32_t& row)
{
    for (uint32_t candidatePlayer = 0u;
         candidatePlayer < s3g::kProcessorStackScorePlayerCount;
         ++candidatePlayer) {
        for (uint32_t candidateString = 0u;
             candidateString < s3g::kProcessorStackScoreStringCount;
             ++candidateString) {
            for (uint32_t candidateRow = 0u;
                 candidateRow < s3g::kProcessorStackScoreRowsPerSection;
                 ++candidateRow) {
                if (!drawing::contains(point, stackScoreCellRect(candidatePlayer,
                        candidateString, candidateRow))) continue;
                player = candidatePlayer;
                string = candidateString;
                row = candidateRow;
                return true;
            }
        }
    }
    return false;
}

bool stackScoreLockAtPoint(drawing::Point point, uint32_t& player,
    uint32_t& slot, uint32_t& row)
{
    for (uint32_t candidatePlayer = 0u;
         candidatePlayer < s3g::kProcessorStackScorePlayerCount;
         ++candidatePlayer) {
        for (uint32_t candidateSlot = 0u;
             candidateSlot < s3g::kProcessorStackScoreLocksPerPlayer;
             ++candidateSlot) {
            for (uint32_t candidateRow = 0u;
                 candidateRow < s3g::kProcessorStackScoreRowsPerSection;
                 ++candidateRow) {
                if (!drawing::contains(point, stackScoreLockRect(
                        candidatePlayer, candidateSlot, candidateRow))) {
                    continue;
                }
                player = candidatePlayer;
                slot = candidateSlot;
                row = candidateRow;
                return true;
            }
        }
    }
    return false;
}

drawing::Rect stackScoreSectionButtonRect(uint32_t section)
{
    const drawing::Rect relationship = drawing::cocoaRect(
        kScoreRelationPanel.frame);
    constexpr double gap = 7.0;
    const double width = (relationship.size.width
        - gap * (s3g::kProcessorStackScoreSectionCount - 1u))
        / s3g::kProcessorStackScoreSectionCount;
    return drawing::makeRect(relationship.origin.x
            + static_cast<double>(section) * (width + gap),
        drawing::maxY(relationship) + 9.0, width, 30.0);
}

clap_id stackScoreLockParamId(uint32_t player,
    s3g::ProcessorStackScoreLockControl control)
{
    const bool b = player != 0u;
    using Control = s3g::ProcessorStackScoreLockControl;
    switch (control) {
    case Control::Neck: return b ? kNeckBParamId : kNeckAParamId;
    case Control::Body: return b ? kBodyBParamId : kBodyAParamId;
    case Control::Circuit: return b ? kCircuitBParamId : kCircuitParamId;
    case Control::Bite: return b ? kBiteBParamId : kBiteParamId;
    case Control::Tone: return b ? kPedalToneBParamId : kPedalToneParamId;
    case Control::Bias: return b ? kBiasBParamId : kBiasParamId;
    case Control::Stack: return b ? kStackBParamId : kStackParamId;
    case Control::Sag: return b ? kSagBParamId : kSagParamId;
    case Control::Focus: return b ? kFocusBParamId : kFocusParamId;
    case Control::Speaker: return b ? kSpeakerBParamId : kSpeakerAParamId;
    case Control::Cone: return b ? kConeBParamId : kConeParamId;
    case Control::Cabinet: return b ? kCabinetBParamId : kCabinetParamId;
    case Control::Mic: return b ? kMicBParamId : kMicParamId;
    case Control::Feedback: return b ? kFeedbackBParamId : kFeedbackParamId;
    case Control::Proximity: return b ? kProximityBParamId : kProximityParamId;
    case Control::Harmonic: return b ? kHarmonicBParamId : kHarmonicParamId;
    case Control::Tracking: return b ? kTrackingBParamId : kTrackingParamId;
    case Control::Polarity: return b ? kPolarityBParamId : kPolarityParamId;
    case Control::Root: return b ? kRootBParamId : kRootParamId;
    case Control::Chaos: return b ? kChaosBParamId : kChaosParamId;
    case Control::Pierce: return b ? kPierceBParamId : kPierceParamId;
    case Control::SelfListen: return b ? kSelfListenBParamId : kSelfListenParamId;
    case Control::TargetGlitch: return b ? kTargetGlitchBParamId : kTargetGlitchParamId;
    case Control::Ratchet: return b ? kGlitchRatchetBParamId : kGlitchRatchetParamId;
    case Control::OverloadMask: return b ? kOverloadMaskBParamId : kOverloadMaskParamId;
    case Control::None:
    case Control::Count:
        return CLAP_INVALID_ID;
    }
    return CLAP_INVALID_ID;
}

s3g::ProcessorStackScoreLockCell stackScoreLockCell(
    const Plugin& plugin, uint32_t section, uint32_t row,
    uint32_t player, uint32_t slot)
{
    const size_t index = s3g::processorStackScoreLockIndex(
        section, row, player, slot);
    const uint32_t packed = plugin.scoreLocks[index].load(
        std::memory_order_acquire);
    s3g::ProcessorStackScoreLockCell lock;
    lock.control = static_cast<uint8_t>(packed & 0xffu);
    lock.normalized = static_cast<uint16_t>((packed >> 8u) & 0xffffu);
    return lock;
}

drawing::Rect stackScoreArrangementRect()
{
    return drawing::makeRect(kScoreGridLeft,
        kScoreGridTop + kScoreGridHeaderHeight
            + kScoreGridRowHeight
                * s3g::kProcessorStackScoreRowsPerSection + 12.0,
        kScoreGridWidth, 58.0);
}

drawing::Rect stackScoreArrangementSlotRect(uint32_t slot)
{
    const drawing::Rect panel = stackScoreArrangementRect();
    constexpr double left = 128.0;
    constexpr double gap = 5.0;
    const double width = (panel.size.width - left - 14.0
        - gap * (s3g::kProcessorStackScoreArrangementSlots - 1u))
        / s3g::kProcessorStackScoreArrangementSlots;
    return drawing::makeRect(panel.origin.x + left
            + static_cast<double>(slot) * (width + gap),
        panel.origin.y + 27.0, width, 20.0);
}

drawing::Rect stackScoreEntryLegendRect()
{
    const drawing::Rect arrangement = stackScoreArrangementRect();
    return drawing::makeRect(arrangement.origin.x, drawing::maxY(arrangement) + 6.0,
        arrangement.size.width, 22.0);
}

void drawStackPatternMultislider(uint8_t player,
                                 const Plugin& plugin,
                                 int dragPlayer,
                                 int dragStep,
                                 drawing::Attrs  valueAttrs,
                                 const drawing::Style& style)
{
    const drawing::Rect field = stackPatternFieldRect(player);
    drawing::setFill(style.strip);
    drawing::rectFill(field);
    drawing::setStroke(style.grid);
    drawing::frameRect(field);

    const uint32_t length = std::clamp<uint32_t>(static_cast<uint32_t>(
        std::lround(paramValue(plugin, stackPatternLengthParam(player)))),
        1u, 8u);

    const drawing::Rect panel = drawing::cocoaRect(
        stackPatternPanel(player).frame);
    for (uint32_t step = 0u; step < 8u; ++step) {
        const drawing::Rect cell = stackPatternStepRect(player, step);
        const clap_id id = stackPatternStepParam(player, step);
        const double value = paramValue(plugin, id);
        const bool rest = static_cast<int32_t>(std::lround(value))
            == s3g::kProcessorStackArpRest;
        const double rawValueY = drawing::maxY(field) - static_cast<double>(
            uiNormalizedValue(id, value)) * field.size.height;
        const double valueY = std::clamp(rawValueY,
            field.origin.y + 1.0, drawing::maxY(field) - 1.0);
        const bool selected = dragPlayer == static_cast<int>(player)
            && dragStep == static_cast<int>(step);
        VSTGUI::CColor  lineColor = selected ? style.text
            : (step < length ? style.accent : style.grid);
        if (rest) {
            drawing::setFill(drawing::withAlpha(lineColor, selected ? 0.36 : 0.22));
            drawing::rectFill(drawing::inset(cell, 5.0, 5.0));
        } else {
            drawing::setFill(lineColor);
            drawing::rectFill(drawing::makeRect(cell.origin.x + 5.0,
                std::floor(valueY), cell.size.width - 10.0, 2.0));
        }

        drawing::String  stepText = rest
            ? drawing::stringFormat("%u REST", step + 1u)
            : drawing::stringFormat("%u %+d", step + 1u,
                static_cast<int>(std::lround(value)));
        drawing::drawCenteredTextToFit(stepText,
            drawing::makeRect(cell.origin.x + 2.0, panel.origin.y + 108.0,
                cell.size.width - 4.0, 14.0), valueAttrs);
    }
}

bool queueRigCopy(Plugin& plugin, bool aToB)
{
    static constexpr std::array<clap_id, 25u> rigA {{
        kNeckAParamId, kBodyAParamId,
        kCircuitParamId, kBiteParamId, kPedalToneParamId, kBiasParamId,
        kStackParamId, kSagParamId, kFocusParamId, kSpeakerAParamId,
        kConeParamId,
        kCabinetParamId, kMicParamId,
        kFeedbackParamId, kProximityParamId, kHarmonicParamId,
        kTrackingParamId, kPolarityParamId, kRootParamId, kChaosParamId,
        kPierceParamId, kSelfListenParamId, kTargetGlitchParamId,
        kGlitchRatchetParamId, kOverloadMaskParamId,
    }};
    static constexpr std::array<clap_id, 25u> rigB {{
        kNeckBParamId, kBodyBParamId,
        kCircuitBParamId, kBiteBParamId, kPedalToneBParamId, kBiasBParamId,
        kStackBParamId, kSagBParamId, kFocusBParamId, kSpeakerBParamId,
        kConeBParamId,
        kCabinetBParamId, kMicBParamId,
        kFeedbackBParamId, kProximityBParamId, kHarmonicBParamId,
        kTrackingBParamId, kPolarityBParamId, kRootBParamId, kChaosBParamId,
        kPierceBParamId, kSelfListenBParamId, kTargetGlitchBParamId,
        kGlitchRatchetBParamId, kOverloadMaskBParamId,
    }};
    using Kind = s3g::clap_gui::ParamEventKind;
    std::array<s3g::clap_gui::ParamEvent, rigA.size() * 3u> events {};
    const auto& source = aToB ? rigA : rigB;
    const auto& destination = aToB ? rigB : rigA;
    for (uint32_t index = 0u; index < source.size(); ++index) {
        const double value = paramValue(plugin, source[index]);
        events[index * 3u] = {
            Kind::GestureBegin, destination[index], 0.0 };
        events[index * 3u + 1u] = {
            Kind::Value, destination[index], value };
        events[index * 3u + 2u] = {
            Kind::GestureEnd, destination[index], 0.0 };
    }
    if (!plugin.guiParamEvents.pushBatch(events.data(), events.size())) {
        return false;
    }
    for (uint32_t index = 0u; index < source.size(); ++index) {
        publishParam(plugin, destination[index],
            paramValue(plugin, source[index]));
    }
    requestGuiParamService(plugin);
    return true;
}

class Editor final : public s3g::portable_gui::routing::Canvas {
public:
using Canvas = s3g::portable_gui::routing::Canvas;

    Plugin* _plugin;
    int _dragParam;
    int _dragPatternPlayer;
    int _dragPatternStep;
    int _factoryPresetIndex;
    uint8_t _page;
    clap_id _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    uint32_t _scoreEditSection;
    uint32_t _scoreSelectedPlayer;
    uint32_t _scoreSelectedString;
    uint32_t _scoreSelectedRow;
    bool _dragScoreLock;
    uint32_t _scoreLockPlayer;
    uint32_t _scoreLockSlot;
    uint32_t _scoreLockRow;
    drawing::Point _scoreLockDragStartPoint;
    double _scoreLockDragStartNormalized;
    int _scorePendingFret;
    double _scorePendingTime;
    int _scoreRandomFeedback;
    double _scoreRandomFeedbackUntil;
    uint64_t _observedParamRevision;
    uint64_t _observedScoreVisualRevision;
    int32_t _pendingScoreSection;
    int32_t _pendingScoreRow;
    int32_t _pendingScoreSlot;
    int32_t _presentedScoreSection;
    int32_t _presentedScoreRow;
    int32_t _presentedScoreSlot;
    bool _scoreVisualPrimed;
    char _presetName[64];
#include "s3g_stereo_processor_editor.inc"
explicit Editor(Plugin& plugin) : Canvas(kGuiWidth,kGuiHeight) {_plugin = &plugin;
    _dragParam = -1;
    _dragPatternPlayer = -1;
    _dragPatternStep = -1;
    _page = 0u;
    _openMenu = CLAP_INVALID_ID;
    _hoverMenuItem = -1;
    _menuItemCount = 0u;
    _scoreEditSection = 0u;
    _scoreSelectedPlayer = 0u;
    _scoreSelectedString = 0u;
    _scoreSelectedRow = 0u;
    _dragScoreLock = false;
    _scoreLockPlayer = 0u;
    _scoreLockSlot = 0u;
    _scoreLockRow = 0u;
    _scoreLockDragStartPoint = drawing::Point{};
    _scoreLockDragStartNormalized = 0.0;
    _scorePendingFret = -1;
    _scorePendingTime = 0.0;
    _scoreRandomFeedback = -1;
    _scoreRandomFeedbackUntil = 0.0;
    auto* instance = static_cast<Plugin*>(_plugin);
    _observedParamRevision = instance
        ? instance->parameterRevision.load(std::memory_order_acquire) : 0u;
    _observedScoreVisualRevision = instance
        ? instance->scoreVisualRevision.load(std::memory_order_acquire) : 0u;
    _pendingScoreSection = _pendingScoreRow = _pendingScoreSlot = -1;
    _presentedScoreSection = _presentedScoreRow = _presentedScoreSlot = -1;
    _scoreVisualPrimed = false;
    _factoryPresetIndex = instance
        ? s3g::processorStackFactoryPresetIndex(
            publishedParamsSnapshot(*instance)) : 0;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        _factoryPresetIndex >= 0
            ? s3g::processorStackFactoryPresetInfo(
                static_cast<uint32_t>(_factoryPresetIndex)).name
            : "CUSTOM");
}

void setDocumentationPage(size_t page)  {
    _page = static_cast<uint8_t>(std::min<size_t>(page, 3u));
    _openMenu = CLAP_INVALID_ID;
    _hoverMenuItem = -1;
    setNeedsDisplay(true);

}

void loadDocumentationScore()  {
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return;
    const auto params = publishedParamsSnapshot(*instance);
    auto program = s3g::generateProcessorStackScore(
        0x53434f52u, params.scale, params.scaleB);
    s3g::setProcessorStackScoreCell(program, 0u, 2u, 0u, 0u,
        s3g::kProcessorStackScoreHold);
    s3g::setProcessorStackScoreCell(program, 0u, 3u, 0u, 0u,
        s3g::kProcessorStackScoreHold);
    s3g::setProcessorStackScoreCell(program, 0u, 3u, 1u, 1u,
        s3g::kProcessorStackScoreHold);
    program = s3g::randomizeProcessorStackScoreLocks(
        program, 0u, 0x4c4f434bu);
    storeScoreProgram(*instance, program, false);
    _scoreEditSection = 0u;
    _scoreSelectedPlayer = 0u;
    _scoreSelectedString = 0u;
    _scoreSelectedRow = 3u;
    _presentedScoreSection = _pendingScoreSection = 0;
    _presentedScoreRow = _pendingScoreRow = 3;
    _presentedScoreSlot = _pendingScoreSlot = 0;
    _scoreVisualPrimed = true;
    _scoreRandomFeedback = 1;
    _scoreRandomFeedbackUntil =
        drawing::uptime() + 60.0;
    setNeedsDisplay(true);

}

void applyFactoryPreset(int index)  {
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return;
    index = std::clamp(index, 0,
        static_cast<int>(s3g::kProcessorStackFactoryPresetCount - 1u));
    if (!queueGuiParams(*instance, s3g::processorStackFactoryPreset(
            static_cast<uint32_t>(index)))) {
        beep();
        return;
    }
    _factoryPresetIndex = index;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        s3g::processorStackFactoryPresetInfo(
            static_cast<uint32_t>(index)).name);
    setNeedsDisplay(true);

}

void markCustomPreset()  {
    _factoryPresetIndex = -1;
    std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");

}

drawing::Rect openMenuRect()  {
    drawing::Rect anchor = drawing::Rect{};
    if (_openMenu == kFactoryPresetMenuId) {
        const auto band = drawing::encoderTitleBand(
            kGuiWidth, kGuiHeight);
        anchor = drawing::cocoaRect(band.presetMenu);
    } else {
        const auto row = std::find_if(kUiRows.begin(), kUiRows.end(),
            [=](const StackUiRow& candidate) {
                return candidate.id == _openMenu
                    && (candidate.page == _page
                        || candidate.page == kAllPages);
            });
        if (row == kUiRows.end() || !isUiMenuParam(row->id)) {
            return drawing::Rect{};
        }
        anchor = drawing::makeRect(
            s3g::gui_layout::processorControlX(row->panelX), row->y - 1.0,
            s3g::gui_layout::processorMenuWidth(row->panelWidth), 15.0);
    }
    const double menuHeight = 18.0 * _menuItemCount;
    double menuY = drawing::maxY(anchor) + 2.0;
    if (menuY + menuHeight > kGuiHeight) {
        menuY = anchor.origin.y - 2.0 - menuHeight;
    }
    return drawing::makeRect(anchor.origin.x, menuY,
        anchor.size.width, menuHeight);

}

void drawOpenMenu_style(drawing::Attrs  attrs, const drawing::Style& style)  {
    if (_openMenu == CLAP_INVALID_ID || _menuItemCount == 0u) return;
    drawing::String  items[32] {};
    const uint32_t count = std::min<uint32_t>(_menuItemCount, 32u);
    if (_openMenu == kFactoryPresetMenuId) {
        for (uint32_t index = 0u; index < count; ++index) {
            items[index] = drawing::stringFromUtf8(s3g::processorStackFactoryPresetInfo(index).name);
        }
    } else {
        for (uint32_t index = 0u; index < count; ++index) {
            char text[64] {};
            const auto* def = paramDef(_openMenu);
            const double menuValue = def
                ? def->minimum + static_cast<double>(index)
                : static_cast<double>(index);
            if (paramsValueToText(
                    &static_cast<Plugin*>(_plugin)->plugin, _openMenu,
                    menuValue, text, sizeof(text))) {
                items[index] = drawing::stringFromUtf8(text);
            } else {
                items[index] = "—";
            }
        }
    }
    const int selected = _openMenu == kFactoryPresetMenuId
        ? _factoryPresetIndex
        : static_cast<int>(std::lround(paramValue(
            *static_cast<Plugin*>(_plugin), _openMenu)
                - paramDef(_openMenu)->minimum));
    drawing::drawDropdownMenu(openMenuRect(), 18.0,
        items, count, selected, _hoverMenuItem, attrs, style);

}

void updateDraggedScoreLock(drawing::Point point)  {
    if (!_dragScoreLock) return;
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return;
    const auto lock = stackScoreLockCell(*instance, _scoreEditSection,
        _scoreLockRow, _scoreLockPlayer, _scoreLockSlot);
    if (lock.control == static_cast<uint8_t>(
            s3g::ProcessorStackScoreLockControl::None)) return;
    const double normalized = std::clamp(_scoreLockDragStartNormalized
        + static_cast<double>(_scoreLockDragStartPoint.y - point.y) / 120.0,
        0.0, 1.0);
    setScoreLock(*instance, _scoreEditSection, _scoreLockRow,
        _scoreLockPlayer, _scoreLockSlot,
        static_cast<s3g::ProcessorStackScoreLockControl>(lock.control),
        normalized);
    setNeedsDisplayInRect(drawing::inset(stackScoreLockRect(
        _scoreLockPlayer, _scoreLockSlot, _scoreLockRow), -1.0, -1.0));

}

void drawScore_values_style(drawing::Attrs  labelAttrs, drawing::Attrs  valueAttrs, const drawing::Style& style)  {
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return;
    const bool scoreBRelatesToA = paramValue(
        *instance, kScoreBSourceParamId) >= 0.5;
    const uint32_t scoreLength = static_cast<uint32_t>(std::clamp(
        std::lround(paramValue(*instance, kScoreLengthParamId)),
        1l, static_cast<long>(s3g::kProcessorStackScoreArrangementSlots)));
    const drawing::Rect grid = stackScoreGridRect();
    VSTGUI::CColor  const scoreTeal = s3g::portable_gui::canvas::rgba(0.055, 0.255, 0.275, 0.34);
    VSTGUI::CColor  const scoreTealStrong = s3g::portable_gui::canvas::rgba(0.075, 0.34, 0.36, 0.62);
    // Edit and lock fills stay deliberately darker than the playhead teal.
    // Their light-gray text is the information; color only marks context.
    VSTGUI::CColor  const scoreEntryFill = s3g::portable_gui::canvas::rgba(0.025, 0.115, 0.125, 0.96);
    VSTGUI::CColor  const scoreLockFill = s3g::portable_gui::canvas::rgba(0.025, 0.085, 0.095, 0.96);
    drawing::setFill(style.cell);
    drawing::rectFill(grid);
    drawing::setStroke(style.grid);
    drawing::frameRect(grid);
    drawing::setFill(style.strip);
    drawing::rectFill(drawing::makeRect(grid.origin.x, grid.origin.y,
        grid.size.width, kScoreGridHeaderHeight));

    for (uint32_t section = 0u;
         section < s3g::kProcessorStackScoreSectionCount; ++section) {
        const drawing::Rect button = stackScoreSectionButtonRect(section);
        drawing::setFill((section == _scoreEditSection ? scoreTealStrong : style.cell));
        drawing::rectFill(button);
        drawing::setStroke((section == _scoreEditSection ? style.accent : style.grid));
        drawing::frameRect(button);
        drawing::String  text = drawing::stringFormat("SECTION %c",
            static_cast<char>('A' + section));
        drawing::drawCenteredTextToFit(text,
            drawing::makeRect(button.origin.x + 4.0, button.origin.y + 7.0,
                button.size.width - 8.0, 16.0),
            section == _scoreEditSection ? labelAttrs : valueAttrs);
    }

    const double playerWidth = stackScorePlayerWidth();
    const double cellWidth = stackScoreStringAreaWidth()
        / s3g::kProcessorStackScoreStringCount;
    static drawing::String  const strings[6] {
        "E", "A", "D", "G", "B", "e",
    };
    for (uint32_t player = 0u;
         player < s3g::kProcessorStackScorePlayerCount; ++player) {
        const double playerX = stackScorePlayerX(player);
        drawing::String  title = player == 0u ? "PLAYER A · TAB"
            : (scoreBRelatesToA
                ? "PLAYER B · RELATE A" : "PLAYER B · EXPLICIT TAB");
        drawing::drawCenteredTextToFit(title,
            drawing::makeRect(playerX, kScoreGridTop + 1.0,
                playerWidth, 14.0), labelAttrs);
        for (uint32_t string = 0u;
             string < s3g::kProcessorStackScoreStringCount; ++string) {
            drawing::drawCenteredTextToFit(strings[string],
                drawing::makeRect(stackScoreStringAreaX(player)
                        + static_cast<double>(string) * cellWidth,
                    kScoreGridTop + 17.0, cellWidth, 14.0), valueAttrs);
        }
        for (uint32_t slot = 0u;
             slot < s3g::kProcessorStackScoreLocksPerPlayer; ++slot) {
            const drawing::Rect lock = stackScoreLockRect(player, slot, 0u);
            drawing::String  lockTitle = drawing::stringFormat("L%u", slot + 1u);
            drawing::drawCenteredTextToFit(lockTitle,
                drawing::makeRect(lock.origin.x, kScoreGridTop + 17.0,
                    lock.size.width, 14.0), valueAttrs);
        }
    }

    const int32_t playingSection = _presentedScoreSection;
    const int32_t playingRow = _presentedScoreRow;
    for (uint32_t row = 0u;
         row < s3g::kProcessorStackScoreRowsPerSection; ++row) {
        const double y = kScoreGridTop + kScoreGridHeaderHeight
            + static_cast<double>(row) * kScoreGridRowHeight;
        if ((row % 4u) == 0u) {
            drawing::setFill(drawing::withAlpha(style.strip, 0.82));
            drawing::rectFill(drawing::makeRect(grid.origin.x, y,
                grid.size.width, kScoreGridRowHeight));
        }
        if (playingSection == static_cast<int32_t>(_scoreEditSection)
            && playingRow == static_cast<int32_t>(row)) {
            drawing::setFill(scoreTeal);
            drawing::rectFill(drawing::makeRect(grid.origin.x, y,
                grid.size.width, kScoreGridRowHeight));
        }
        drawing::String  rowText = drawing::stringFormat("%02u", row + 1u);
        drawing::drawCenteredTextToFit(rowText,
            drawing::makeRect(grid.origin.x + 2.0, y + 4.0,
                kScoreRowNumberWidth - 4.0, 14.0),
            row == _scoreSelectedRow ? labelAttrs : valueAttrs);
        for (uint32_t player = 0u;
             player < s3g::kProcessorStackScorePlayerCount; ++player) {
            for (uint32_t string = 0u;
                 string < s3g::kProcessorStackScoreStringCount; ++string) {
                const drawing::Rect cell = stackScoreCellRect(player, string, row);
                const bool selected = player == _scoreSelectedPlayer
                    && string == _scoreSelectedString
                    && row == _scoreSelectedRow;
                if (selected) {
                    drawing::setFill(scoreEntryFill);
                    drawing::rectFill(drawing::inset(cell, 1.0, 1.0));
                }
                drawing::setStroke(style.grid);
                drawing::frameRect(cell);
                if (selected) {
                    drawing::setStroke(drawing::withAlpha(style.accent, 0.68));
                    drawing::frameRect(drawing::inset(cell, 1.0, 1.0));
                }
                const size_t index = s3g::processorStackScoreCellIndex(
                    _scoreEditSection, row, player, string);
                const int fret = instance->scoreCells[index].load(
                    std::memory_order_acquire);
                drawing::String  text = fret == s3g::kProcessorStackScoreHold
                    ? "~" : fret >= 0
                        ? drawing::stringFormat("%d", fret) : "—";
                drawing::Attrs  attrs = player == 1u
                        && scoreBRelatesToA
                    ? valueAttrs : (fret != s3g::kProcessorStackScoreRest
                        ? labelAttrs : valueAttrs);
                drawing::drawCenteredTextToFit(text,
                    drawing::makeRect(cell.origin.x, cell.origin.y + 4.0,
                        cell.size.width, 14.0), attrs);
            }
            for (uint32_t slot = 0u;
                 slot < s3g::kProcessorStackScoreLocksPerPlayer; ++slot) {
                const drawing::Rect cell = stackScoreLockRect(player, slot, row);
                const auto lock = stackScoreLockCell(*instance,
                    _scoreEditSection, row, player, slot);
                const auto control = static_cast<
                    s3g::ProcessorStackScoreLockControl>(std::min<uint32_t>(
                        lock.control,
                        s3g::kProcessorStackScoreLockControlCount - 1u));
                if (control != s3g::ProcessorStackScoreLockControl::None) {
                    drawing::setFill(scoreLockFill);
                    drawing::rectFill(drawing::inset(cell, 1.0, 1.0));
                }
                drawing::setStroke(style.grid);
                drawing::frameRect(cell);
                drawing::String  text = "---";
                if (control != s3g::ProcessorStackScoreLockControl::None) {
                    const double normalized =
                        s3g::processorStackScoreLockNormalized(lock);
                    const bool material = control
                            == s3g::ProcessorStackScoreLockControl::Neck
                        || control
                            == s3g::ProcessorStackScoreLockControl::Body;
                    const bool circuit = control
                        == s3g::ProcessorStackScoreLockControl::Circuit;
                    const bool speaker = control
                        == s3g::ProcessorStackScoreLockControl::Speaker;
                    const int displayValue = static_cast<int>(std::lround(
                        normalized * (circuit ? 7.0
                            : material ? 3.0 : speaker ? 6.0 : 99.0)));
                    text = drawing::stringFormat("%s%02d",
                        s3g::processorStackScoreLockControlShortName(control),
                        displayValue);
                }
                drawing::drawCenteredTextToFit(text,
                    drawing::makeRect(cell.origin.x + 2.0, cell.origin.y + 4.0,
                        cell.size.width - 4.0, 14.0),
                    control == s3g::ProcessorStackScoreLockControl::None
                        ? valueAttrs : labelAttrs);
            }
        }
    }

    const drawing::Rect arrangement = stackScoreArrangementRect();
    drawing::drawPanelFrame(arrangement.origin.x,
        arrangement.origin.y, arrangement.size.width,
        arrangement.size.height, style);
    drawing::drawPanelHeader(
        "ARRANGEMENT · CLICK SLOT: SECTION · RIGHT-CLICK L1/L2: CONTROL · DRAG: VALUE",
        true, arrangement.origin.x, arrangement.origin.y,
        arrangement.size.width, 21.0, labelAttrs, style);
    const int32_t playingSlot = _presentedScoreSlot;
    for (uint32_t slot = 0u;
         slot < s3g::kProcessorStackScoreArrangementSlots; ++slot) {
        const drawing::Rect rect = stackScoreArrangementSlotRect(slot);
        const bool enabled = slot < scoreLength;
        const bool playing = playingSlot == static_cast<int32_t>(slot);
        drawing::setFill((playing ? scoreTealStrong : enabled ? style.cell : style.bg));
        drawing::rectFill(rect);
        drawing::setStroke(style.grid);
        drawing::frameRect(rect);
        const uint32_t section = std::min<uint32_t>(
            instance->scoreArrangement[slot].load(
                std::memory_order_acquire),
            s3g::kProcessorStackScoreSectionCount - 1u);
        drawing::String  text = drawing::stringFormat("%u · %c",
            slot + 1u, static_cast<char>('A' + section));
        drawing::drawCenteredTextToFit(text,
            drawing::makeRect(rect.origin.x, rect.origin.y + 3.0,
                rect.size.width, 14.0),
            playing ? labelAttrs : valueAttrs);
    }

    const drawing::Rect legend = stackScoreEntryLegendRect();
    drawing::setFill(style.strip);
    drawing::rectFill(legend);
    drawing::setStroke(style.grid);
    drawing::frameRect(legend);
    drawing::drawCenteredTextToFit(
        "NOTE ENTRY · 0–24 FRET · H / ~ HOLD · - / . / DELETE REST · R REATTACK ABOVE · [ / ] FRET - / + · RETURN NEXT ROW · TAB PLAYER",
        drawing::inset(legend, 8.0, 3.0), labelAttrs);

}

void service() override {
    
    auto* instance = static_cast<Plugin*>(_plugin);
    bool parameterChanged = false;
    if (instance) {
        const uint64_t revision = instance->parameterRevision.load(
            std::memory_order_acquire);
        if (revision != _observedParamRevision) {
            _observedParamRevision = revision;
            parameterChanged = true;
            _factoryPresetIndex = s3g::processorStackFactoryPresetIndex(
                publishedParamsSnapshot(*instance));
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                _factoryPresetIndex >= 0
                    ? s3g::processorStackFactoryPresetInfo(
                        static_cast<uint32_t>(_factoryPresetIndex)).name
                    : "CUSTOM");
        }
    }
    if (!instance) return;
    if (_scoreRandomFeedback >= 0) {
        const uint32_t action = static_cast<uint32_t>(_scoreRandomFeedback);
        const bool expired = drawing::uptime()
            >= _scoreRandomFeedbackUntil;
        if (_page == 3u) {
            setNeedsDisplayInRect(drawing::inset(
                stackScoreRandomActionButtonRect(action), -1.0, -1.0));
        }
        if (expired) _scoreRandomFeedback = -1;
    }
    if (parameterChanged || _page != 3u) {
        setNeedsDisplay(true);
        return;
    }

    int32_t capturedSection = -1;
    int32_t capturedRow = -1;
    int32_t capturedSlot = -1;
    for (uint32_t attempt = 0u; attempt < 4u; ++attempt) {
        const uint64_t before = instance->scoreVisualRevision.load(
            std::memory_order_acquire);
        if ((before & 1u) != 0u) continue;
        const int32_t section = instance->scoreVisualSection.load(
            std::memory_order_relaxed);
        const int32_t row = instance->scoreVisualRow.load(
            std::memory_order_relaxed);
        const int32_t slot = instance->scoreVisualSlot.load(
            std::memory_order_relaxed);
        const uint64_t after = instance->scoreVisualRevision.load(
            std::memory_order_acquire);
        if (before != after || (after & 1u) != 0u) continue;
        _observedScoreVisualRevision = after;
        capturedSection = section;
        capturedRow = row;
        capturedSlot = slot;
        break;
    }

    const int32_t nextSection = _scoreVisualPrimed
        ? _pendingScoreSection : _presentedScoreSection;
    const int32_t nextRow = _scoreVisualPrimed
        ? _pendingScoreRow : _presentedScoreRow;
    const int32_t nextSlot = _scoreVisualPrimed
        ? _pendingScoreSlot : _presentedScoreSlot;
    _pendingScoreSection = capturedSection;
    _pendingScoreRow = capturedRow;
    _pendingScoreSlot = capturedSlot;
    _scoreVisualPrimed = true;

    if (nextSection != _presentedScoreSection
        || nextRow != _presentedScoreRow) {
        if (_presentedScoreSection == static_cast<int32_t>(_scoreEditSection)
            && _presentedScoreRow >= 0) {
            setNeedsDisplayInRect(drawing::inset(stackScoreRowRect(
                static_cast<uint32_t>(_presentedScoreRow)), -1.0, -1.0));
        }
        if (nextSection == static_cast<int32_t>(_scoreEditSection)
            && nextRow >= 0) {
            setNeedsDisplayInRect(drawing::inset(stackScoreRowRect(
                static_cast<uint32_t>(nextRow)), -1.0, -1.0));
        }
        _presentedScoreSection = nextSection;
        _presentedScoreRow = nextRow;
    }
    if (nextSlot != _presentedScoreSlot) {
        if (_presentedScoreSlot >= 0) {
            setNeedsDisplayInRect(drawing::inset(
                stackScoreArrangementSlotRect(
                    static_cast<uint32_t>(_presentedScoreSlot)),
                -1.0, -1.0));
        }
        if (nextSlot >= 0) {
            setNeedsDisplayInRect(drawing::inset(
                stackScoreArrangementSlotRect(
                    static_cast<uint32_t>(nextSlot)), -1.0, -1.0));
        }
        _presentedScoreSlot = nextSlot;
    }
    setNeedsDisplayInRect(drawing::makeRect(0.0, 0.0, kGuiWidth, 34.0));

}

void drawOriginal(drawing::Rect dirty)  {
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return;
    drawing::Style style;
    drawing::setFill(style.bg);
    drawing::rectFill(dirty);
    drawing::Attrs  titleAttrs = drawing::softTitleAttrs();
    drawing::Attrs  labelAttrs = drawing::softLabelAttrs();
    drawing::Attrs  valueAttrs = drawing::softValueAttrs();
    const auto titleBand = drawing::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    drawing::drawEncoderTitleBand(
        "s3g PROCESSOR STACK",
        drawing::stringFromUtf8(_presetName),
        drawing::peakDbText(
            instance->outputPeak.load(std::memory_order_relaxed)),
        titleBand, titleAttrs, labelAttrs, valueAttrs, style);

    const auto drawPanel = [&](drawing::String  name, const layout::Panel& panel) {
        const drawing::Rect rect = drawing::cocoaRect(panel.frame);
        drawing::drawPanelFrame(rect.origin.x, rect.origin.y,
            rect.size.width, rect.size.height, style);
        drawing::drawPanelHeader(name, true, rect.origin.x,
            rect.origin.y, rect.size.width,
            s3g::gui_layout::kStandardMetrics.headerHeight,
            labelAttrs, style);
    };
    for (const auto& panel : kUiPanels) {
        if (panel.page != _page && panel.page != kAllPages) continue;
        drawPanel(drawing::stringFromUtf8(panel.name), panel.panel);
    }

    static drawing::String  const pageLabels[4] {
        "PLAY", "RIG A", "RIG B", "SCORE",
    };
    const drawing::Rect outputHeader = drawing::cocoaRect(
        kOutputPanel.frame);
    for (uint8_t page = 0u; page < 4u; ++page) {
        drawing::drawToolboxHeaderButton(stackPageButtonRect(page),
            outputHeader, pageLabels[page], page == _page,
            valueAttrs, style);
    }
    if (_page == 2u) {
        const drawing::Rect linksHeader = drawing::cocoaRect(
            kLinksPanel.frame);
        drawing::drawToolboxHeaderActionButton(stackCopyButtonRect(0u),
            linksHeader, "COPY A > B", valueAttrs, style);
        drawing::drawToolboxHeaderActionButton(stackCopyButtonRect(1u),
            linksHeader, "COPY B > A", valueAttrs, style);
    }
    if (_page == 3u) {
        const drawing::Rect timingHeader = drawing::cocoaRect(
            kScoreTimingPanel.frame);
        static drawing::String  const labels[kStackScoreRandomActionCount] {
            "FORM", "LEAD", "RIFF", "LOCKS", "THIN",
        };
        const double now = drawing::uptime();
        for (uint32_t action = 0u;
             action < kStackScoreRandomActionCount; ++action) {
            const drawing::Rect button = stackScoreRandomActionButtonRect(action);
            const bool pressed = _scoreRandomFeedback
                    == static_cast<int>(action)
                && now < _scoreRandomFeedbackUntil;
            if (!pressed) {
                drawing::drawToolboxHeaderActionButton(button,
                    timingHeader, labels[action], valueAttrs, style);
                continue;
            }
            drawing::setFill(s3g::portable_gui::canvas::rgba(0.025, 0.15, 0.16, 0.98));
            drawing::rectFill(button);
            drawing::setStroke(style.accent);
            drawing::frameRect(button);
            drawing::drawCenteredTextToFit(labels[action],
                drawing::inset(button, 2.0, 1.0), labelAttrs);
        }
    }

    for (const auto& row : kUiRows) {
        if (row.page != _page && row.page != kAllPages) continue;
        const double value = paramValue(*instance, row.id);
        char text[64] {};
        paramsValueToText(&instance->plugin,
            row.id, value, text, sizeof(text));
        drawing::String  label = drawing::stringFromUtf8(row.label);
        drawing::String  display = drawing::stringFromUtf8(text);
        if (isUiToggleParam(row.id)) {
            drawing::drawToggle(label, value >= 0.5, row.y,
                labelAttrs, valueAttrs, style,
                s3g::gui_layout::processorLabelX(row.panelX),
                s3g::gui_layout::processorControlX(row.panelX), 64.0);
        } else if (isUiMenuParam(row.id)) {
            drawing::drawProcessorMenu(label, display, row.y,
                row.panelX, row.panelWidth,
                labelAttrs, valueAttrs, style);
        } else {
            drawing::drawProcessorSlider(label, display,
                static_cast<double>(uiNormalizedValue(row.id, value)),
                row.y, row.panelX, row.panelWidth,
                labelAttrs, valueAttrs, style);
        }
    }
    if (_page == 0u) {
        drawStackPatternMultislider(0u, *instance,
            _dragPatternPlayer, _dragPatternStep, valueAttrs, style);
        drawStackPatternMultislider(1u, *instance,
            _dragPatternPlayer, _dragPatternStep, valueAttrs, style);
    }
    if (_page == 3u) {
        drawScore_values_style(labelAttrs, valueAttrs, style);
    }
    drawOpenMenu_style(valueAttrs, style);

}

void updateDraggedParam(drawing::Point point)  {
    if (_dragParam <= 0) return;
    const clap_id id = static_cast<clap_id>(_dragParam);
    const auto row = std::find_if(kUiRows.begin(), kUiRows.end(),
        [=](const StackUiRow& candidate) {
            return candidate.id == id
                && (candidate.page == _page
                    || candidate.page == kAllPages);
        });
    if (row == kUiRows.end()) return;
    const double controlX = s3g::gui_layout::processorControlX(row->panelX);
    const double trackWidth =
        s3g::gui_layout::processorTrackWidth(row->panelWidth);
    const double normalized = std::clamp(
        (point.x - controlX) / trackWidth, 0.0, 1.0);
    auto* instance = static_cast<Plugin*>(_plugin);
    queueGuiParamValue(*instance, id,
        uiValueFromNormalized(id, normalized));
    setNeedsDisplay(true);

}

void updateDraggedPattern(drawing::Point point)  {
    if (_dragPatternPlayer < 0 || _dragPatternPlayer > 1) return;
    const uint8_t player = static_cast<uint8_t>(_dragPatternPlayer);
    const int nextStep = stackPatternStepAtX(player, point.x);
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return;
    if (nextStep >= 0 && nextStep != _dragPatternStep) {
        if (_dragPatternStep >= 0) {
            queueGuiParamGestureEnd(*instance, stackPatternStepParam(
                player, static_cast<uint32_t>(_dragPatternStep)));
        }
        _dragPatternStep = nextStep;
        queueGuiParamGestureBegin(*instance, stackPatternStepParam(
            player, static_cast<uint32_t>(_dragPatternStep)));
    }
    if (_dragPatternStep < 0) return;
    const drawing::Rect field = stackPatternFieldRect(player);
    const double normalized = std::clamp(
        (drawing::maxY(field) - point.y) / field.size.height, 0.0, 1.0);
    const clap_id id = stackPatternStepParam(
        player, static_cast<uint32_t>(_dragPatternStep));
    queueGuiParamValue(*instance, id,
        uiValueFromNormalized(id, normalized));
    markCustomPreset();
    setNeedsDisplay(true);

}

void rightMouseDown(drawing::Event*  event)  {
    if (_page == 0u) {
        const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
        auto* instance = static_cast<Plugin*>(_plugin);
        if (!instance) return;
        for (uint8_t player = 0u; player < 2u; ++player) {
            if (!drawing::contains(point, stackPatternFieldRect(player))) {
                continue;
            }
            const int step = stackPatternStepAtX(player, point.x);
            if (step < 0) return;
            const clap_id id = stackPatternStepParam(
                player, static_cast<uint32_t>(step));
            queueGuiParamGesture(*instance, id,
                static_cast<double>(s3g::kProcessorStackArpRest));
            _dragPatternPlayer = -1;
            _dragPatternStep = -1;
            markCustomPreset();
            setNeedsDisplay(true);
            return;
        }
        (void)0;
        return;
    }
    if (_page != 3u) {
        (void)0;
        return;
    }
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    uint32_t player = 0u;
    uint32_t slot = 0u;
    uint32_t row = 0u;
    if (!stackScoreLockAtPoint(point, player, slot, row)) {
        (void)0;
        return;
    }
    _scoreSelectedPlayer = player;
    _scoreSelectedRow = row;
    _scoreLockPlayer = player;
    _scoreLockSlot = slot;
    _scoreLockRow = row;
    _scorePendingFret = -1;
    focus();
    openScoreLockMenu(player, row, slot, point);

}

void mouseDown(drawing::Event*  event)  {
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return;
    const auto titleBand = drawing::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    if (_openMenu != CLAP_INVALID_ID) {
        const int hit = drawing::dropdownHitIndex(point,
            openMenuRect(), 18.0, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == kFactoryPresetMenuId) {
                applyFactoryPreset(hit);
            } else {
                const clap_id menuParam = _openMenu;
                const auto* def = paramDef(menuParam);
                queueGuiParamGesture(*instance, menuParam,
                    (def ? def->minimum : 0.0) + static_cast<double>(hit));
                if (menuParam != kMidiReceiveParamId
                    && menuParam != kArpHostSyncParamId
                    && menuParam != kScoreEnableParamId
                    && menuParam != kScoreRateParamId
                    && menuParam != kScoreLengthParamId
                    && menuParam != kScoreBSourceParamId) {
                    markCustomPreset();
                }
            }
        }
        _openMenu = CLAP_INVALID_ID;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point,
            drawing::cocoaRect(titleBand.presetMenu))) {
        _openMenu = kFactoryPresetMenuId;
        _hoverMenuItem = -1;
        _menuItemCount = s3g::kProcessorStackFactoryPresetCount;
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point,
            drawing::cocoaRect(titleBand.loadButton))) {
        drawing::String name;
        if (presetDialog(false, &name)) {
            markCustomPreset();
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                !name.empty() ? name.c_str() : "CUSTOM");
        } else {
            beep();
        }
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point,
            drawing::cocoaRect(titleBand.saveButton))) {
        drawing::String name;
        if (presetDialog(true, &name)) {
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                !name.empty() ? name.c_str() : "CUSTOM");
        } else {
            beep();
        }
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point,
            drawing::cocoaRect(titleBand.randomButton))) {
        if (queueGuiParams(*instance,
                safeRandomParams(*instance, drawing::randomBits()))) {
            markCustomPreset();
        } else {
            beep();
        }
        setNeedsDisplay(true);
        return;
    }

    for (uint8_t page = 0u; page < 4u; ++page) {
        if (!drawing::contains(point, stackPageButtonRect(page))) continue;
        if (page == 3u && _page != 3u) {
            _scoreVisualPrimed = false;
            _pendingScoreSection = _pendingScoreRow = _pendingScoreSlot = -1;
            _presentedScoreSection = _presentedScoreRow
                = _presentedScoreSlot = -1;
        }
        _page = page;
        _dragParam = -1;
        _dragPatternPlayer = -1;
        _dragPatternStep = -1;
        setNeedsDisplay(true);
        return;
    }
    if (_page == 2u) {
        for (uint8_t copy = 0u; copy < 2u; ++copy) {
            if (!drawing::contains(point, stackCopyButtonRect(copy))) continue;
            if (queueRigCopy(*instance, copy == 0u)) {
                markCustomPreset();
            } else {
                beep();
            }
            setNeedsDisplay(true);
            return;
        }
    }

    if (_page == 3u) {
        for (uint32_t action = 0u;
             action < kStackScoreRandomActionCount; ++action) {
            if (!drawing::contains(point,
                    stackScoreRandomActionButtonRect(action))) continue;
            const auto current = scoreProgramSnapshot(*instance);
            const auto scoreParams = publishedParamsSnapshot(*instance);
            s3g::ProcessorStackScoreProgram generated = current;
            const uint32_t seed = drawing::randomBits();
            switch (action) {
            case 0u:
                generated = s3g::generateProcessorStackScore(
                    seed, scoreParams.scale, scoreParams.scaleB);
                // FORM changes the complete tablature and arrangement but
                // keeps detailed control sequencing authored in the lanes.
                generated.locks = current.locks;
                _scoreEditSection = 0u;
                break;
            case 1u:
                generated = s3g::randomizeProcessorStackScoreLead(
                    current, _scoreEditSection, seed,
                    scoreParams.scale, scoreParams.scaleB);
                break;
            case 2u:
                generated = s3g::randomizeProcessorStackScoreRiff(
                    current, _scoreEditSection, seed,
                    scoreParams.scale, scoreParams.scaleB);
                break;
            case 3u:
                // LOCKS changes only the two per-player lane values in the
                // selected section. Notes, form, and base parameters survive.
                generated = s3g::randomizeProcessorStackScoreLocks(
                    current, _scoreEditSection, seed);
                break;
            case 4u:
                // THIN removes attack events across every section and player.
                // Arrangement slots and the two parameter-lock lanes survive.
                generated = s3g::randomizeProcessorStackScoreThin(
                    current, seed);
                break;
            default:
                break;
            }
            storeScoreProgram(*instance, generated, true);
            _scoreSelectedPlayer = 0u;
            _scoreSelectedString = 0u;
            _scoreSelectedRow = 0u;
            _scorePendingFret = -1;
            _scoreRandomFeedback = static_cast<int>(action);
            _scoreRandomFeedbackUntil =
                drawing::uptime() + 0.22;
            focus();
            setNeedsDisplay(true);
            return;
        }
        for (uint32_t section = 0u;
             section < s3g::kProcessorStackScoreSectionCount; ++section) {
            if (!drawing::contains(point,
                    stackScoreSectionButtonRect(section))) continue;
            _scoreEditSection = section;
            _scorePendingFret = -1;
            focus();
            setNeedsDisplay(true);
            return;
        }
        for (uint32_t slot = 0u;
             slot < s3g::kProcessorStackScoreArrangementSlots; ++slot) {
            if (!drawing::contains(point,
                    stackScoreArrangementSlotRect(slot))) continue;
            const uint32_t current = std::min<uint32_t>(
                instance->scoreArrangement[slot].load(
                    std::memory_order_acquire),
                s3g::kProcessorStackScoreSectionCount - 1u);
            const bool backwards = (pointerModifiers
                & 1u) != 0u;
            const uint32_t next = backwards
                ? (current + s3g::kProcessorStackScoreSectionCount - 1u)
                    % s3g::kProcessorStackScoreSectionCount
                : (current + 1u) % s3g::kProcessorStackScoreSectionCount;
            instance->scoreArrangement[slot].store(
                next, std::memory_order_release);
            instance->scoreRevision.fetch_add(
                1u, std::memory_order_acq_rel);
            notifyScoreChanged(*instance);
            setNeedsDisplay(true);
            return;
        }
        uint32_t lockPlayer = 0u;
        uint32_t lockSlot = 0u;
        uint32_t lockRow = 0u;
        if (stackScoreLockAtPoint(
                point, lockPlayer, lockSlot, lockRow)) {
            _scoreSelectedPlayer = lockPlayer;
            _scoreSelectedRow = lockRow;
            _scoreLockPlayer = lockPlayer;
            _scoreLockSlot = lockSlot;
            _scoreLockRow = lockRow;
            _scorePendingFret = -1;
            const auto lock = stackScoreLockCell(*instance,
                _scoreEditSection, lockRow, lockPlayer, lockSlot);
            if (event->clickCount() >= 2) {
                setScoreLock(*instance, _scoreEditSection, lockRow,
                    lockPlayer, lockSlot,
                    s3g::ProcessorStackScoreLockControl::None, 0.0);
                _dragScoreLock = false;
            } else if (lock.control != static_cast<uint8_t>(
                    s3g::ProcessorStackScoreLockControl::None)) {
                _dragScoreLock = true;
                _scoreLockDragStartPoint = point;
                _scoreLockDragStartNormalized =
                    s3g::processorStackScoreLockNormalized(lock);
            }
            focus();
            setNeedsDisplayInRect(drawing::inset(stackScoreLockRect(
                lockPlayer, lockSlot, lockRow), -1.0, -1.0));
            return;
        }
        uint32_t player = 0u;
        uint32_t string = 0u;
        uint32_t row = 0u;
        if (stackScoreCellAtPoint(point, player, string, row)) {
            _scoreSelectedPlayer = player;
            _scoreSelectedString = string;
            _scoreSelectedRow = row;
            _scorePendingFret = -1;
            if (event->clickCount() >= 2) {
                setScoreCell(*instance, _scoreEditSection, row,
                    player, string, s3g::kProcessorStackScoreRest);
            }
            focus();
            setNeedsDisplay(true);
            return;
        }
    }

    if (_page == 0u) {
        for (uint8_t player = 0u; player < 2u; ++player) {
            if (!drawing::contains(point, stackPatternFieldRect(player))) {
                continue;
            }
            const int step = stackPatternStepAtX(player, point.x);
            if (step < 0) return;
            const clap_id id = stackPatternStepParam(
                player, static_cast<uint32_t>(step));
            double defaultValue = 0.0;
            if (drawing::sliderDoubleClickDefault(
                    event, &instance->plugin, id, &defaultValue)) {
                queueGuiParamGesture(*instance, id, defaultValue);
                _dragPatternPlayer = -1;
                _dragPatternStep = -1;
            } else {
                _dragPatternPlayer = static_cast<int>(player);
                _dragPatternStep = step;
                queueGuiParamGestureBegin(*instance, id);
                updateDraggedPattern(point);
            }
            markCustomPreset();
            setNeedsDisplay(true);
            return;
        }
    }

    for (const auto& row : kUiRows) {
        if (row.page != _page && row.page != kAllPages) continue;
        const drawing::Rect hit = drawing::makeRect(
            row.panelX + s3g::gui_layout::kStandardMetrics.hitInset,
            row.y - 9.0,
            row.panelWidth
                - s3g::gui_layout::kStandardMetrics.hitInset * 2.0,
            s3g::gui_layout::kStandardMetrics.hitHeight);
        if (!drawing::contains(point, hit)) continue;
        if (isUiToggleParam(row.id)) {
            const double next = paramValue(*instance, row.id) >= 0.5
                ? 0.0 : 1.0;
            queueGuiParamGesture(*instance, row.id, next);
            markCustomPreset();
            setNeedsDisplay(true);
            return;
        }
        if (isUiMenuParam(row.id)) {
            _openMenu = row.id;
            _hoverMenuItem = -1;
            _menuItemCount = uiMenuItemCount(row.id);
            setNeedsDisplay(true);
            return;
        }
        double defaultValue = 0.0;
        if (drawing::sliderDoubleClickDefault(
                event, &instance->plugin, row.id, &defaultValue)) {
            queueGuiParamGesture(*instance, row.id, defaultValue);
            _dragParam = -1;
        } else {
            _dragParam = static_cast<int>(row.id);
            queueGuiParamGestureBegin(*instance, row.id);
            updateDraggedParam(point);
        }
        if (row.id != kOutputParamId && row.id != kMidiReceiveParamId
            && row.id != kArpHostSyncParamId
            && row.id != kScoreEnableParamId
            && row.id != kScoreRateParamId
            && row.id != kScoreGateParamId
            && row.id != kScoreLengthParamId
            && row.id != kScoreBSourceParamId) {
            markCustomPreset();
        }
        return;
    }

}

void mouseDragged(drawing::Event*  event)  {
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    if (_dragScoreLock) {
        updateDraggedScoreLock(point);
    } else if (_dragPatternPlayer >= 0) {
        updateDraggedPattern(point);
    } else if (_dragParam > 0) {
        updateDraggedParam(point);
    }

}

void mouseUp(drawing::Event*  event)  {
    (void)event;
    if (_dragParam > 0) {
        queueGuiParamGestureEnd(*static_cast<Plugin*>(_plugin),
            static_cast<clap_id>(_dragParam));
    }
    if (_dragPatternPlayer >= 0 && _dragPatternStep >= 0) {
        queueGuiParamGestureEnd(*static_cast<Plugin*>(_plugin),
            stackPatternStepParam(static_cast<uint8_t>(_dragPatternPlayer),
                static_cast<uint32_t>(_dragPatternStep)));
    }
    _dragParam = -1;
    _dragPatternPlayer = -1;
    _dragPatternStep = -1;
    _dragScoreLock = false;

}

void mouseMoved(drawing::Event*  event)  {
    if (_openMenu == CLAP_INVALID_ID) return;
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    const int hover = drawing::dropdownHitIndex(
        point, openMenuRect(), 18.0, _menuItemCount);
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        setNeedsDisplay(true);
    }

}

};
}
s3g::portable_gui::foundation::ContentView* makePortableCanvas(Plugin& p) { return new stereo_processor_canvas::Editor(p); }
#include "s3g_clap_canvas_gui.inc"
