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
