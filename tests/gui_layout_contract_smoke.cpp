#include "../plugins/common/s3g_gui_layout.h"

#include <array>

namespace {

namespace layout = s3g::gui_layout;

constexpr layout::Canvas kCanvas { 1160.0, 858.0 };
constexpr auto kOutput = layout::makePanel(
    layout::PluginClass::ProceduralEncoder,
    layout::PanelRole::Output,
    layout::kLargeEncoderFirstColumn,
    42.0, 80.0, 2u);
constexpr auto kEngine = layout::stackPanel(
    layout::PanelRole::Engine, kOutput,
    layout::toolboxHeightForRows(6u), 6u);
constexpr auto kEnvelope = layout::stackPanel(
    layout::PanelRole::Envelope, kEngine,
    layout::toolboxHeightForRows(4u), 4u);
constexpr std::array kPanels { kOutput, kEngine, kEnvelope };
constexpr auto kTopology = layout::makePanel(
    layout::PluginClass::ProceduralEncoder,
    layout::PanelRole::Topology,
    layout::kLargeEncoderSecondColumn,
    42.0, layout::toolboxHeightForRows(8u), 8u);
constexpr auto kProjection = layout::stackPanel(
    layout::PanelRole::Projection, kTopology,
    layout::toolboxHeightForRows(4u), 4u);
constexpr std::array kSecondColumnPanels { kTopology, kProjection };

static_assert(layout::rowY(kOutput, 0u) == 78.0);
static_assert(layout::rowY(kOutput, 1u) == 104.0);
static_assert(layout::rowY(kEngine, 0u) == 170.0);
static_assert(layout::rowY(kEngine, 5u) == 300.0);
static_assert(layout::toolboxFirstRowY(42.0) == 78.0);
static_assert(layout::toolboxRowY(42.0, 3u) == 156.0);
static_assert(layout::kStandardMetrics.toolboxBottomClearance == 18.0);
static_assert(layout::kStandardMetrics.contentTop == 42.0);
static_assert(layout::kStandardMetrics.headerLabelInset == 8.0);
static_assert(layout::kStandardMetrics.labelInset == 16.0);
static_assert(layout::kStandardMetrics.labelInset
    - layout::kStandardMetrics.headerLabelInset == 8.0);
static_assert(layout::processorLabelX(596.0) == 612.0);
static_assert(layout::processorControlX(596.0) == 704.0);
static_assert(layout::processorValueX(596.0, 306.0) == 844.0);
static_assert(layout::processorTrackWidth(306.0) == 132.0);
static_assert(layout::processorTrackWidth(344.0) == 150.0);
static_assert(layout::processorMenuWidth(306.0) == 182.0);
static_assert(layout::toolboxHeightForRows(1u) == 54.0);
static_assert(layout::toolboxHeightForRows(4u) == 132.0);
static_assert(layout::toolboxHeightForRows(7u) == 210.0);
static_assert(layout::toolboxHeightForRows(0u) == 0.0);
constexpr bool kConditionalRows[] { true, false, true, false, true };
static_assert(layout::compactedVisibleRow(kConditionalRows, 5u, 0u) == 0u);
static_assert(layout::compactedVisibleRow(kConditionalRows, 5u, 2u) == 1u);
static_assert(layout::compactedVisibleRow(kConditionalRows, 5u, 4u) == 2u);
static_assert(layout::menuBoxRect(kEngine, 0u).x == 738.0);
static_assert(layout::menuBoxRect(kEngine, 0u).y == 169.0);
static_assert(layout::sliderHitRect(kEngine, 2u).x == 638.0);
static_assert(layout::sliderHitRect(kEngine, 2u).y == 214.0);
static_assert(layout::validateColumn(kPanels, kCanvas));
static_assert(layout::rolesFollowTemplate(
    kPanels, layout::kProceduralEncoderTemplate, true));
static_assert(layout::controlMatchesSlot(
    kOutput, layout::kLargeEncoderOrderSlot));
static_assert(layout::rowY(
    kOutput, layout::kLargeEncoderOrderSlot.row) == 104.0);
static_assert(layout::roleMatchesAnchorIfPresent(
    kSecondColumnPanels, layout::PanelRole::Topology,
    layout::kLargeEncoderTopologyAnchor));
static_assert(layout::rolesFollowTemplate(
    kSecondColumnPanels, layout::kProceduralEncoderTemplate, false));
static_assert(layout::topologyRow(
    layout::SharedControlRole::TopologyShape) == 0u);
static_assert(layout::topologyRow(
    layout::SharedControlRole::TopologyRate) == 2u);
static_assert(layout::topologyRow(
    layout::SharedControlRole::TopologyTwist) == 7u);
static_assert(layout::topologyControlMatches(
    kTopology, layout::SharedControlRole::TopologyTwist));

constexpr auto kWideTitle = layout::encoderTitleBand({ 1160.0, 858.0 });
static_assert(kWideTitle.presetLabelX == 320.0);
static_assert(kWideTitle.presetMenu.x == 382.0);
static_assert(kWideTitle.loadButton.x == 580.0);
static_assert(kWideTitle.saveButton.x == 636.0);
static_assert(kWideTitle.randomButton.x == 692.0);
static_assert(layout::encoderTitleActionRect(
    kWideTitle, layout::EncoderTitleAction::Load).x == 580.0);
static_assert(layout::encoderTitleBandFits(kWideTitle));

constexpr auto kMediumTitle = layout::encoderTitleBand({ 1040.0, 620.0 });
static_assert(kMediumTitle.presetLabelX == 280.0);
static_assert(kMediumTitle.presetMenu.x == 342.0);
static_assert(layout::encoderTitleBandFits(kMediumTitle));

constexpr auto kCompactTitle = layout::encoderTitleBand({ 900.0, 716.0 });
static_assert(kCompactTitle.presetLabelX == 238.0);
static_assert(kCompactTitle.presetMenu.x == 300.0);
static_assert(layout::encoderTitleBandFits(kCompactTitle));

constexpr const auto& kPanner = layout::kPannerFamilyLayout;
constexpr std::array kPannerRegularPanels {
    kPanner.output, kPanner.panner, kPanner.source
};
constexpr std::array kPannerDesignPanels {
    kPanner.output, kPanner.designPanner, kPanner.speaker
};
static_assert(kPanner.canvas.width == 900.0);
static_assert(kPanner.canvas.height == 720.0);
static_assert(kPanner.mainPanel.y == layout::kStandardMetrics.contentTop);
static_assert(kPanner.output.frame.x == 630.0);
static_assert(kPanner.outputRowY == layout::rowY(kPanner.output, 0u));
static_assert(kPanner.outputRowY == 78.0);
static_assert(kPanner.pannerFirstRowY == layout::rowY(kPanner.panner, 0u));
static_assert(kPanner.sourceFirstRowY == layout::rowY(kPanner.source, 0u));
static_assert(kPanner.speakerFirstRowY == layout::rowY(kPanner.speaker, 0u));
static_assert(kPanner.menuWidth == 102.0);
static_assert(kPanner.sliderTrackWidth == 82.0);
static_assert(layout::pannerMenuBoxRect(kPanner.panner, 0u).width == 102.0);
static_assert(layout::rowY(kPanner.panner, 10u) == 404.0);
static_assert(layout::rowY(kPanner.source, 4u) == 574.0);
static_assert(layout::rowY(kPanner.designPanner, 2u) == 196.0);
static_assert(layout::rowY(kPanner.speaker, 3u) == 384.0);
static_assert(layout::validateColumn(kPannerRegularPanels, kPanner.canvas));
static_assert(layout::validateColumn(kPannerDesignPanels, kPanner.canvas));
static_assert(layout::rolesFollowTemplate(
    kPannerRegularPanels, layout::kSpatialPannerDecoderTemplate, true));
static_assert(layout::rolesFollowTemplate(
    kPannerDesignPanels, layout::kSpatialPannerDecoderTemplate, true));

constexpr const auto& kMacro = layout::kMacroFamilyLayout;
constexpr std::array kMacroDelayPanels {
    kMacro.output, kMacro.delayEngine, kMacro.delayRelationships
};
constexpr std::array kMacroPitchPanels {
    kMacro.output, kMacro.pitchEngine, kMacro.pitchRelationships
};
constexpr std::array kMacroPreviewPanels { kMacro.preview };
static_assert(kMacro.canvas.width == 760.0);
static_assert(kMacro.canvas.height == 496.0);
static_assert(layout::kMacroFirstColumn.x == 18.0);
static_assert(layout::kMacroFirstColumn.width == 352.0);
static_assert(layout::kMacroSecondColumn.x == 388.0);
static_assert(layout::kMacroSecondColumn.width == 354.0);
static_assert(layout::rowY(kMacro.output, 0u) == 78.0);
static_assert(layout::rowY(kMacro.output, 1u) == 104.0);
static_assert(layout::rowY(kMacro.delayEngine, 0u) == 170.0);
static_assert(layout::rowY(kMacro.pitchEngine, 2u) == 222.0);
static_assert(layout::rowY(kMacro.delayRelationships, 0u) == 340.0);
static_assert(layout::rowY(kMacro.pitchRelationships, 0u) == 288.0);
static_assert(layout::processorLabelX(kMacro.output.frame.x) == 34.0);
static_assert(layout::processorControlX(kMacro.output.frame.x) == 126.0);
static_assert(layout::processorValueX(
    kMacro.output.frame.x, kMacro.output.frame.width) == 312.0);
static_assert(layout::processorSliderFitsPanel(kMacro.output));
static_assert(layout::processorSliderFitsPanel(kMacro.delayEngine));
static_assert(layout::processorSliderFitsPanel(
    kMacro.delayRelationships));
static_assert(layout::processorSliderFitsPanel(kMacro.pitchEngine));
static_assert(layout::processorSliderFitsPanel(
    kMacro.pitchRelationships));
static_assert(kMacro.preview.role
    == layout::PanelRole::LaneRelationships);
static_assert(kMacro.preview.frame.x
    == layout::kMacroSecondColumn.x);
static_assert(layout::validateColumn(kMacroDelayPanels, kMacro.canvas));
static_assert(layout::validateColumn(kMacroPitchPanels, kMacro.canvas));
static_assert(layout::validateColumn(
    kMacroPreviewPanels, kMacro.canvas, false));
static_assert(layout::rolesFollowTemplate(
    kMacroDelayPanels, layout::kMacroEffectTemplate, true));
static_assert(layout::rolesFollowTemplate(
    kMacroPreviewPanels, layout::kMacroEffectTemplate, false));

constexpr const auto& kMacroShred = layout::kMacroShredFamilyLayout;
constexpr std::array kMacroShredFirstPanels {
    kMacroShred.output, kMacroShred.engine, kMacroShred.relationships
};
constexpr std::array kMacroShredSecondPanels {
    kMacroShred.preview, kMacroShred.containment
};
static_assert(kMacroShred.canvas.width == 760.0);
static_assert(kMacroShred.canvas.height == 620.0);
static_assert(kMacroShred.output.frame.x
    == layout::kMacroFirstColumn.x);
static_assert(kMacroShred.output.frame.width
    == layout::kMacroFirstColumn.width);
static_assert(kMacroShred.preview.role
    == layout::PanelRole::LaneRelationships);
static_assert(kMacroShred.preview.frame.x
    == layout::kMacroSecondColumn.x);
static_assert(kMacroShred.preview.frame.width
    == layout::kMacroSecondColumn.width);
static_assert(layout::processorSliderFitsPanel(kMacroShred.output));
static_assert(layout::processorSliderFitsPanel(kMacroShred.engine));
static_assert(layout::processorSliderFitsPanel(
    kMacroShred.relationships));
static_assert(layout::panelContainsRect(
    kMacroShred.containment, kMacroShred.containmentMeter));
static_assert(layout::panelContainsRect(
    kMacroShred.containment, kMacroShred.containmentField));
static_assert(layout::panelContainsRect(
    kMacroShred.containment, kMacroShred.panicButton));
static_assert(layout::validateColumn(
    kMacroShredFirstPanels, kMacroShred.canvas));
static_assert(layout::validateColumn(
    kMacroShredSecondPanels, kMacroShred.canvas, false));
static_assert(layout::rolesFollowTemplate(
    kMacroShredFirstPanels, layout::kMacroEffectTemplate, true));
static_assert(layout::rolesFollowTemplate(
    kMacroShredSecondPanels, layout::kMacroEffectTemplate, false));

constexpr const auto& kMacroShredMono =
    layout::kMacroShredMonoFamilyLayout;
constexpr std::array kMacroShredMonoPanels {
    kMacroShredMono.output, kMacroShredMono.engine,
    kMacroShredMono.containment
};
static_assert(kMacroShredMono.canvas.width == 416.0);
static_assert(kMacroShredMono.canvas.height == 498.0);
static_assert(kMacroShredMono.output.frame.y == 68.0);
static_assert(layout::processorSliderFitsPanel(
    kMacroShredMono.output));
static_assert(layout::processorSliderFitsPanel(
    kMacroShredMono.engine));
static_assert(layout::panelContainsRect(
    kMacroShredMono.containment, kMacroShredMono.containmentMeter));
static_assert(layout::panelContainsRect(
    kMacroShredMono.containment, kMacroShredMono.panicButton));
static_assert(layout::validateColumn(
    kMacroShredMonoPanels, kMacroShredMono.canvas));
static_assert(layout::rolesFollowTemplate(
    kMacroShredMonoPanels, layout::kMacroEffectTemplate, true));
constexpr auto kMacroTitle = layout::macroTitleBand(kMacro.canvas);
constexpr auto kMacroMonoTitle =
    layout::macroTitleBand(kMacroShredMono.canvas);
static_assert(kMacroTitle.controlY == 14.0);
static_assert(kMacroMonoTitle.controlY == 40.0);
static_assert(layout::processorTitleBandFits(kMacroTitle));
static_assert(layout::processorTitleBandFits(kMacroMonoTitle));

constexpr std::array kEngineSubset {
    layout::EncoderFamilyControl::Order,
    layout::EncoderFamilyControl::VoicesObjects,
    layout::EncoderFamilyControl::SourceSelection,
};
static_assert(layout::controlsFollowFamilyOrder(
    kEngineSubset, layout::kEngineControlOrder));
constexpr std::array kInvalidEngineSubset {
    layout::EncoderFamilyControl::VoicesObjects,
    layout::EncoderFamilyControl::Order,
};
static_assert(!layout::controlsFollowFamilyOrder(
    kInvalidEngineSubset, layout::kEngineControlOrder));
static_assert(layout::encoderControlOrder(
    layout::EncoderControlFamily::Tuning).controls[0]
        == layout::EncoderFamilyControl::BaseRoot);
static_assert(layout::encoderControlOrder(
    layout::EncoderControlFamily::Envelope).controls[3]
        == layout::EncoderFamilyControl::Release);
static_assert(layout::encoderControlOrder(
    layout::EncoderControlFamily::Projection).controls[0]
        == layout::EncoderFamilyControl::Azimuth);
static_assert(layout::encoderControlOrder(
    layout::EncoderControlFamily::Motion).controls[2]
        == layout::EncoderFamilyControl::Rate);
static_assert(layout::encoderControlOrder(
    layout::EncoderControlFamily::Listener).controls[4]
        == layout::EncoderFamilyControl::ListenerResponseBypass);
static_assert(layout::encoderControlOrder(
    layout::EncoderControlFamily::Environment).controls[4]
        == layout::EncoderFamilyControl::EnvironmentAir);

constexpr auto kInvalidFirst = layout::makePanel(
    layout::PluginClass::ProceduralEncoder,
    layout::PanelRole::Engine,
    layout::kLargeEncoderFirstColumn,
    42.0, 54.0, 1u);
constexpr std::array kInvalidPanels { kInvalidFirst };
static_assert(!layout::validateColumn(kInvalidPanels, kCanvas));
constexpr auto kLateTopology = layout::makePanel(
    layout::PluginClass::ProceduralEncoder,
    layout::PanelRole::Topology,
    layout::kLargeEncoderSecondColumn,
    68.0, 254.0, 8u);
constexpr std::array kInvalidTopologyPanels { kLateTopology };
static_assert(!layout::roleMatchesAnchorIfPresent(
    kInvalidTopologyPanels, layout::PanelRole::Topology,
    layout::kLargeEncoderTopologyAnchor));

constexpr bool templatesBeginWithOutput()
{
    constexpr layout::PluginClass classes[] {
        layout::PluginClass::ProceduralEncoder,
        layout::PluginClass::EffectProcessor,
        layout::PluginClass::MacroEffect,
        layout::PluginClass::SpatialPannerDecoder,
        layout::PluginClass::MixerMatrixLane,
        layout::PluginClass::CompactUtility,
    };
    for (const auto pluginClass : classes) {
        const auto& order = layout::templateOrder(pluginClass);
        if (order.requiresOutputFirst
            && (order.firstColumnCount == 0u
                || order.firstColumn[0] != layout::PanelRole::Output)) {
            return false;
        }
    }
    return true;
}

static_assert(templatesBeginWithOutput());

} // namespace

int main()
{
    return layout::validateColumn(kPanels, kCanvas) ? 0 : 1;
}
