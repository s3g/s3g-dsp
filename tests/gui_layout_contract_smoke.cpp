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
constexpr auto kCombinedOutputSource = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder,
    layout::PanelRole::Output,
    layout::kLargeEncoderFirstColumn, 42.0, 3u);
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
constexpr layout::Rect kEnvironmentalFieldPanel {
    18.0, 42.0, 596.0, 608.0
};
constexpr auto kEnvironmentalFieldButton =
    layout::environmentalFieldPageButtonRect(kEnvironmentalFieldPanel, 0u);
constexpr auto kEnvironmentalSurfButton =
    layout::environmentalFieldPageButtonRect(kEnvironmentalFieldPanel, 1u);
static_assert(layout::environmentalFieldHeaderFits(kEnvironmentalFieldPanel));
static_assert(kEnvironmentalFieldButton.x == 254.0);
static_assert(kEnvironmentalSurfButton.x == 304.0);
static_assert(kEnvironmentalFieldButton.width == 45.0);
static_assert(kEnvironmentalSurfButton.x + kEnvironmentalSurfButton.width
    < kEnvironmentalFieldPanel.x
        + layout::kEnvironmentalFieldHeader.rightControlsOffset);
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
static_assert(layout::kTopologyProcessorColumns.field.x == 12.0);
static_assert(layout::kTopologyProcessorColumns.field.y
    == layout::kStandardMetrics.contentTop);
static_assert(layout::kTopologyProcessorColumns.field.width == 620.0);
static_assert(layout::kTopologyProcessorColumns.field.height == 638.0);
static_assert(layout::kTopologyProcessorColumns.field.x
        + layout::kTopologyProcessorColumns.field.width
        + layout::kStandardMetrics.panelGap
    == layout::kTopologyProcessorColumns.first.x);
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
static_assert(layout::sourceCardinalityRow(
    layout::SharedControlRole::SourceCardinality) == 0u);
static_assert(layout::sourceCardinalityControlMatches(
    kEngine, layout::SharedControlRole::SourceCardinality));
static_assert(layout::combinedOutputSourceCardinalityRow(
    layout::SharedControlRole::SourceCardinality) == 2u);
static_assert(layout::combinedOutputSourceCardinalityControlMatches(
    kCombinedOutputSource,
    layout::SharedControlRole::SourceCardinality));
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
static_assert(kMacroShred.engine.rowCount == 9u);
static_assert(layout::panelRowsFit(kMacroShred.engine));
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
static_assert(kMacroShredMono.engine.rowCount == 9u);
static_assert(layout::panelRowsFit(kMacroShredMono.engine));
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

constexpr const auto& kArray = layout::kArrayFamilyLayout;
constexpr auto kArrayTitle = layout::arrayTitleBand(kArray.canvas);
static_assert(kArray.canvas.width == 720.0);
static_assert(kArray.canvas.height == 388.0);
static_assert(kArray.output.frame.y == layout::kStandardMetrics.contentTop);
static_assert(kArray.array.frame.y == layout::kStandardMetrics.contentTop);
static_assert(kArray.output.role == layout::PanelRole::Output);
static_assert(layout::rowY(kArray.output, 0u) == 78.0);
static_assert(layout::rowY(kArray.output, 1u) == 104.0);
static_assert(layout::rowY(kArray.array, 0u) == 78.0);
static_assert(layout::rowY(kArray.editor, 0u) == 170.0);
static_assert(layout::rowY(kArray.editor, 7u) == 352.0);
static_assert(kArray.rowsPerPage == 8u);
static_assert(layout::processorSliderFitsPanel(kArray.output));
static_assert(layout::processorSliderFitsPanel(kArray.array));
static_assert(layout::panelContainsRect(kArray.editor, kArray.channelPlot));
static_assert(layout::panelContainsRect(
    kArray.editor, kArray.channelValueColumn));
static_assert(layout::panelContainsRect(
    kArray.editor, kArray.channelMuteColumn));
static_assert(layout::panelContainsRect(
    kArray.editor, kArray.channelInvertColumn));
static_assert(layout::rectFitsCanvas(kArray.output.frame, kArray.canvas));
static_assert(layout::rectFitsCanvas(kArray.array.frame, kArray.canvas));
static_assert(layout::rectFitsCanvas(kArray.editor.frame, kArray.canvas));
static_assert(layout::processorTitleBandFits(kArrayTitle));

constexpr const auto& kTransform = layout::kTransformFamilyLayout;
constexpr auto kTransformTitle =
    layout::transformTitleBand(kTransform.canvas);
static_assert(kTransform.canvas.width == 820.0);
static_assert(kTransform.canvas.height == 496.0);
static_assert(kTransform.fieldPanel.y
    == layout::kStandardMetrics.contentTop);
static_assert(kTransform.output.frame.y
    == layout::kStandardMetrics.contentTop);
static_assert(layout::rowY(kTransform.output, 0u) == 78.0);
static_assert(layout::rowY(kTransform.output, 1u) == 104.0);
static_assert(layout::rowY(kTransform.primarySeven, 0u) == 170.0);
static_assert(layout::rowY(kTransform.primarySeven, 6u) == 326.0);
static_assert(layout::rowY(kTransform.orderBands, 7u) == 444.0);
static_assert(layout::processorSliderFitsPanel(kTransform.output));
static_assert(layout::processorSliderFitsPanel(kTransform.primarySeven));
static_assert(kTransformTitle.presetLabelX == 286.0);
static_assert(kTransformTitle.presetMenu.x == 348.0);
static_assert(kTransformTitle.saveButton.x == 556.0);
static_assert(layout::panelContainsRect(
    { layout::PluginClass::CompactUtility, layout::PanelRole::Utility,
        kTransform.fieldPanel, 36.0, 26.0, 0u },
    kTransform.fieldPlot));
static_assert(layout::rectFitsCanvas(
    kTransform.fieldPanel, kTransform.canvas));
static_assert(layout::rectFitsCanvas(
    kTransform.orderBands.frame, kTransform.canvas));
static_assert(layout::processorTitleBandFits(kTransformTitle));

constexpr const auto& kMatrix = layout::kMatrixFamilyLayout;
constexpr auto kMatrixTitle = layout::matrixTitleBand(kMatrix.canvas);
static_assert(kMatrix.canvas.width == 1040.0);
static_assert(kMatrix.canvas.height == 648.0);
static_assert(kMatrix.output.frame.y
    == layout::kStandardMetrics.contentTop);
static_assert(layout::rowY(kMatrix.output, 0u) == 78.0);
static_assert(layout::rowY(kMatrix.ambiPattern, 0u) == 144.0);
static_assert(layout::rowY(kMatrix.ambiPattern, 9u) == 378.0);
static_assert(layout::rowY(kMatrix.groupPattern, 10u) == 404.0);
static_assert(layout::processorSliderFitsPanel(kMatrix.output));
static_assert(layout::processorSliderFitsPanel(kMatrix.ambiPattern));
static_assert(layout::processorSliderFitsPanel(kMatrix.groupPattern));
static_assert(layout::rectFitsCanvas(
    kMatrix.matrixPanel, kMatrix.canvas));
static_assert(layout::rectFitsCanvas(
    kMatrix.previewPanel, kMatrix.canvas));
static_assert(layout::rectFitsCanvas(
    kMatrix.glossary, kMatrix.canvas));
static_assert(layout::encoderTitleBandFits(kMatrixTitle));
static_assert(layout::encoderTitleActionRect(
    kMatrixTitle, layout::EncoderTitleAction::Random).x == 632.0);

constexpr const auto& kMixer = layout::kMixerFamilyLayout;
constexpr auto kMixerTitle = layout::mixerTitleBand(kMixer.canvas);
constexpr std::array kMixerPanels {
    kMixer.output, kMixer.busCursor, kMixer.selectedNode
};
constexpr std::array kAmbiMixerPanels {
    kMixer.output, kMixer.ambiBusCursor, kMixer.ambiSelectedNode
};
static_assert(kMixer.canvas.width == 920.0);
static_assert(kMixer.canvas.height == 920.0);
static_assert(kMixer.fieldPanel.y
    == layout::kStandardMetrics.contentTop);
static_assert(kMixer.output.frame.y
    == layout::kStandardMetrics.contentTop);
static_assert(layout::rowY(kMixer.output, 0u) == 78.0);
static_assert(layout::rowY(kMixer.busCursor, 0u) == 144.0);
static_assert(layout::rowY(kMixer.busCursor, 10u) == 404.0);
static_assert(layout::rowY(kMixer.ambiBusCursor, 7u) == 326.0);
static_assert(layout::rowY(kMixer.selectedNode, 10u) == 730.0);
static_assert(layout::rowY(kMixer.ambiSelectedNode, 7u) == 574.0);
static_assert(layout::processorSliderFitsPanel(kMixer.output));
static_assert(layout::processorSliderFitsPanel(kMixer.busCursor));
static_assert(layout::processorSliderFitsPanel(kMixer.ambiBusCursor));
static_assert(layout::processorSliderFitsPanel(kMixer.selectedNode));
static_assert(layout::processorSliderFitsPanel(kMixer.ambiSelectedNode));
static_assert(layout::panelContainsRect(
    { layout::PluginClass::MixerMatrixLane,
        layout::PanelRole::Diagnostics, kMixer.fieldPanel,
        36.0, 26.0, 0u },
    kMixer.fieldPlot));
static_assert(layout::validateColumn(kMixerPanels, kMixer.canvas));
static_assert(layout::validateColumn(kAmbiMixerPanels, kMixer.canvas));
static_assert(layout::processorTitleBandFits(kMixerTitle));

constexpr const auto& kCompactEffect =
    layout::kCompactEffectFamilyLayout;
constexpr auto kCompactEffectOutput =
    layout::compactEffectOutputPanel(2u);
constexpr auto kCompactEffectEngine =
    layout::compactEffectLeftPanel(
        kCompactEffectOutput, layout::PanelRole::Engine, 4u);
constexpr auto kCompactEffectRelationships =
    layout::compactEffectRightPanel(
        layout::PanelRole::Relationships, 5u);
constexpr std::array kCompactEffectLeftPanels {
    kCompactEffectOutput, kCompactEffectEngine
};
constexpr std::array kCompactEffectRightPanels {
    kCompactEffectRelationships
};
constexpr auto kCompactEffectTitle =
    layout::compactEffectTitleBand(kCompactEffect.canvas);
static_assert(kCompactEffect.canvas.width == 760.0);
static_assert(kCompactEffect.canvas.height == 376.0);
static_assert(kCompactEffect.firstColumn.x == 18.0);
static_assert(kCompactEffect.secondColumn.x == 388.0);
static_assert(layout::rowY(kCompactEffectOutput, 0u) == 78.0);
static_assert(layout::rowY(kCompactEffectOutput, 1u) == 104.0);
static_assert(layout::rowY(kCompactEffectEngine, 0u) == 170.0);
static_assert(layout::processorSliderFitsPanel(kCompactEffectOutput));
static_assert(layout::processorSliderFitsPanel(kCompactEffectEngine));
static_assert(layout::processorSliderFitsPanel(
    kCompactEffectRelationships));
static_assert(layout::validateColumn(
    kCompactEffectLeftPanels, kCompactEffect.canvas));
static_assert(layout::validateColumn(
    kCompactEffectRightPanels, kCompactEffect.canvas, false));
static_assert(layout::rolesFollowTemplate(
    kCompactEffectLeftPanels, layout::kCompactEffectTemplate, true));
static_assert(layout::rolesFollowTemplate(
    kCompactEffectRightPanels, layout::kCompactEffectTemplate, false));
static_assert(layout::processorTitleBandFits(kCompactEffectTitle));

constexpr const auto& kAmbiEffect = layout::kAmbiEffectFamilyLayout;
constexpr auto kAmbiEffectDisplacementOutput =
    layout::ambiEffectDisplacementOutputPanel();
constexpr auto kAmbiEffectTitle =
    layout::ambiEffectTitleBand(kAmbiEffect.displacementCanvas);
static_assert(kAmbiEffect.displacementCanvas.width == 920.0);
static_assert(kAmbiEffect.displacementCanvas.height == 820.0);
static_assert(kAmbiEffect.displacementFieldPanel.y
    == layout::kStandardMetrics.contentTop);
static_assert(layout::rowY(kAmbiEffectDisplacementOutput, 0u) == 74.0);
static_assert(layout::processorSliderFitsPanel(
    kAmbiEffectDisplacementOutput));
static_assert(layout::rectFitsCanvas(
    kAmbiEffect.displacementField, kAmbiEffect.displacementCanvas));
static_assert(layout::processorTitleBandFits(kAmbiEffectTitle));

constexpr const auto& kAnalyzer = layout::kAnalyzerFamilyLayout;
constexpr auto kAnalyzerTitle =
    layout::analyzerTitleBand(kAnalyzer.preferredCanvas);
static_assert(kAnalyzer.contentTop
    == layout::kStandardMetrics.contentTop);
static_assert(kAnalyzer.contentTop + kAnalyzer.toolbarHeight
    + kAnalyzer.contentGap == 86.0);
static_assert(layout::processorTitleBandFits(kAnalyzerTitle));

constexpr const auto& kOutputUtility =
    layout::kOutputUtilityFamilyLayout;
constexpr auto kOutputUtilityOutput =
    layout::outputUtilityPanel(
        layout::PanelRole::Output, 42.0, 4u);
constexpr auto kOutputUtilityRouting =
    layout::fittedStackPanel(
        layout::PanelRole::Routing, kOutputUtilityOutput, 5u);
constexpr auto kOutputUtilityEngine =
    layout::fittedStackPanel(
        layout::PanelRole::Engine, kOutputUtilityRouting, 2u);
constexpr std::array kOutputUtilityPanels {
    kOutputUtilityOutput, kOutputUtilityRouting, kOutputUtilityEngine
};
constexpr auto kOutputUtilityTitle =
    layout::outputUtilityTitleBand(kOutputUtility.canvas);
static_assert(kOutputUtility.canvas.width == 920.0);
static_assert(kOutputUtility.canvas.height == 560.0);
static_assert(kOutputUtility.fieldPanel.y
    == layout::kStandardMetrics.contentTop);
static_assert(layout::rowY(kOutputUtilityOutput, 0u) == 78.0);
static_assert(layout::processorSliderFitsPanel(kOutputUtilityOutput));
static_assert(layout::processorSliderFitsPanel(kOutputUtilityRouting));
static_assert(layout::processorSliderFitsPanel(kOutputUtilityEngine));
static_assert(layout::validateColumn(
    kOutputUtilityPanels, kOutputUtility.canvas));
static_assert(layout::rolesFollowTemplate(
    kOutputUtilityPanels, layout::kOutputUtilityTemplate, true));
static_assert(layout::rectFitsCanvas(
    kOutputUtility.field, kOutputUtility.canvas));
static_assert(layout::rectFitsCanvas(
    kOutputUtility.meter, kOutputUtility.canvas));
static_assert(layout::processorTitleBandFits(kOutputUtilityTitle));

constexpr const auto& kImprint = layout::kImprintFamilyLayout;
constexpr auto kImprintOutput =
    layout::imprintPanel(layout::PanelRole::Output, 42.0, 2u);
constexpr layout::Panel kImprintSource {
    layout::PluginClass::EffectProcessor, layout::PanelRole::Source,
    { 616.0, 134.0, 272.0, 126.0 }, 36.0, 26.0, 0u
};
constexpr auto kImprintProcess =
    layout::imprintPanel(layout::PanelRole::Engine, 272.0, 5u);
constexpr std::array kImprintPanels {
    kImprintOutput, kImprintSource, kImprintProcess
};
constexpr auto kImprintTitle =
    layout::imprintTitleBand(kImprint.canvas);
static_assert(kImprint.canvas.width == 900.0);
static_assert(kImprint.fieldPanel.y
    == layout::kStandardMetrics.contentTop);
static_assert(layout::rowY(kImprintOutput, 0u) == 78.0);
static_assert(layout::rowY(kImprintProcess, 0u) == 308.0);
static_assert(layout::processorSliderFitsPanel(kImprintOutput));
static_assert(layout::processorSliderFitsPanel(kImprintProcess));
static_assert(layout::validateColumn(kImprintPanels, kImprint.canvas));
static_assert(layout::rolesFollowTemplate(
    kImprintPanels, layout::kEffectProcessorTemplate, true));
static_assert(layout::rectFitsCanvas(kImprint.field, kImprint.canvas));
static_assert(layout::processorTitleBandFits(kImprintTitle));

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
        layout::PluginClass::CompactEffect,
        layout::PluginClass::AnalyzerMonitor,
        layout::PluginClass::OutputUtility,
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
