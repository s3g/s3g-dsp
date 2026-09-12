// Literal Cocoa adaptation: preserve layout, drawing order and hit maps.
namespace stereo_processor_canvas {
namespace drawing = s3g::portable_gui::stereo_drawing;
namespace foundation = s3g::portable_gui::foundation;

constexpr clap_id kFactoryPresetMenuId = 0x7ffffff0u;

enum class GuiPage : uint8_t { Build = 0u, Motion };
enum class ArpEditLane : uint8_t { Pitch = 0u, Accent, Gate, Octave };

namespace layout = s3g::gui_layout;

struct UiRow {
    clap_id id;
    const char* label;
    GuiPage page;
    double panelX;
    double panelWidth;
    double y;
};

struct UiPanel {
    const char* title;
    GuiPage page;
    layout::Panel panel;
};

constexpr double kColumnWidth = 348.0;
constexpr layout::Canvas kCanvas {
    static_cast<double>(kGuiWidth), static_cast<double>(kGuiHeight)
};
constexpr layout::Column kColumnOne { 16.0, kColumnWidth, 75.0 };
constexpr layout::Column kColumnTwo { 376.0, kColumnWidth, 75.0 };
constexpr layout::Column kColumnThree { 736.0, kColumnWidth, 75.0 };

constexpr layout::Panel kWorkspacePanel {
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Utility,
    { 16.0, 42.0, 1068.0, 21.0 }, 36.0, 26.0, 0u
};

constexpr auto kOutputPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Output,
    kColumnOne, 75.0, 4u);
constexpr auto kFoundationPanel = layout::fittedStackPanel(
    layout::PanelRole::Engine, kOutputPanel, 8u);
constexpr auto kEnvelopePanel = layout::fittedStackPanel(
    layout::PanelRole::Envelope, kFoundationPanel, 6u);
constexpr auto kBodyPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Source,
    kColumnTwo, 75.0, 9u);
constexpr auto kTexturePanel = layout::fittedStackPanel(
    layout::PanelRole::Relationships, kBodyPanel, 5u);
constexpr auto kShapePanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::ToneShape,
    kColumnThree, 75.0, 8u);

constexpr auto kClockPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Motion,
    kColumnOne, 75.0, 1u);
constexpr auto kPerformancePanel = layout::fittedStackPanel(
    layout::PanelRole::Relationships, kClockPanel, 1u);
constexpr auto kExpressionPanel = layout::fittedStackPanel(
    layout::PanelRole::Relationships, kPerformancePanel, 6u);
constexpr auto kAuxRoutesPanel = layout::fittedStackPanel(
    layout::PanelRole::Modulation, kExpressionPanel, 6u);
constexpr auto kLayerEnvelopesPanel = layout::fittedStackPanel(
    layout::PanelRole::Envelope, kAuxRoutesPanel, 3u);
constexpr auto kModOnePanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Modulation,
    kColumnTwo, 75.0, 4u);
constexpr auto kModTwoPanel = layout::fittedStackPanel(
    layout::PanelRole::Modulation, kModOnePanel, 4u);
constexpr auto kModThreePanel = layout::fittedStackPanel(
    layout::PanelRole::Modulation, kModTwoPanel, 4u);
constexpr auto kArpeggiatorPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::EventTiming,
    kColumnThree, 75.0, 7u);
constexpr auto kArpPatternPanel = layout::stackPanel(
    layout::PanelRole::EventTiming, kArpeggiatorPanel, 132.0, 0u);

constexpr auto kAmpPanel = layout::fittedStackPanel(
    layout::PanelRole::ToneShape, kShapePanel, 1u);
constexpr auto kShredPanel = layout::fittedStackPanel(
    layout::PanelRole::Projection, kAmpPanel, 5u);
constexpr auto kDynamicsPanel = layout::fittedStackPanel(
    layout::PanelRole::Topology, kTexturePanel, 6u);
constexpr std::array kBuildFirstColumnPanels {
    kOutputPanel,
    kFoundationPanel,
    kEnvelopePanel,
};
constexpr std::array kBuildSecondColumnPanels {
    kBodyPanel,
    kTexturePanel,
    kDynamicsPanel,
};
constexpr std::array kBuildThirdColumnPanels {
    kShapePanel,
    kAmpPanel,
    kShredPanel,
};
constexpr std::array kMotionSecondColumnPanels {
    kModOnePanel,
    kModTwoPanel,
    kModThreePanel,
};
constexpr std::array kMotionFirstColumnPanels {
    kClockPanel,
    kPerformancePanel,
    kExpressionPanel,
    kAuxRoutesPanel,
    kLayerEnvelopesPanel,
};
constexpr std::array kMotionThirdColumnPanels {
    kArpeggiatorPanel,
    kArpPatternPanel,
};
constexpr std::array<UiPanel, 19u> kUiPanels {{
    { "OUTPUT + ROUTING", GuiPage::Build, kOutputPanel },
    { "FOUNDATION", GuiPage::Build, kFoundationPanel },
    { "ENVELOPE", GuiPage::Build, kEnvelopePanel },
    { "BODY ENGINE", GuiPage::Build, kBodyPanel },
    { "TEXTURE", GuiPage::Build, kTexturePanel },
    { "SHAPE", GuiPage::Build, kShapePanel },
    { "CLOCK", GuiPage::Motion, kClockPanel },
    { "PERFORMANCE", GuiPage::Motion, kPerformancePanel },
    { "EXPRESSION ROUTING", GuiPage::Motion, kExpressionPanel },
    { "SECONDARY MOD ROUTES", GuiPage::Motion, kAuxRoutesPanel },
    { "LAYER ENVELOPES", GuiPage::Motion, kLayerEnvelopesPanel },
    { "MODULATOR 1", GuiPage::Motion, kModOnePanel },
    { "MODULATOR 2", GuiPage::Motion, kModTwoPanel },
    { "MODULATOR 3", GuiPage::Motion, kModThreePanel },
    { "ARPEGGIATOR", GuiPage::Motion, kArpeggiatorPanel },
    { "ARP STEP EDITOR", GuiPage::Motion, kArpPatternPanel },
    { "AMPLIFIER", GuiPage::Build, kAmpPanel },
    { "STEREO SHRED", GuiPage::Build, kShredPanel },
    { "DYNAMICS + CEILING", GuiPage::Build, kDynamicsPanel },
}};

constexpr std::array<UiRow, kParamCount - 32u> kUiRows {{
    { kVoiceModeParamId, "VOICE MODE", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kFoundationPanel, 0u) },
    { kGlideParamId, "GLIDE", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kFoundationPanel, 1u) },
    { kFoundationWaveParamId, "WAVE", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kFoundationPanel, 2u) },
    { kFoundationOctaveParamId, "OCTAVE", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kFoundationPanel, 3u) },
    { kFoundationLevelParamId, "LEVEL", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kFoundationPanel, 4u) },
    { kPitchPunchParamId, "PITCH PUNCH", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kFoundationPanel, 5u) },
    { kPunchTimeParamId, "PUNCH TIME", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kFoundationPanel, 6u) },
    { kFoundationReturnParamId, "LOW RETURN", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kFoundationPanel, 7u) },

    { kAttackParamId, "ATTACK", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kEnvelopePanel, 0u) },
    { kDecayParamId, "DECAY", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kEnvelopePanel, 1u) },
    { kSustainParamId, "SUSTAIN", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kEnvelopePanel, 2u) },
    { kReleaseParamId, "RELEASE", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kEnvelopePanel, 3u) },
    { kFilterEnvelopeParamId, "FILTER ENV", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kEnvelopePanel, 4u) },
    { kFilterDecayParamId, "FILTER DECAY", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kEnvelopePanel, 5u) },

    { kBodyEngineParamId, "ENGINE", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kBodyPanel, 0u) },
    { kBodyLevelParamId, "LEVEL", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kBodyPanel, 1u) },
    { kBodyControlAParamId, "CONTROL A", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kBodyPanel, 2u) },
    { kBodyControlBParamId, "CONTROL B", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kBodyPanel, 3u) },
    { kBodyControlCParamId, "CONTROL C", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kBodyPanel, 4u) },
    { kBodyControlDParamId, "CONTROL D", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kBodyPanel, 5u) },
    { kBodyControlEParamId, "CONTROL E", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kBodyPanel, 6u) },
    { kModalDriveParamId, "MODAL DRIVE", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kBodyPanel, 7u) },
    { kBodyWidthParamId, "WIDTH", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kBodyPanel, 8u) },

    { kTextureModeParamId, "MODE", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kTexturePanel, 0u) },
    { kTextureLevelParamId, "LEVEL", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kTexturePanel, 1u) },
    { kTextureColorParamId, "COLOR", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kTexturePanel, 2u) },
    { kTextureTrackParamId, "PITCH TRACK", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kTexturePanel, 3u) },
    { kTextureWidthParamId, "WIDTH", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kTexturePanel, 4u) },

    { kFilterTypeParamId, "TYPE", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShapePanel, 0u) },
    { kCutoffParamId, "CUTOFF", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShapePanel, 1u) },
    { kResonanceParamId, "RESONANCE", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShapePanel, 2u) },
    { kFilterDriveParamId, "DRIVE", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShapePanel, 3u) },
    { kKeyTrackParamId, "KEY TRACK", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShapePanel, 4u) },
    { kFoundationFilterParamId, "FOUND ROUTE", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShapePanel, 5u) },
    { kBodyFilterParamId, "BODY ROUTE", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShapePanel, 6u) },
    { kTextureFilterParamId, "TEXTURE ROUTE", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShapePanel, 7u) },

    { kMotionClockParamId, "CLOCK", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kClockPanel, 0u) },
    { kModWheelAmountParamId, "MOD WHEEL", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kPerformancePanel, 0u) },

    { kVelocityTargetParamId, "VEL TARGET", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kExpressionPanel, 0u) },
    { kVelocityDepthParamId, "VEL DEPTH", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kExpressionPanel, 1u) },
    { kPressureTargetParamId, "PRESS TARGET", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kExpressionPanel, 2u) },
    { kPressureDepthParamId, "PRESS DEPTH", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kExpressionPanel, 3u) },
    { kTimbreTargetParamId, "TIMBRE TGT", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kExpressionPanel, 4u) },
    { kTimbreDepthParamId, "TIMBRE DEPTH", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kExpressionPanel, 5u) },

    { kMod1SecondaryTargetParamId, "M1 TARGET B", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kAuxRoutesPanel, 0u) },
    { kMod1SecondaryDepthParamId, "M1 DEPTH B", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kAuxRoutesPanel, 1u) },
    { kMod2SecondaryTargetParamId, "M2 TARGET B", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kAuxRoutesPanel, 2u) },
    { kMod2SecondaryDepthParamId, "M2 DEPTH B", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kAuxRoutesPanel, 3u) },
    { kMod3SecondaryTargetParamId, "M3 TARGET B", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kAuxRoutesPanel, 4u) },
    { kMod3SecondaryDepthParamId, "M3 DEPTH B", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kAuxRoutesPanel, 5u) },

    { kBodyDecayParamId, "BODY DECAY", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kLayerEnvelopesPanel, 0u) },
    { kTextureAttackParamId, "TEX ATTACK", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kLayerEnvelopesPanel, 1u) },
    { kTextureDecayParamId, "TEX DECAY", GuiPage::Motion, 16.0,
        kColumnWidth, layout::rowY(kLayerEnvelopesPanel, 2u) },

    { kMod1ShapeParamId, "SHAPE", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModOnePanel, 0u) },
    { kMod1RateParamId, "RATE / DIV", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModOnePanel, 1u) },
    { kMod1DepthParamId, "DEPTH", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModOnePanel, 2u) },
    { kMod1TargetParamId, "TARGET", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModOnePanel, 3u) },
    { kMod2ShapeParamId, "SHAPE", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModTwoPanel, 0u) },
    { kMod2RateParamId, "RATE / DIV", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModTwoPanel, 1u) },
    { kMod2DepthParamId, "DEPTH", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModTwoPanel, 2u) },
    { kMod2TargetParamId, "TARGET", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModTwoPanel, 3u) },
    { kMod3ShapeParamId, "SHAPE", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModThreePanel, 0u) },
    { kMod3RateParamId, "RATE / DIV", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModThreePanel, 1u) },
    { kMod3DepthParamId, "DEPTH", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModThreePanel, 2u) },
    { kMod3TargetParamId, "TARGET", GuiPage::Motion, 376.0,
        kColumnWidth, layout::rowY(kModThreePanel, 3u) },

    { kArpPatternParamId, "PATTERN", GuiPage::Motion, 736.0,
        kColumnWidth, layout::rowY(kArpeggiatorPanel, 0u) },
    { kArpScaleParamId, "SCALE RULE", GuiPage::Motion, 736.0,
        kColumnWidth, layout::rowY(kArpeggiatorPanel, 1u) },
    { kArpSyncParamId, "SYNC", GuiPage::Motion, 736.0,
        kColumnWidth, layout::rowY(kArpeggiatorPanel, 2u) },
    { kArpRateParamId, "RATE", GuiPage::Motion, 736.0,
        kColumnWidth, layout::rowY(kArpeggiatorPanel, 3u) },
    { kArpOctavesParamId, "OCTAVES", GuiPage::Motion, 736.0,
        kColumnWidth, layout::rowY(kArpeggiatorPanel, 4u) },
    { kArpGateParamId, "GATE", GuiPage::Motion, 736.0,
        kColumnWidth, layout::rowY(kArpeggiatorPanel, 5u) },
    { kArpLengthParamId, "LENGTH", GuiPage::Motion, 736.0,
        kColumnWidth, layout::rowY(kArpeggiatorPanel, 6u) },

    { kTubeParamId, "TUBE", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kAmpPanel, 0u) },
    { kShredCircuitParamId, "CIRCUIT", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShredPanel, 0u) },
    { kShredParamId, "SHRED", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShredPanel, 1u) },
    { kShredFeedbackParamId, "FEEDBACK", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShredPanel, 2u) },
    { kShredColorParamId, "COLOR", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShredPanel, 3u) },
    { kShredMixParamId, "MIX", GuiPage::Build, 736.0,
        kColumnWidth, layout::rowY(kShredPanel, 4u) },

    { kDynamicsParamId, "DYNAMICS", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kDynamicsPanel, 0u) },
    { kDynamicsBiteParamId, "BITE", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kDynamicsPanel, 1u) },
    { kSaturationParamId, "SATURATION", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kDynamicsPanel, 2u) },
    { kClipParamId, "CLIP", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kDynamicsPanel, 3u) },
    { kTiltParamId, "TILT", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kDynamicsPanel, 4u) },
    { kMaximizerParamId, "MAXIMIZER", GuiPage::Build, 376.0,
        kColumnWidth, layout::rowY(kDynamicsPanel, 5u) },

    { kOutputParamId, "OUTPUT", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kOutputPanel, 0u) },
    { kMidiReceiveParamId, "MIDI RECEIVE", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kOutputPanel, 1u) },
    { kTransposeParamId, "TRANSPOSE", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kOutputPanel, 2u) },
    { kExpressionModeParamId, "EXPRESSION MODE", GuiPage::Build, 16.0,
        kColumnWidth, layout::rowY(kOutputPanel, 3u) },
}};

static_assert(layout::rectFitsCanvas(kWorkspacePanel.frame, kCanvas));
static_assert(layout::validateColumn(kBuildFirstColumnPanels, kCanvas));
static_assert(layout::validateColumn(
    kBuildSecondColumnPanels, kCanvas, false));
static_assert(layout::validateColumn(
    kBuildThirdColumnPanels, kCanvas, false));
static_assert(layout::validateColumn(
    kMotionFirstColumnPanels, kCanvas, false));
static_assert(layout::validateColumn(
    kMotionSecondColumnPanels, kCanvas, false));
static_assert(layout::validateColumn(
    kMotionThirdColumnPanels, kCanvas, false));
static_assert(layout::rolesFollowTemplate(
    kBuildFirstColumnPanels, layout::kProceduralEncoderTemplate, true));
static_assert(kUiRows.size() + 32u == kParamCount);

bool uiRowVisible(const UiRow& row, GuiPage page, const Plugin& p)
{
    if (row.page != page) return false;
    return row.id != kModalDriveParamId
        || currentBodyEngine(p) == s3g::BassBodyEngine::Modal;
}

double uiRowY(const UiRow& row, const Plugin& p)
{
    if (row.id == kBodyWidthParamId
        && currentBodyEngine(p) != s3g::BassBodyEngine::Modal)
        return layout::rowY(kBodyPanel, 7u);
    return row.y;
}

constexpr bool uiPanelVisible(const UiPanel& panel, GuiPage page)
{
    return panel.page == page;
}

bool isEngineMenuParam(const Plugin& p, clap_id id)
{
    const auto engine = currentBodyEngine(p);
    return engine == s3g::BassBodyEngine::Swarm
        && id == kBodyControlCParamId;
}

bool isUiMenuParam(const Plugin& p, clap_id id)
{
    return id == kVoiceModeParamId || id == kFoundationWaveParamId
        || id == kFoundationOctaveParamId || id == kBodyEngineParamId
        || id == kTextureModeParamId || id == kFilterTypeParamId
        || id == kMotionClockParamId || isModShapeParam(id)
        || isModTargetParam(id) || id == kShredCircuitParamId
        || id == kMidiReceiveParamId || id == kExpressionModeParamId
        || id == kArpPatternParamId || id == kArpScaleParamId
        || id == kArpSyncParamId || id == kArpRateParamId
        || id == kArpOctavesParamId || id == kArpLengthParamId
        || isEngineMenuParam(p, id);
}

uint32_t uiMenuItemCount(const Plugin& p, clap_id id)
{
    if (isEngineMenuParam(p, id)) return 7u;
    if (id == kVoiceModeParamId) return 2u;
    if (id == kFoundationWaveParamId) return 3u;
    if (id == kFoundationOctaveParamId) return 2u;
    if (id == kBodyEngineParamId) return 8u;
    if (id == kTextureModeParamId) return 5u;
    if (id == kFilterTypeParamId) return 6u;
    if (id == kMotionClockParamId) return 2u;
    if (isModShapeParam(id)) return s3g::kBassModShapeCount;
    if (isModTargetParam(id)) return s3g::kBassModTargetCount;
    if (id == kShredCircuitParamId) return s3g::kBassShredCircuitCount;
    if (id == kMidiReceiveParamId) return 17u;
    if (id == kExpressionModeParamId) return 2u;
    if (id == kArpPatternParamId)
        return s3g::kProcessorStackArpPatternCount;
    if (id == kArpScaleParamId) return s3g::kProcessorStackScaleCount;
    if (id == kArpSyncParamId) return 2u;
    if (id == kArpRateParamId) return s3g::kProcessorStackArpRateCount;
    if (id == kArpOctavesParamId) return 4u;
    if (id == kArpLengthParamId) return 8u;
    return 0u;
}

double uiMenuItemValue(const Plugin& p, clap_id id, uint32_t index)
{
    const uint32_t count = uiMenuItemCount(p, id);
    if (isEngineMenuParam(p, id) && count > 1u)
        return static_cast<double>(index) / static_cast<double>(count - 1u);
    if (isModTargetParam(id) && index < kBassModTargetMenuOrder.size())
        return static_cast<double>(kBassModTargetMenuOrder[index]);
    const auto* def = paramDef(id);
    return def ? def->minimum + static_cast<double>(index)
        : static_cast<double>(index);
}

int uiMenuSelectedIndex(const Plugin& p, clap_id id)
{
    if (isEngineMenuParam(p, id)) {
        return static_cast<int>(std::lround(paramValue(p, id)
            * static_cast<double>(uiMenuItemCount(p, id) - 1u)));
    }
    if (isModTargetParam(id)) {
        const auto target = static_cast<s3g::BassModTarget>(std::lround(
            paramValue(p, id)));
        const auto found = std::find(kBassModTargetMenuOrder.begin(),
            kBassModTargetMenuOrder.end(), target);
        return found == kBassModTargetMenuOrder.end() ? -1
            : static_cast<int>(std::distance(
                kBassModTargetMenuOrder.begin(), found));
    }
    const auto* def = paramDef(id);
    return static_cast<int>(std::lround(paramValue(p, id)
        - (def ? def->minimum : 0.0)));
}

bool isLogUiParam(clap_id id)
{
    return id == kCutoffParamId || id == kAttackParamId
        || id == kDecayParamId || id == kReleaseParamId
        || id == kFilterDecayParamId || id == kPunchTimeParamId
        || id == kBodyDecayParamId || id == kTextureDecayParamId;
}

double uiNormalizedValue(clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return 0.0;
    if (isLogUiParam(id) && def->minimum > 0.0)
        return std::clamp(std::log(value / def->minimum)
            / std::log(def->maximum / def->minimum), 0.0, 1.0);
    return std::clamp((value - def->minimum)
        / std::max(1.0e-12, def->maximum - def->minimum), 0.0, 1.0);
}

double uiValueFromNormalized(clap_id id, double normalized)
{
    const auto* def = paramDef(id);
    if (!def) return 0.0;
    normalized = std::clamp(normalized, 0.0, 1.0);
    const double value = isLogUiParam(id)
        ? def->minimum * std::pow(def->maximum / def->minimum, normalized)
        : def->minimum + (def->maximum - def->minimum) * normalized;
    return clampValue(*def, value);
}

drawing::Rect lowformArpPatternFieldRect()
{
    const drawing::Rect panel = drawing::cocoaRect(
        kArpPatternPanel.frame);
    return drawing::makeRect(panel.origin.x + 16.0, panel.origin.y + 31.0,
        panel.size.width - 32.0, 72.0);
}

drawing::Rect lowformArpPatternStepRect(uint32_t step)
{
    const drawing::Rect field = lowformArpPatternFieldRect();
    const double stepWidth = field.size.width / 8.0;
    return drawing::makeRect(field.origin.x + stepWidth
            * static_cast<double>(std::min<uint32_t>(step, 7u)),
        field.origin.y, stepWidth, field.size.height);
}

clap_id lowformArpPatternStepParam(uint32_t step)
{
    return kArpStep1ParamId + std::min<uint32_t>(step, 7u);
}

clap_id lowformArpEditorParam(ArpEditLane lane, uint32_t step)
{
    step = std::min<uint32_t>(step, 7u);
    switch (lane) {
    case ArpEditLane::Pitch: return kArpStep1ParamId + step;
    case ArpEditLane::Accent: return kArpAccent1ParamId + step;
    case ArpEditLane::Gate: return kArpGateMode1ParamId + step;
    case ArpEditLane::Octave: return kArpOctave1ParamId + step;
    }
    return kArpStep1ParamId + step;
}

drawing::Rect lowformArpLaneButtonRect(uint32_t lane)
{
    const drawing::Rect panel = drawing::cocoaRect(kArpPatternPanel.frame);
    return drawing::makeRect(panel.origin.x + 126.0 + lane * 52.0,
        panel.origin.y + 3.0, 48.0, 17.0);
}

int lowformArpPatternStepAtX(double x)
{
    const drawing::Rect field = lowformArpPatternFieldRect();
    if (x < field.origin.x || x > drawing::maxX(field)) return -1;
    const double normalized = std::clamp(
        (x - field.origin.x) / field.size.width,
        static_cast<double>(0.0), static_cast<double>(0.999999));
    return static_cast<int>(std::floor(normalized * 8.0));
}

void drawLowformArpPattern(const Plugin& plugin, ArpEditLane lane,
    int dragStep, drawing::Attrs  valueAttrs,
    const drawing::Style& style)
{
    constexpr std::array<const char*, 4u> laneNames {{
        "PITCH", "ACCENT", "GATE", "OCT" }};
    const drawing::Rect panel = drawing::cocoaRect(
        kArpPatternPanel.frame);
    for (uint32_t i = 0u; i < laneNames.size(); ++i)
        drawing::drawToolboxHeaderButton(
            lowformArpLaneButtonRect(i), panel,
            drawing::stringFromUtf8(laneNames[i]),
            static_cast<uint32_t>(lane) == i, valueAttrs, style);

    const drawing::Rect field = lowformArpPatternFieldRect();
    drawing::setFill(style.strip);
    drawing::rectFill(field);
    drawing::setStroke(style.grid);
    drawing::frameRect(field);

    const uint32_t length = std::clamp<uint32_t>(static_cast<uint32_t>(
        std::lround(paramValue(plugin, kArpLengthParamId))), 1u, 8u);
    for (uint32_t step = 0u; step < 8u; ++step) {
        const drawing::Rect cell = lowformArpPatternStepRect(step);
        const clap_id id = lowformArpEditorParam(lane, step);
        const double value = paramValue(plugin, id);
        const bool rest = lane == ArpEditLane::Pitch
            && static_cast<int32_t>(std::lround(value))
            == s3g::kProcessorStackArpRest;
        const double rawValueY = drawing::maxY(field) - static_cast<double>(
            uiNormalizedValue(id, value)) * field.size.height;
        const double valueY = std::clamp(rawValueY,
            field.origin.y + 1.0, drawing::maxY(field) - 1.0);
        const bool selected = dragStep == static_cast<int>(step);
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
        char valueText[32] {};
        paramsValueToText(&plugin.plugin, id, value,
            valueText, sizeof(valueText));
        drawing::String  stepText = drawing::stringFormat("%u %s",
            step + 1u, valueText);
        drawing::drawCenteredTextToFit(stepText,
            drawing::makeRect(cell.origin.x + 2.0, panel.origin.y + 108.0,
                cell.size.width - 4.0, 14.0), valueAttrs);
    }
}

void drawLowformModActivity(const Plugin& plugin, uint32_t index,
    const layout::Panel& modPanel, const drawing::Style& style)
{
    const drawing::Rect panel = drawing::cocoaRect(modPanel.frame);
    const drawing::Rect meter = drawing::makeRect(drawing::maxX(panel) - 76.0,
        panel.origin.y + 10.0, 60.0, 4.0);
    drawing::setFill(style.strip);
    drawing::rectFill(meter);
    drawing::setStroke(style.grid);
    drawing::frameRect(meter);
    const double center = drawing::midX(meter);
    const double amount = static_cast<double>(std::clamp(
        plugin.modActivity[index].load(std::memory_order_relaxed),
        -1.0f, 1.0f)) * (meter.size.width * 0.5 - 1.0);
    drawing::setFill(style.accent);
    drawing::rectFill(drawing::makeRect(std::min(center, center + amount),
        meter.origin.y + 1.0, std::fabs(amount), meter.size.height - 2.0));
    drawing::setFill(style.text);
    drawing::rectFill(drawing::makeRect(center, meter.origin.y, 1.0, meter.size.height));
}

int factoryPresetIndex(const Plugin& p)
{
    for (uint32_t preset = 0u;
         preset < s3g::kLowformFactoryPresetCount; ++preset) {
        const auto expected = valuesFromParams(
            s3g::lowformFactoryPreset(preset), p.midiReceive,
            p.expressionMode, p.transpose, p.arpParams);
        bool match = true;
        for (uint32_t i = 0u; i < kParamCount; ++i) {
            if (isPresetPreservedParam(kParamDefs[i].id)) continue;
            const double actual = paramValue(p, kParamDefs[i].id);
            if (std::fabs(actual - expected[i]) > 1.0e-5) {
                match = false;
                break;
            }
        }
        if (match) return static_cast<int>(preset);
    }
    return -1;
}

float randomUnit(uint32_t& state)
{
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return static_cast<float>(state & 0x00ffffffu) / 16777215.0f;
}

s3g::LowformParams safeRandomParams(const Plugin& plugin,
    uint32_t seed)
{
    s3g::LowformParams p;
    p.outputGainDb = static_cast<float>(paramValue(plugin, kOutputParamId));
    p.voiceMode = randomUnit(seed) < 0.28f
        ? static_cast<float>(s3g::BassVoiceMode::Mono)
        : static_cast<float>(s3g::BassVoiceMode::Poly);
    p.glideMs = randomUnit(seed) * 240.0f;
    p.foundationWave = std::floor(randomUnit(seed) * 3.0f);
    p.foundationOctave = randomUnit(seed) < 0.18f ? -1.0f : 0.0f;
    p.foundationLevel = 0.72f + randomUnit(seed) * 0.48f;
    p.pitchPunchSemitones = randomUnit(seed) < 0.55f ? 0.0f
        : -3.0f + randomUnit(seed) * 12.0f;
    p.punchTimeMs = 18.0f + randomUnit(seed) * 150.0f;
    p.foundationReturn = 0.58f + randomUnit(seed) * 0.40f;
    p.bodyEngine = std::floor(randomUnit(seed) * 8.0f);
    p.bodyLevel = 0.38f + randomUnit(seed) * 0.67f;
    p.bodyControlA = randomUnit(seed);
    p.bodyControlB = 0.16f + randomUnit(seed) * 0.76f;
    p.bodyControlC = randomUnit(seed);
    p.bodyControlD = randomUnit(seed);
    p.bodyControlE = randomUnit(seed);
    p.bodyWidth = randomUnit(seed) * 0.72f;
    p.textureMode = randomUnit(seed) < 0.32f ? 0.0f
        : 1.0f + std::floor(randomUnit(seed) * 4.0f);
    p.textureLevel = p.textureMode == 0.0f ? 0.0f
        : 0.08f + randomUnit(seed) * 0.40f;
    p.textureColor = 0.15f + randomUnit(seed) * 0.75f;
    p.textureTrack = 0.35f + randomUnit(seed) * 0.65f;
    p.textureWidth = randomUnit(seed);
    p.filterType = 1.0f + std::floor(randomUnit(seed) * 5.0f);
    p.cutoffHz = std::pow(2.0f, std::log2(180.0f)
        + randomUnit(seed) * (std::log2(5600.0f) - std::log2(180.0f)));
    p.resonance = randomUnit(seed) * 0.48f;
    p.filterDrive = randomUnit(seed) * 0.52f;
    p.keyTrack = randomUnit(seed) * 0.82f;
    p.foundationFilter = randomUnit(seed) * 0.32f;
    p.bodyFilter = 0.58f + randomUnit(seed) * 0.42f;
    p.textureFilter = 0.48f + randomUnit(seed) * 0.52f;
    p.attackSeconds = std::pow(10.0f, -3.2f + randomUnit(seed) * 2.4f);
    p.decaySeconds = 0.04f + randomUnit(seed) * 0.80f;
    p.sustain = 0.42f + randomUnit(seed) * 0.58f;
    p.releaseSeconds = 0.06f + randomUnit(seed) * 1.2f;
    p.filterEnvelopeOctaves = -1.0f + randomUnit(seed) * 4.8f;
    p.filterDecaySeconds = 0.05f + randomUnit(seed) * 0.85f;
    // Clock is a performance preference, not a sound-design dice result.
    p.motionClock = static_cast<float>(paramValue(
        plugin, kMotionClockParamId));
    for (auto& mod : p.mods) {
        if (randomUnit(seed) < 0.30f) {
            mod.shape = 0.0f;
            mod.depth = 0.0f;
            mod.target = 0.0f;
            mod.secondaryDepth = 0.0f;
            mod.secondaryTarget = 0.0f;
        } else {
            mod.shape = 1.0f + std::floor(randomUnit(seed)
                * static_cast<float>(s3g::kBassModShapeCount - 1u));
            mod.rate = randomUnit(seed);
            mod.depth = -0.52f + randomUnit(seed) * 1.04f;
            mod.target = 1.0f + std::floor(randomUnit(seed)
                * static_cast<float>(s3g::kBassModTargetCount - 1u));
            if (randomUnit(seed) < 0.42f) {
                mod.secondaryDepth = -0.36f + randomUnit(seed) * 0.72f;
                mod.secondaryTarget = 1.0f + std::floor(randomUnit(seed)
                    * static_cast<float>(s3g::kBassModTargetCount - 1u));
            } else {
                mod.secondaryDepth = 0.0f;
                mod.secondaryTarget = 0.0f;
            }
        }
    }
    p.bodyDecaySeconds = randomUnit(seed) < 0.48f ? 12.0f
        : 0.08f + randomUnit(seed) * 2.4f;
    p.textureAttackSeconds = randomUnit(seed) < 0.62f ? 0.0f
        : randomUnit(seed) * 0.75f;
    p.textureDecaySeconds = randomUnit(seed) < 0.48f ? 12.0f
        : 0.06f + randomUnit(seed) * 2.8f;
    p.modalDrive = randomUnit(seed) * 0.72f;
    p.modWheelAmount = randomUnit(seed);
    p.dynamicsBite = randomUnit(seed) * 0.30f;
    p.tube = randomUnit(seed) * 0.62f;
    p.shred = randomUnit(seed) < 0.48f ? 0.0f
        : 0.12f + randomUnit(seed) * 0.56f;
    p.shredCircuit = p.shred > 0.0f
        ? std::floor(randomUnit(seed) * s3g::kBassShredCircuitCount) : 0.0f;
    p.shredFeedback = p.shred > 0.0f ? randomUnit(seed) * 0.36f : 0.0f;
    p.shredColor = 0.22f + randomUnit(seed) * 0.62f;
    p.shredMix = p.shred > 0.0f ? 0.12f + randomUnit(seed) * 0.46f : 0.0f;
    p.dynamics = 0.12f + randomUnit(seed) * 0.52f;
    p.saturation = randomUnit(seed) * 0.38f;
    p.clip = randomUnit(seed) < 0.75f ? 0.0f : randomUnit(seed) * 0.30f;
    p.tilt = -0.35f + randomUnit(seed) * 0.70f;
    p.maximizer = 0.12f + randomUnit(seed) * 0.48f;
    return p;
}

class Editor final : public s3g::portable_gui::routing::Canvas {
public:
using Canvas = s3g::portable_gui::routing::Canvas;

    Plugin* _plugin;
    int _dragParam;
    int _dragPatternStep;
    ArpEditLane _arpEditLane;
    int _factoryPresetIndex;
    clap_id _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    uint64_t _observedParamRevision;
    GuiPage _page;
    char _presetName[64];
#include "s3g_stereo_processor_editor.inc"
explicit Editor(Plugin& plugin) : Canvas(kGuiWidth,kGuiHeight) {_plugin = &plugin;
    _dragParam = -1;
    _dragPatternStep = -1;
    _arpEditLane = ArpEditLane::Pitch;
    _openMenu = CLAP_INVALID_ID;
    _hoverMenuItem = -1;
    _menuItemCount = 0u;
    _page = GuiPage::Build;
    auto* p = static_cast<Plugin*>(_plugin);
    _observedParamRevision = p ? p->parameterRevision.load(
        std::memory_order_acquire) : 0u;
    _factoryPresetIndex = p ? factoryPresetIndex(*p) : -1;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        _factoryPresetIndex >= 0
            ? s3g::lowformFactoryPresetInfo(
                static_cast<uint32_t>(_factoryPresetIndex)).name
            : "CUSTOM");
}

void applyFactoryPreset(int index)  {
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    index = std::clamp(index, 0,
        static_cast<int>(s3g::kLowformFactoryPresetCount - 1u));
    if (!queueGuiParams(*p, s3g::lowformFactoryPreset(
            static_cast<uint32_t>(index)))) {
        beep();
        return;
    }
    _factoryPresetIndex = index;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        s3g::lowformFactoryPresetInfo(
            static_cast<uint32_t>(index)).name);
    setNeedsDisplay(true);

}

void markCustomPreset()  {
    _factoryPresetIndex = -1;
    std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");

}

int workspacePage()  {
    return static_cast<int>(_page);

}

drawing::Rect openMenuRect()  {
    drawing::Rect anchor = drawing::Rect{};
    if (_openMenu == kFactoryPresetMenuId) {
        anchor = drawing::cocoaRect(
            drawing::encoderTitleBand(kGuiWidth, kGuiHeight).presetMenu);
    } else {
        auto* p = static_cast<Plugin*>(_plugin);
        if (!p) return drawing::Rect{};
        const auto row = std::find_if(kUiRows.begin(), kUiRows.end(),
            [=](const UiRow& candidate) {
                return candidate.id == _openMenu
                    && uiRowVisible(candidate, _page, *p);
            });
        if (row == kUiRows.end()) return drawing::Rect{};
        anchor = drawing::makeRect(layout::processorControlX(row->panelX),
            uiRowY(*row, *p) - 1.0,
            layout::processorMenuWidth(row->panelWidth), 15.0);
    }
    const bool multiColumnTargets = isModTargetParam(_openMenu);
    const uint32_t rows = multiColumnTargets
        ? drawing::multiColumnMenuRows(
            _menuItemCount, kModTargetMenuColumns)
        : _menuItemCount;
    const double height = 18.0 * rows;
    const double width = anchor.size.width
        * (multiColumnTargets ? kModTargetMenuColumns : 1u);
    double y = drawing::maxY(anchor) + 2.0;
    if (y + height > kGuiHeight) y = anchor.origin.y - 2.0 - height;
    double x = anchor.origin.x;
    if (x + width > kGuiWidth - 16.0) x = kGuiWidth - 16.0 - width;
    return drawing::makeRect(x, y, width, height);

}

void drawOpenMenu_style(drawing::Attrs  attrs, const drawing::Style& style)  {
    if (_openMenu == CLAP_INVALID_ID || _menuItemCount == 0u) return;
    drawing::String  items[32] {};
    const uint32_t count = std::min<uint32_t>(_menuItemCount, 32u);
    auto* p = static_cast<Plugin*>(_plugin);
    if (_openMenu == kFactoryPresetMenuId) {
        for (uint32_t i = 0u; i < count; ++i)
            items[i] = drawing::stringFromUtf8(s3g::lowformFactoryPresetInfo(i).name);
    } else if (_openMenu == kFoundationOctaveParamId) {
        items[0] = "-1 OCT";
        items[1] = "+0 OCT";
    } else {
        for (uint32_t i = 0u; i < count; ++i) {
            const double value = uiMenuItemValue(*p, _openMenu, i);
            char text[64] {};
            paramsValueToText(&p->plugin, _openMenu, value,
                text, sizeof(text));
            items[i] = drawing::stringFromUtf8(text);
        }
    }
    const int selected = _openMenu == kFactoryPresetMenuId
        ? _factoryPresetIndex
        : uiMenuSelectedIndex(*p, _openMenu);
    if (isModTargetParam(_openMenu)) {
        drawing::drawMultiColumnDropdownMenu(
            openMenuRect(), 18.0, items, count,
            kModTargetMenuColumns, selected, _hoverMenuItem, attrs, style);
    } else {
        drawing::drawDropdownMenu(openMenuRect(), 18.0,
            items, count, selected, _hoverMenuItem, attrs, style);
    }

}

void service() override {
    
    auto* p = static_cast<Plugin*>(_plugin);
    if (p) {
        const uint64_t revision = p->parameterRevision.load(
            std::memory_order_acquire);
        if (revision != _observedParamRevision) {
            _observedParamRevision = revision;
            _factoryPresetIndex = factoryPresetIndex(*p);
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                _factoryPresetIndex >= 0
                    ? s3g::lowformFactoryPresetInfo(
                        static_cast<uint32_t>(_factoryPresetIndex)).name
                    : "CUSTOM");
        }
    }
    if (true && p && true)
        setNeedsDisplay(true);

}

void drawOriginal(drawing::Rect dirty)  {
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    drawing::Style style;
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs  titleAttrs = drawing::softTitleAttrs();
    drawing::Attrs  labelAttrs = drawing::softLabelAttrs();
    drawing::Attrs  valueAttrs = drawing::softValueAttrs();
    const auto titleBand = drawing::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    drawing::drawEncoderTitleBand("s3g PROCESSOR LOWFORM 2",
        drawing::stringFromUtf8(_presetName),
        drawing::peakDbText(
            p->outputPeak.load(std::memory_order_relaxed)),
        titleBand, titleAttrs, labelAttrs, valueAttrs, style);

    drawing::drawPanelFrame(kWorkspacePanel, style);
    drawing::drawPanelHeader("WORKSPACE", true, kWorkspacePanel,
        labelAttrs, style);
    const drawing::Rect header = drawing::cocoaRect(kWorkspacePanel.frame);
    constexpr std::array<const char*, 2u> names {{ "BUILD", "MOTION" }};
    for (uint32_t i = 0u; i < names.size(); ++i) {
        const drawing::Rect button = drawing::makeRect(904.0 + i * 82.0, 44.0, 74.0, 17.0);
        drawing::drawToolboxHeaderButton(button, header,
            drawing::stringFromUtf8(names[i]),
            static_cast<uint32_t>(_page) == i, valueAttrs, style);
    }

    for (const auto& panel : kUiPanels) {
        if (!uiPanelVisible(panel, _page)) continue;
        drawing::drawPanelFrame(panel.panel, style);
        drawing::String  panelTitle = panel.panel.frame.x == kBodyPanel.frame.x
                && panel.panel.frame.y == kBodyPanel.frame.y
                && panel.page == GuiPage::Build
            ? drawing::stringFormat("%s ENGINE",
                s3g::bassBodyEngineName(currentBodyEngine(*p)))
            : drawing::stringFromUtf8(panel.title);
        drawing::drawPanelHeader(panelTitle, true, panel.panel,
            labelAttrs, style);
    }
    for (const auto& row : kUiRows) {
        if (!uiRowVisible(row, _page, *p)) continue;
        const double value = paramValue(*p, row.id);
        char text[64] {};
        paramsValueToText(&p->plugin, row.id, value, text, sizeof(text));
        const bool bodyControl = isBodyControlParam(row.id);
        drawing::String  label = drawing::stringFromUtf8(bodyControl
            ? bodyControlName(currentBodyEngine(*p), row.id) : row.label);
        if (bodyControl) label = drawing::uppercaseString(label);
        drawing::String  display = drawing::stringFromUtf8(text);
        if (isUiMenuParam(*p, row.id))
            drawing::drawProcessorMenu(label, display, row.y,
                row.panelX, row.panelWidth, labelAttrs, valueAttrs, style);
        else
            drawing::drawProcessorSlider(label, display,
                static_cast<double>(uiNormalizedValue(row.id, value)),
                uiRowY(row, *p), row.panelX, row.panelWidth,
                labelAttrs, valueAttrs, style);
    }
    if (_page == GuiPage::Motion) {
        drawLowformModActivity(*p, 0u, kModOnePanel, style);
        drawLowformModActivity(*p, 1u, kModTwoPanel, style);
        drawLowformModActivity(*p, 2u, kModThreePanel, style);
        drawLowformArpPattern(*p, _arpEditLane,
            _dragPatternStep, valueAttrs, style);
    }
    drawOpenMenu_style(valueAttrs, style);

}

void updateDraggedParam(drawing::Point point)  {
    if (_dragParam <= 0) return;
    const clap_id id = static_cast<clap_id>(_dragParam);
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    const auto row = std::find_if(kUiRows.begin(), kUiRows.end(),
        [=](const UiRow& candidate) {
            return candidate.id == id && uiRowVisible(candidate, _page, *p);
        });
    if (row == kUiRows.end()) return;
    const double x = layout::processorControlX(row->panelX);
    const double width = layout::processorTrackWidth(row->panelWidth);
    const double normalized = std::clamp((point.x - x) / width, 0.0, 1.0);
    queueGuiParamValue(*p, id, uiValueFromNormalized(id, normalized));
    setNeedsDisplay(true);

}

void updateDraggedPattern(drawing::Point point)  {
    const int nextStep = lowformArpPatternStepAtX(point.x);
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    if (nextStep >= 0 && nextStep != _dragPatternStep) {
        if (_dragPatternStep >= 0)
            queueGuiParamGestureEnd(*p, lowformArpEditorParam(
                _arpEditLane, static_cast<uint32_t>(_dragPatternStep)));
        _dragPatternStep = nextStep;
        queueGuiParamGestureBegin(*p, lowformArpEditorParam(
            _arpEditLane, static_cast<uint32_t>(_dragPatternStep)));
    }
    if (_dragPatternStep < 0) return;
    const drawing::Rect field = lowformArpPatternFieldRect();
    const double normalized = std::clamp(
        (drawing::maxY(field) - point.y) / field.size.height, 0.0, 1.0);
    const clap_id id = lowformArpEditorParam(_arpEditLane,
        static_cast<uint32_t>(_dragPatternStep));
    const double nextValue = uiValueFromNormalized(id, normalized);
    // The graph is stepped. Do not flood the GUI/audio queue with hundreds
    // of identical mouse-drag values while transport is stopped.
    if (paramValue(*p, id) != nextValue)
        queueGuiParamValue(*p, id, nextValue);
    setNeedsDisplay(true);

}

void rightMouseDown(drawing::Event*  event)  {
    if (_page != GuiPage::Motion) {
        (void)0;
        return;
    }
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    if (!drawing::contains(point, lowformArpPatternFieldRect())) {
        (void)0;
        return;
    }
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    const int step = lowformArpPatternStepAtX(point.x);
    if (step < 0) return;
    queueGuiParamGesture(*p,
        lowformArpPatternStepParam(static_cast<uint32_t>(step)),
        static_cast<double>(s3g::kProcessorStackArpRest));
    _dragPatternStep = -1;
    setNeedsDisplay(true);

}

void mouseDown(drawing::Event*  event)  {
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    const auto titleBand = drawing::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    if (_openMenu != CLAP_INVALID_ID) {
        const int hit = isModTargetParam(_openMenu)
            ? drawing::multiColumnDropdownHitIndex(
                point, openMenuRect(), 18.0, _menuItemCount,
                kModTargetMenuColumns)
            : drawing::dropdownHitIndex(
                point, openMenuRect(), 18.0, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == kFactoryPresetMenuId) {
                applyFactoryPreset(hit);
            } else {
                const double value = uiMenuItemValue(
                    *p, _openMenu, static_cast<uint32_t>(hit));
                queueGuiParamGesture(*p, _openMenu, value);
                if (_openMenu != kOutputParamId
                    && !isPresetPreservedParam(_openMenu))
                    markCustomPreset();
            }
        }
        _openMenu = CLAP_INVALID_ID;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        setNeedsDisplay(true);
        return;
    }
    for (uint32_t i = 0u; i < 2u; ++i) {
        const drawing::Rect button = drawing::makeRect(904.0 + i * 82.0, 44.0, 74.0, 17.0);
        if (drawing::contains(point, button)) {
            _page = static_cast<GuiPage>(i);
            setNeedsDisplay(true);
            return;
        }
    }
    if (drawing::contains(point,
            drawing::cocoaRect(titleBand.presetMenu))) {
        _openMenu = kFactoryPresetMenuId;
        _hoverMenuItem = -1;
        _menuItemCount = s3g::kLowformFactoryPresetCount;
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
        } else beep();
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point,
            drawing::cocoaRect(titleBand.saveButton))) {
        drawing::String name;
        if (presetDialog(true, &name))
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                !name.empty() ? name.c_str() : "CUSTOM");
        else beep();
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point,
            drawing::cocoaRect(titleBand.randomButton))) {
        if (queueGuiParams(*p, safeRandomParams(*p, drawing::randomBits())))
            markCustomPreset();
        else beep();
        setNeedsDisplay(true);
        return;
    }
    if (_page == GuiPage::Motion) {
        for (uint32_t lane = 0u; lane < 4u; ++lane) {
            if (drawing::contains(point, lowformArpLaneButtonRect(lane))) {
                _arpEditLane = static_cast<ArpEditLane>(lane);
                _dragPatternStep = -1;
                setNeedsDisplay(true);
                return;
            }
        }
    }
    if (_page == GuiPage::Motion
        && drawing::contains(point, lowformArpPatternFieldRect())) {
        const int step = lowformArpPatternStepAtX(point.x);
        if (step < 0) return;
        const clap_id id = lowformArpEditorParam(_arpEditLane,
            static_cast<uint32_t>(step));
        double defaultValue = 0.0;
        if (drawing::sliderDoubleClickDefault(
                event, &p->plugin, id, &defaultValue)) {
            queueGuiParamGesture(*p, id, defaultValue);
            _dragPatternStep = -1;
        } else {
            _dragPatternStep = step;
            queueGuiParamGestureBegin(*p, id);
            updateDraggedPattern(point);
        }
        setNeedsDisplay(true);
        return;
    }
    for (const auto& row : kUiRows) {
        if (!uiRowVisible(row, _page, *p)) continue;
        const double rowY = uiRowY(row, *p);
        const drawing::Rect hit = drawing::makeRect(
            row.panelX + layout::kStandardMetrics.hitInset, rowY - 9.0,
            row.panelWidth - layout::kStandardMetrics.hitInset * 2.0,
            layout::kStandardMetrics.hitHeight);
        if (!drawing::contains(point, hit)) continue;
        if (isUiMenuParam(*p, row.id)) {
            _openMenu = row.id;
            _hoverMenuItem = -1;
            _menuItemCount = uiMenuItemCount(*p, row.id);
            setNeedsDisplay(true);
            return;
        }
        double defaultValue = 0.0;
        if (drawing::sliderDoubleClickDefault(
                event, &p->plugin, row.id, &defaultValue)) {
            queueGuiParamGesture(*p, row.id, defaultValue);
            _dragParam = -1;
        } else {
            _dragParam = static_cast<int>(row.id);
            queueGuiParamGestureBegin(*p, row.id);
            updateDraggedParam(point);
        }
        if (row.id != kOutputParamId
            && !isPresetPreservedParam(row.id))
            markCustomPreset();
        return;
    }

}

void mouseDragged(drawing::Event*  event)  {
    if (_dragPatternStep >= 0)
        updateDraggedPattern(convertPoint_fromView(event->locationInWindow(), nullptr));
    else if (_dragParam > 0)
        updateDraggedParam(convertPoint_fromView(event->locationInWindow(), nullptr));

}

void mouseUp(drawing::Event*  event)  {
    (void)event;
    if (_dragParam > 0)
        queueGuiParamGestureEnd(*static_cast<Plugin*>(_plugin),
            static_cast<clap_id>(_dragParam));
    if (_dragPatternStep >= 0)
        queueGuiParamGestureEnd(*static_cast<Plugin*>(_plugin),
            lowformArpEditorParam(_arpEditLane,
                static_cast<uint32_t>(_dragPatternStep)));
    _dragParam = -1;
    _dragPatternStep = -1;

}

void mouseMoved(drawing::Event*  event)  {
    if (_openMenu == CLAP_INVALID_ID) return;
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    const int hover = isModTargetParam(_openMenu)
        ? drawing::multiColumnDropdownHitIndex(
            point, openMenuRect(), 18.0, _menuItemCount,
            kModTargetMenuColumns)
        : drawing::dropdownHitIndex(
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
