#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace s3g::gui_layout {

enum class PluginClass : uint8_t {
    ProceduralEncoder,
    EffectProcessor,
    MacroEffect,
    SpatialPannerDecoder,
    MixerMatrixLane,
    CompactUtility,
    CompactEffect,
    AnalyzerMonitor,
    OutputUtility,
};

enum class PanelRole : uint8_t {
    None,
    Output,
    Engine,
    Source,
    Tuning,
    Envelope,
    ToneShape,
    EventTiming,
    Relationships,
    LaneRelationships,
    Modulation,
    Motion,
    Topology,
    Projection,
    Listener,
    Capture,
    LayoutDecoder,
    SelectedObject,
    Routing,
    Lanes,
    Diagnostics,
    Utility,
};

enum class SharedControlRole : uint8_t {
    OutputLevel,
    AmbisonicOrder,
    TopologyShape,
    TopologyMotion,
    TopologyRate,
    TopologyAmount,
    TopologyDepth,
    TopologyScale,
    TopologyCollapse,
    TopologyTwist,
};

enum class EncoderTitleAction : uint8_t {
    Preset,
    Load,
    Save,
    Random,
};

enum class EncoderControlFamily : uint8_t {
    Engine,
    Tuning,
    Envelope,
    Projection,
    Motion,
    Listener,
    Environment,
};

enum class EncoderFamilyControl : uint8_t {
    Order,
    ModeTrigger,
    VoicesObjects,
    SourceSelection,
    BaseRoot,
    Scale,
    Tune,
    Spread,
    Detune,
    Attack,
    Decay,
    Sustain,
    Release,
    ShapeWindow,
    Azimuth,
    Elevation,
    Distance,
    WidthSpread,
    FollowInertia,
    MotionModeScene,
    ClockSync,
    Rate,
    AmountDepth,
    DeviationChaos,
    AxisSpecific,
    ListenerEnable,
    PickupSet,
    ListeningMode,
    ListenerAmountReturn,
    ListenerResponseBypass,
    EnvironmentPlaceType,
    EnvironmentSize,
    EnvironmentDecay,
    EnvironmentDamping,
    EnvironmentAir,
};

struct EncoderControlOrder {
    EncoderControlFamily family = EncoderControlFamily::Engine;
    std::array<EncoderFamilyControl, 8> controls {};
    uint32_t count = 0u;
};

struct Rect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

struct Canvas {
    double width = 0.0;
    double height = 0.0;
};

constexpr bool rectFitsCanvas(Rect rect, Canvas canvas);

struct Column {
    double x = 0.0;
    double width = 0.0;
    double top = 0.0;
};

struct Metrics {
    double contentTop = 42.0;
    double headerHeight = 21.0;
    double headerLabelInset = 8.0;
    double panelGap = 12.0;
    double firstRowOffset = 36.0;
    double rowPitch = 26.0;
    double compactRowPitch = 24.0;
    double toolboxBottomClearance = 18.0;
    double labelInset = 16.0;
    double controlInset = 108.0;
    double valueInset = 196.0;
    double trackWidth = 82.0;
    double menuWidth = 124.0;
    double panelRightInset = 16.0;
    double processorValueWidth = 42.0;
    double processorValueGap = 8.0;
    double processorTrackWidth = 150.0;
    double hitInset = 8.0;
    double hitHeight = 24.0;
};

struct Panel {
    PluginClass pluginClass = PluginClass::CompactUtility;
    PanelRole role = PanelRole::None;
    Rect frame {};
    double firstRowOffset = 36.0;
    double rowPitch = 26.0;
    uint32_t rowCount = 0u;
};

struct TemplateOrder {
    PluginClass pluginClass = PluginClass::CompactUtility;
    std::array<PanelRole, 12> firstColumn {};
    uint32_t firstColumnCount = 0u;
    std::array<PanelRole, 12> secondColumn {};
    uint32_t secondColumnCount = 0u;
    bool requiresOutputFirst = true;
};

struct PanelAnchor {
    PluginClass pluginClass = PluginClass::CompactUtility;
    PanelRole role = PanelRole::None;
    Column column {};
    double y = 0.0;
};

struct ControlSlot {
    SharedControlRole role = SharedControlRole::OutputLevel;
    PanelAnchor panel {};
    uint32_t row = 0u;
    double y = 0.0;
};

struct EncoderTitleBand {
    Canvas canvas {};
    double titleX = 18.0;
    double titleY = 14.0;
    double presetLabelX = 320.0;
    double controlY = 13.0;
    Rect presetMenu {};
    Rect loadButton {};
    Rect saveButton {};
    Rect randomButton {};
    double statusRightInset = 18.0;
};

struct EnvironmentalFieldHeader {
    double titleSafeWidth = 218.0;
    double pageButtonOffset = 236.0;
    double pageButtonWidth = 45.0;
    double pageButtonGap = 5.0;
    double rightControlsOffset = 410.0;
    double headerInsetY = 4.0;
    double buttonHeight = 13.0;
};

struct PannerFamilyLayout {
    Canvas canvas {};
    Rect mainPanel {};
    Rect field {};
    Panel output {};
    Panel panner {};
    Panel source {};
    Panel designPanner {};
    Panel speaker {};
    double outputRowY = 0.0;
    double pannerFirstRowY = 0.0;
    double sourceFirstRowY = 0.0;
    double speakerFirstRowY = 0.0;
    double menuWidth = 0.0;
    double sliderTrackWidth = 0.0;
};

struct MacroFamilyLayout {
    Canvas canvas {};
    Panel output {};
    Panel delayEngine {};
    Panel pitchEngine {};
    Panel delayRelationships {};
    Panel pitchRelationships {};
    Panel preview {};
};

struct TopologyProcessorColumns {
    double canvasWidth = 0.0;
    double rightInset = 0.0;
    Rect field {};
    Column first {};
    Column second {};
};

struct MacroShredFamilyLayout {
    Canvas canvas {};
    Panel output {};
    Panel engine {};
    Panel relationships {};
    Panel preview {};
    Panel containment {};
    Rect containmentMeter {};
    Rect containmentField {};
    Rect panicButton {};
};

struct ArrayFamilyLayout {
    Canvas canvas {};
    Panel output {};
    Panel array {};
    Panel editor {};
    Rect channelPlot {};
    Rect channelValueColumn {};
    Rect channelMuteColumn {};
    Rect channelInvertColumn {};
    uint32_t rowsPerPage = 0u;
};

struct TransformFamilyLayout {
    Canvas canvas {};
    Rect fieldPanel {};
    Rect fieldPlot {};
    Panel output {};
    Panel primarySeven {};
    Panel primarySix {};
    Panel primaryFour {};
    Panel secondaryFour {};
    Panel weighting {};
    Panel orderBands {};
};

struct MatrixFamilyLayout {
    Canvas canvas {};
    Rect matrixPanel {};
    Rect matrixGrid {};
    Rect previewPanel {};
    Panel output {};
    Panel ambiPattern {};
    Panel groupPattern {};
    Rect glossary {};
};

struct MixerFamilyLayout {
    Canvas canvas {};
    Rect fieldPanel {};
    Rect fieldPlot {};
    Panel output {};
    Panel busCursor {};
    Panel ambiBusCursor {};
    Panel selectedNode {};
    Panel ambiSelectedNode {};
};

struct CompactEffectFamilyLayout {
    Canvas canvas {};
    Column firstColumn {};
    Column secondColumn {};
};

struct AmbiEffectFamilyLayout {
    Canvas displacementCanvas {};
    Rect displacementFieldPanel {};
    Rect displacementField {};
    Column displacementColumn {};
};

struct AnalyzerFamilyLayout {
    Canvas preferredCanvas {};
    double horizontalInset = 18.0;
    double contentTop = 42.0;
    double toolbarHeight = 34.0;
    double contentGap = 10.0;
    double bottomInset = 26.0;
};

struct OutputUtilityFamilyLayout {
    Canvas canvas {};
    Rect fieldPanel {};
    Rect field {};
    Column parameterColumn {};
    Rect meter {};
};

struct ImprintFamilyLayout {
    Canvas canvas {};
    Rect fieldPanel {};
    Rect field {};
    Column parameterColumn {};
};

struct NoInputMixerFamilyLayout {
    Canvas canvas {};
    Rect fieldPanel {};
    Rect fieldPlot {};
    Panel output {};
    Panel network {};
    Panel selectedLane {};
    Panel crosspoint {};
    Panel eq {};
    Panel inserts {};
    Panel movement {};
    Panel containment {};
    Rect containmentMeter {};
    Rect containmentField {};
    Rect panicButton {};
};

inline constexpr Metrics kStandardMetrics {};

inline constexpr EnvironmentalFieldHeader kEnvironmentalFieldHeader {};

constexpr Rect environmentalFieldPageButtonRect(
    Rect fieldPanel, uint32_t index)
{
    return {
        fieldPanel.x + kEnvironmentalFieldHeader.pageButtonOffset
            + static_cast<double>(index)
                * (kEnvironmentalFieldHeader.pageButtonWidth
                    + kEnvironmentalFieldHeader.pageButtonGap),
        fieldPanel.y + kEnvironmentalFieldHeader.headerInsetY,
        kEnvironmentalFieldHeader.pageButtonWidth,
        kEnvironmentalFieldHeader.buttonHeight,
    };
}

constexpr bool environmentalFieldHeaderFits(Rect fieldPanel)
{
    const Rect fieldButton = environmentalFieldPageButtonRect(fieldPanel, 0u);
    const Rect surfButton = environmentalFieldPageButtonRect(fieldPanel, 1u);
    const double titleRight = fieldPanel.x
        + kEnvironmentalFieldHeader.titleSafeWidth;
    const double controlsLeft = fieldPanel.x
        + kEnvironmentalFieldHeader.rightControlsOffset;
    return fieldButton.x >= titleRight + 12.0
        && surfButton.x >= fieldButton.x + fieldButton.width
        && surfButton.x + surfButton.width + 12.0 <= controlsLeft
        && fieldButton.y >= fieldPanel.y
        && fieldButton.y + fieldButton.height
            <= fieldPanel.y + 21.0;
}

constexpr double processorLabelX(double panelX)
{
    return panelX + kStandardMetrics.labelInset;
}

constexpr double processorControlX(double panelX)
{
    return panelX + kStandardMetrics.controlInset;
}

constexpr double processorValueX(double panelX, double panelWidth)
{
    return panelX + panelWidth - kStandardMetrics.panelRightInset
        - kStandardMetrics.processorValueWidth;
}

constexpr double processorTrackWidth(double panelWidth)
{
    const double available = panelWidth
        - kStandardMetrics.controlInset
        - kStandardMetrics.panelRightInset
        - kStandardMetrics.processorValueWidth
        - kStandardMetrics.processorValueGap;
    return available < kStandardMetrics.processorTrackWidth
        ? available : kStandardMetrics.processorTrackWidth;
}

constexpr double processorMenuWidth(double panelWidth)
{
    return panelWidth - kStandardMetrics.controlInset
        - kStandardMetrics.panelRightInset;
}

constexpr bool processorSliderFitsPanel(const Panel& panel)
{
    const double controlX = processorControlX(panel.frame.x);
    const double trackWidth = processorTrackWidth(panel.frame.width);
    const double valueX =
        processorValueX(panel.frame.x, panel.frame.width);
    const double valueRight =
        valueX + kStandardMetrics.processorValueWidth;
    const double panelRight = panel.frame.x + panel.frame.width;
    return processorLabelX(panel.frame.x) >= panel.frame.x
        && controlX >= panel.frame.x
        && trackWidth > 0.0
        && controlX + trackWidth
            + kStandardMetrics.processorValueGap <= valueX
        && valueRight
            <= panelRight - kStandardMetrics.panelRightInset;
}

constexpr bool panelContainsRect(const Panel& panel, Rect rect)
{
    return rect.x >= panel.frame.x
        && rect.y >= panel.frame.y
        && rect.x + rect.width
            <= panel.frame.x + panel.frame.width
        && rect.y + rect.height
            <= panel.frame.y + panel.frame.height;
}

inline constexpr Column kLargeEncoderFirstColumn { 630.0, 250.0, 42.0 };
inline constexpr Column kLargeEncoderSecondColumn { 896.0, 246.0, 42.0 };
inline constexpr TopologyProcessorColumns kTopologyProcessorColumns {
    1356.0,
    12.0,
    { 12.0, 42.0, 620.0, 638.0 },
    { 644.0, 344.0, 42.0 },
    { 1000.0, 344.0, 42.0 },
};
static_assert(
    kTopologyProcessorColumns.field.x
        + kTopologyProcessorColumns.field.width
        + kStandardMetrics.panelGap
        == kTopologyProcessorColumns.first.x,
    "Topology Processor field and first control column must use the standard panel gap.");
static_assert(
    kTopologyProcessorColumns.second.x
        - (kTopologyProcessorColumns.first.x
            + kTopologyProcessorColumns.first.width)
        == kStandardMetrics.panelGap,
    "Topology Processor columns must use the standard panel gap.");
static_assert(
    kTopologyProcessorColumns.second.x
        + kTopologyProcessorColumns.second.width
        + kTopologyProcessorColumns.rightInset
        == kTopologyProcessorColumns.canvasWidth,
    "Topology Processor second column must respect the canvas right inset.");
inline constexpr PanelAnchor kLargeEncoderOutputAnchor {
    PluginClass::ProceduralEncoder, PanelRole::Output,
    kLargeEncoderFirstColumn, 42.0
};
inline constexpr PanelAnchor kLargeEncoderEngineAnchor {
    PluginClass::ProceduralEncoder, PanelRole::Engine,
    kLargeEncoderFirstColumn, 108.0
};
inline constexpr PanelAnchor kLargeEncoderTopologyAnchor {
    PluginClass::ProceduralEncoder, PanelRole::Topology,
    kLargeEncoderSecondColumn, 42.0
};
inline constexpr ControlSlot kLargeEncoderOrderSlot {
    SharedControlRole::AmbisonicOrder, kLargeEncoderOutputAnchor, 1u, 104.0
};

inline constexpr Column kMacroFirstColumn { 18.0, 352.0, 42.0 };
inline constexpr Column kMacroSecondColumn { 388.0, 354.0, 42.0 };

inline constexpr PannerFamilyLayout kPannerFamilyLayout {
    { 900.0, 720.0 },
    { 18.0, 42.0, 596.0, 616.0 },
    { 34.0, 76.0, 564.0, 566.0 },
    { PluginClass::SpatialPannerDecoder, PanelRole::Output,
        { 630.0, 42.0, 250.0, 54.0 }, 36.0, 26.0, 1u },
    { PluginClass::SpatialPannerDecoder, PanelRole::LayoutDecoder,
        { 630.0, 108.0, 250.0, 314.0 }, 36.0, 26.0, 11u },
    { PluginClass::SpatialPannerDecoder, PanelRole::SelectedObject,
        { 630.0, 434.0, 250.0, 158.0 }, 36.0, 26.0, 5u },
    { PluginClass::SpatialPannerDecoder, PanelRole::LayoutDecoder,
        { 630.0, 108.0, 250.0, 150.0 }, 36.0, 26.0, 4u },
    { PluginClass::SpatialPannerDecoder, PanelRole::SelectedObject,
        { 630.0, 270.0, 250.0, 132.0 }, 36.0, 26.0, 4u },
    78.0,
    144.0,
    470.0,
    306.0,
    102.0,
    82.0,
};

inline constexpr MacroFamilyLayout kMacroFamilyLayout {
    { 760.0, 496.0 },
    { PluginClass::MacroEffect, PanelRole::Output,
        { kMacroFirstColumn.x, 42.0, kMacroFirstColumn.width, 80.0 },
        36.0, 26.0, 2u },
    { PluginClass::MacroEffect, PanelRole::Engine,
        { kMacroFirstColumn.x, 134.0, kMacroFirstColumn.width, 158.0 },
        36.0, 26.0, 5u },
    { PluginClass::MacroEffect, PanelRole::Engine,
        { kMacroFirstColumn.x, 134.0, kMacroFirstColumn.width, 106.0 },
        36.0, 26.0, 3u },
    { PluginClass::MacroEffect, PanelRole::Relationships,
        { kMacroFirstColumn.x, 304.0, kMacroFirstColumn.width, 158.0 },
        36.0, 26.0, 5u },
    { PluginClass::MacroEffect, PanelRole::Relationships,
        { kMacroFirstColumn.x, 252.0, kMacroFirstColumn.width, 158.0 },
        36.0, 26.0, 5u },
    { PluginClass::MacroEffect, PanelRole::LaneRelationships,
        { kMacroSecondColumn.x, 42.0, kMacroSecondColumn.width, 436.0 },
        36.0, 26.0, 0u },
};

inline constexpr MacroShredFamilyLayout kMacroShredFamilyLayout {
    { 760.0, 620.0 },
    { PluginClass::MacroEffect, PanelRole::Output,
        { kMacroFirstColumn.x, 42.0, kMacroFirstColumn.width, 80.0 },
        36.0, 26.0, 2u },
    { PluginClass::MacroEffect, PanelRole::Engine,
        { kMacroFirstColumn.x, 134.0, kMacroFirstColumn.width, 236.0 },
        36.0, 23.0, 9u },
    { PluginClass::MacroEffect, PanelRole::Relationships,
        { kMacroFirstColumn.x, 382.0, kMacroFirstColumn.width, 158.0 },
        36.0, 26.0, 5u },
    { PluginClass::MacroEffect, PanelRole::LaneRelationships,
        { kMacroSecondColumn.x, 42.0, kMacroSecondColumn.width, 220.0 },
        36.0, 26.0, 0u },
    { PluginClass::MacroEffect, PanelRole::Utility,
        { kMacroSecondColumn.x, 274.0, kMacroSecondColumn.width, 328.0 },
        36.0, 26.0, 0u },
    { 496.0, 314.0, 228.0, 10.0 },
    { 400.0, 340.0, 330.0, 192.0 },
    { 580.0, 548.0, 144.0, 34.0 },
};

inline constexpr MacroShredFamilyLayout kMacroShredMonoFamilyLayout {
    { 416.0, 498.0 },
    { PluginClass::MacroEffect, PanelRole::Output,
        { 18.0, 68.0, 380.0, 80.0 }, 36.0, 26.0, 2u },
    { PluginClass::MacroEffect, PanelRole::Engine,
        { 18.0, 160.0, 380.0, 236.0 }, 36.0, 23.0, 9u },
    {},
    {},
    { PluginClass::MacroEffect, PanelRole::Utility,
        { 18.0, 408.0, 380.0, 72.0 }, 36.0, 26.0, 0u },
    { 126.0, 446.0, 150.0, 10.0 },
    {},
    { 286.0, 436.0, 96.0, 30.0 },
};

inline constexpr ArrayFamilyLayout kArrayFamilyLayout {
    { 720.0, 388.0 },
    { PluginClass::CompactUtility, PanelRole::Output,
        { 18.0, 42.0, 332.0, 80.0 }, 36.0, 26.0, 2u },
    { PluginClass::CompactUtility, PanelRole::Engine,
        { 362.0, 42.0, 340.0, 54.0 }, 36.0, 26.0, 1u },
    { PluginClass::CompactUtility, PanelRole::Utility,
        { 18.0, 134.0, 684.0, 236.0 }, 36.0, 26.0, 8u },
    { 66.0, 170.0, 462.0, 196.0 },
    { 536.0, 166.0, 74.0, 204.0 },
    { 620.0, 166.0, 28.0, 204.0 },
    { 654.0, 166.0, 38.0, 204.0 },
    8u,
};

inline constexpr TransformFamilyLayout kTransformFamilyLayout {
    { 820.0, 496.0 },
    { 18.0, 42.0, 506.0, 436.0 },
    { 34.0, 78.0, 474.0, 360.0 },
    { PluginClass::CompactUtility, PanelRole::Output,
        { 536.0, 42.0, 266.0, 80.0 }, 36.0, 26.0, 2u },
    { PluginClass::CompactUtility, PanelRole::Engine,
        { 536.0, 134.0, 266.0, 210.0 }, 36.0, 26.0, 7u },
    { PluginClass::CompactUtility, PanelRole::Engine,
        { 536.0, 134.0, 266.0, 184.0 }, 36.0, 26.0, 6u },
    { PluginClass::CompactUtility, PanelRole::Engine,
        { 536.0, 134.0, 266.0, 132.0 }, 36.0, 26.0, 4u },
    { PluginClass::CompactUtility, PanelRole::Utility,
        { 536.0, 278.0, 266.0, 132.0 }, 36.0, 26.0, 4u },
    { PluginClass::CompactUtility, PanelRole::Engine,
        { 536.0, 134.0, 266.0, 80.0 }, 36.0, 26.0, 2u },
    { PluginClass::CompactUtility, PanelRole::Relationships,
        { 536.0, 226.0, 266.0, 236.0 }, 36.0, 26.0, 8u },
};

inline constexpr MatrixFamilyLayout kMatrixFamilyLayout {
    { 1040.0, 648.0 },
    { 18.0, 42.0, 430.0, 440.0 },
    { 72.0, 90.0, 344.0, 344.0 },
    { 466.0, 42.0, 254.0, 440.0 },
    { PluginClass::MixerMatrixLane, PanelRole::Output,
        { 738.0, 42.0, 284.0, 54.0 }, 36.0, 26.0, 1u },
    { PluginClass::MixerMatrixLane, PanelRole::Motion,
        { 738.0, 108.0, 284.0, 288.0 }, 36.0, 26.0, 10u },
    { PluginClass::MixerMatrixLane, PanelRole::Motion,
        { 738.0, 108.0, 284.0, 314.0 }, 36.0, 26.0, 11u },
    { 18.0, 500.0, 1004.0, 132.0 },
};

inline constexpr MixerFamilyLayout kMixerFamilyLayout {
    { 920.0, 920.0 },
    { 18.0, 42.0, 560.0, 860.0 },
    { 34.0, 78.0, 528.0, 528.0 },
    { PluginClass::MixerMatrixLane, PanelRole::Output,
        { 596.0, 42.0, 306.0, 54.0 }, 36.0, 26.0, 1u },
    { PluginClass::MixerMatrixLane, PanelRole::Routing,
        { 596.0, 108.0, 306.0, 314.0 }, 36.0, 26.0, 11u },
    { PluginClass::MixerMatrixLane, PanelRole::Routing,
        { 596.0, 108.0, 306.0, 236.0 }, 36.0, 26.0, 8u },
    { PluginClass::MixerMatrixLane, PanelRole::SelectedObject,
        { 596.0, 434.0, 306.0, 314.0 }, 36.0, 26.0, 11u },
    { PluginClass::MixerMatrixLane, PanelRole::SelectedObject,
        { 596.0, 356.0, 306.0, 236.0 }, 36.0, 26.0, 8u },
};

inline constexpr CompactEffectFamilyLayout kCompactEffectFamilyLayout {
    { 760.0, 376.0 },
    { 18.0, 352.0, 42.0 },
    { 388.0, 354.0, 42.0 },
};

inline constexpr AmbiEffectFamilyLayout kAmbiEffectFamilyLayout {
    { 920.0, 820.0 },
    { 18.0, 42.0, 612.0, 760.0 },
    { 34.0, 76.0, 580.0, 648.0 },
    { 648.0, 258.0, 42.0 },
};

inline constexpr AnalyzerFamilyLayout kAnalyzerFamilyLayout {
    { 980.0, 560.0 },
    18.0,
    42.0,
    34.0,
    10.0,
    26.0,
};

inline constexpr OutputUtilityFamilyLayout kOutputUtilityFamilyLayout {
    { 920.0, 560.0 },
    { 18.0, 42.0, 560.0, 500.0 },
    { 34.0, 78.0, 528.0, 404.0 },
    { 596.0, 306.0, 42.0 },
    { 608.0, 380.0, 282.0, 144.0 },
};

inline constexpr ImprintFamilyLayout kImprintFamilyLayout {
    { 900.0, 480.0 },
    { 18.0, 42.0, 584.0, 420.0 },
    { 34.0, 78.0, 552.0, 366.0 },
    { 616.0, 272.0, 42.0 },
};

inline constexpr NoInputMixerFamilyLayout kNoInputMixerFamilyLayout {
    { 1356.0, 820.0 },
    { 12.0, 42.0, 1332.0, 766.0 },
    { 28.0, 78.0, 1300.0, 714.0 },
    { PluginClass::EffectProcessor, PanelRole::Output,
        { 248.0, 150.0, 392.0, 132.0 }, 36.0, 26.0, 4u },
    { PluginClass::EffectProcessor, PanelRole::Engine,
        { 936.0, 78.0, 392.0, 262.0 }, 36.0, 26.0, 9u },
    { PluginClass::EffectProcessor, PanelRole::SelectedObject,
        { 936.0, 78.0, 392.0, 210.0 }, 36.0, 26.0, 7u },
    { PluginClass::EffectProcessor, PanelRole::Routing,
        { 936.0, 618.0, 392.0, 126.0 }, 36.0, 26.0, 4u },
    { PluginClass::EffectProcessor, PanelRole::ToneShape,
        { 936.0, 300.0, 392.0, 132.0 }, 36.0, 26.0, 4u },
    { PluginClass::EffectProcessor, PanelRole::Engine,
        { 936.0, 444.0, 392.0, 236.0 }, 36.0, 26.0, 8u },
    { PluginClass::EffectProcessor, PanelRole::Motion,
        { 936.0, 346.0, 392.0, 260.0 }, 36.0, 26.0, 9u },
    { PluginClass::EffectProcessor, PanelRole::Diagnostics,
        { 652.0, 150.0, 456.0, 520.0 }, 36.0, 26.0, 0u },
    { 770.0, 224.0, 310.0, 12.0 },
    { 680.0, 260.0, 400.0, 330.0 },
    { 1120.0, 8.0, 90.0, 24.0 },
};

static_assert(kNoInputMixerFamilyLayout.fieldPanel.y
    == kStandardMetrics.contentTop);
static_assert(kNoInputMixerFamilyLayout.output.role == PanelRole::Output);

inline constexpr EncoderControlOrder kEngineControlOrder {
    EncoderControlFamily::Engine,
    { EncoderFamilyControl::Order, EncoderFamilyControl::ModeTrigger,
        EncoderFamilyControl::VoicesObjects,
        EncoderFamilyControl::SourceSelection },
    4u,
};
inline constexpr EncoderControlOrder kTuningControlOrder {
    EncoderControlFamily::Tuning,
    { EncoderFamilyControl::BaseRoot, EncoderFamilyControl::Scale,
        EncoderFamilyControl::Tune, EncoderFamilyControl::Spread,
        EncoderFamilyControl::Detune },
    5u,
};
inline constexpr EncoderControlOrder kEnvelopeControlOrder {
    EncoderControlFamily::Envelope,
    { EncoderFamilyControl::Attack, EncoderFamilyControl::Decay,
        EncoderFamilyControl::Sustain, EncoderFamilyControl::Release,
        EncoderFamilyControl::ShapeWindow },
    5u,
};
inline constexpr EncoderControlOrder kProjectionControlOrder {
    EncoderControlFamily::Projection,
    { EncoderFamilyControl::Azimuth, EncoderFamilyControl::Elevation,
        EncoderFamilyControl::Distance, EncoderFamilyControl::WidthSpread,
        EncoderFamilyControl::FollowInertia },
    5u,
};
inline constexpr EncoderControlOrder kMotionControlOrder {
    EncoderControlFamily::Motion,
    { EncoderFamilyControl::MotionModeScene, EncoderFamilyControl::ClockSync,
        EncoderFamilyControl::Rate, EncoderFamilyControl::AmountDepth,
        EncoderFamilyControl::DeviationChaos,
        EncoderFamilyControl::AxisSpecific },
    6u,
};
inline constexpr EncoderControlOrder kListenerControlOrder {
    EncoderControlFamily::Listener,
    { EncoderFamilyControl::ListenerEnable, EncoderFamilyControl::PickupSet,
        EncoderFamilyControl::ListeningMode,
        EncoderFamilyControl::ListenerAmountReturn,
        EncoderFamilyControl::ListenerResponseBypass },
    5u,
};
inline constexpr EncoderControlOrder kEnvironmentControlOrder {
    EncoderControlFamily::Environment,
    { EncoderFamilyControl::EnvironmentPlaceType,
        EncoderFamilyControl::EnvironmentSize,
        EncoderFamilyControl::EnvironmentDecay,
        EncoderFamilyControl::EnvironmentDamping,
        EncoderFamilyControl::EnvironmentAir },
    5u,
};

constexpr const EncoderControlOrder& encoderControlOrder(
    EncoderControlFamily family)
{
    switch (family) {
    case EncoderControlFamily::Engine: return kEngineControlOrder;
    case EncoderControlFamily::Tuning: return kTuningControlOrder;
    case EncoderControlFamily::Envelope: return kEnvelopeControlOrder;
    case EncoderControlFamily::Projection: return kProjectionControlOrder;
    case EncoderControlFamily::Motion: return kMotionControlOrder;
    case EncoderControlFamily::Listener: return kListenerControlOrder;
    case EncoderControlFamily::Environment: return kEnvironmentControlOrder;
    }
    return kEngineControlOrder;
}

template <size_t Count>
constexpr bool controlsFollowFamilyOrder(
    const std::array<EncoderFamilyControl, Count>& controls,
    const EncoderControlOrder& order)
{
    uint32_t cursor = 0u;
    for (const auto control : controls) {
        while (cursor < order.count && order.controls[cursor] != control) {
            ++cursor;
        }
        if (cursor >= order.count) return false;
        ++cursor;
    }
    return true;
}

constexpr EncoderTitleBand encoderTitleBand(Canvas canvas)
{
    if (canvas.width >= 1100.0) {
        return {
            canvas, 18.0, 14.0, 320.0, 13.0,
            { 382.0, 13.0, 190.0, 15.0 },
            { 580.0, 13.0, 48.0, 15.0 },
            { 636.0, 13.0, 48.0, 15.0 },
            { 692.0, 13.0, 66.0, 15.0 },
            18.0,
        };
    }
    if (canvas.width >= 1000.0) {
        return {
            canvas, 18.0, 14.0, 280.0, 13.0,
            { 342.0, 13.0, 170.0, 15.0 },
            { 520.0, 13.0, 48.0, 15.0 },
            { 576.0, 13.0, 48.0, 15.0 },
            { 632.0, 13.0, 66.0, 15.0 },
            18.0,
        };
    }
    return {
        canvas, 18.0, 14.0, 238.0, 13.0,
        { 300.0, 13.0, 148.0, 15.0 },
        { 456.0, 13.0, 44.0, 15.0 },
        { 508.0, 13.0, 44.0, 15.0 },
        { 560.0, 13.0, 62.0, 15.0 },
        18.0,
    };
}

constexpr EncoderTitleBand macroTitleBand(Canvas canvas)
{
    if (canvas.width < 700.0) {
        return {
            canvas, 18.0, 14.0, 18.0, 40.0,
            { 80.0, 39.0, 148.0, 15.0 },
            { 236.0, 39.0, 48.0, 15.0 },
            { 292.0, 39.0, 48.0, 15.0 },
            {},
            18.0,
        };
    }
    return {
        canvas, 18.0, 14.0, 206.0, 14.0,
        { 268.0, 13.0, 124.0, 15.0 },
        { 400.0, 13.0, 48.0, 15.0 },
        { 456.0, 13.0, 48.0, 15.0 },
        {},
        18.0,
    };
}

constexpr EncoderTitleBand arrayTitleBand(Canvas canvas)
{
    return encoderTitleBand(canvas);
}

constexpr EncoderTitleBand transformTitleBand(Canvas canvas)
{
    if (canvas.width >= 800.0) {
        return {
            canvas, 18.0, 14.0, 286.0, 13.0,
            { 348.0, 13.0, 148.0, 15.0 },
            { 504.0, 13.0, 44.0, 15.0 },
            { 556.0, 13.0, 44.0, 15.0 },
            {},
            18.0,
        };
    }
    return encoderTitleBand(canvas);
}

constexpr EncoderTitleBand matrixTitleBand(Canvas canvas)
{
    return encoderTitleBand(canvas);
}

constexpr EncoderTitleBand mixerTitleBand(Canvas canvas)
{
    return encoderTitleBand(canvas);
}

constexpr EncoderTitleBand compactEffectTitleBand(Canvas canvas)
{
    return encoderTitleBand(canvas);
}

constexpr EncoderTitleBand ambiEffectTitleBand(Canvas canvas)
{
    if (canvas.width >= 860.0) {
        return {
            canvas, 18.0, 14.0, 310.0, 13.0,
            { 372.0, 13.0, 148.0, 15.0 },
            { 528.0, 13.0, 48.0, 15.0 },
            { 584.0, 13.0, 48.0, 15.0 },
            {},
            18.0,
        };
    }
    return encoderTitleBand(canvas);
}

constexpr EncoderTitleBand analyzerTitleBand(Canvas canvas)
{
    return encoderTitleBand(canvas);
}

constexpr EncoderTitleBand outputUtilityTitleBand(Canvas canvas)
{
    return encoderTitleBand(canvas);
}

constexpr EncoderTitleBand imprintTitleBand(Canvas canvas)
{
    return encoderTitleBand(canvas);
}

constexpr Rect encoderTitleActionRect(const EncoderTitleBand& band,
                                      EncoderTitleAction action)
{
    switch (action) {
    case EncoderTitleAction::Preset: return band.presetMenu;
    case EncoderTitleAction::Load: return band.loadButton;
    case EncoderTitleAction::Save: return band.saveButton;
    case EncoderTitleAction::Random: return band.randomButton;
    }
    return {};
}

constexpr bool rectsOverlap(Rect left, Rect right)
{
    return left.x < right.x + right.width
        && left.x + left.width > right.x
        && left.y < right.y + right.height
        && left.y + left.height > right.y;
}

constexpr bool encoderTitleBandFits(const EncoderTitleBand& band)
{
    const Rect controls[] {
        band.presetMenu,
        band.loadButton,
        band.saveButton,
        band.randomButton,
    };
    for (size_t index = 0u; index < 4u; ++index) {
        if (!rectFitsCanvas(controls[index], band.canvas)) return false;
        for (size_t other = index + 1u; other < 4u; ++other) {
            if (rectsOverlap(controls[index], controls[other])) return false;
        }
    }
    return band.presetLabelX < band.presetMenu.x
        && band.presetMenu.y == band.loadButton.y
        && band.loadButton.y == band.saveButton.y
        && band.saveButton.y == band.randomButton.y
        && band.presetMenu.height == band.loadButton.height
        && band.loadButton.height == band.saveButton.height
        && band.saveButton.height == band.randomButton.height;
}

constexpr bool processorTitleBandFits(const EncoderTitleBand& band)
{
    const Rect controls[] {
        band.presetMenu,
        band.loadButton,
        band.saveButton,
    };
    for (size_t index = 0u; index < 3u; ++index) {
        if (!rectFitsCanvas(controls[index], band.canvas)) return false;
        for (size_t other = index + 1u; other < 3u; ++other) {
            if (rectsOverlap(controls[index], controls[other])) return false;
        }
    }
    return band.presetLabelX < band.presetMenu.x
        && band.presetMenu.y == band.loadButton.y
        && band.loadButton.y == band.saveButton.y
        && band.presetMenu.height == band.loadButton.height
        && band.loadButton.height == band.saveButton.height;
}

inline constexpr TemplateOrder kProceduralEncoderTemplate {
    PluginClass::ProceduralEncoder,
    { PanelRole::Output, PanelRole::Engine, PanelRole::Source,
        PanelRole::Tuning, PanelRole::Relationships, PanelRole::EventTiming,
        PanelRole::ToneShape, PanelRole::Envelope, PanelRole::Projection,
        PanelRole::Listener, PanelRole::Capture, PanelRole::Utility },
    12u,
    { PanelRole::Topology, PanelRole::Projection, PanelRole::Motion,
        PanelRole::Engine, PanelRole::Utility, PanelRole::EventTiming,
        PanelRole::Modulation, PanelRole::ToneShape, PanelRole::Listener,
        PanelRole::Capture, PanelRole::Diagnostics, PanelRole::Relationships },
    12u,
    true,
};

inline constexpr TemplateOrder kEffectProcessorTemplate {
    PluginClass::EffectProcessor,
    { PanelRole::Output, PanelRole::Source, PanelRole::Engine,
        PanelRole::Modulation, PanelRole::ToneShape,
        PanelRole::EventTiming, PanelRole::Utility, PanelRole::None },
    7u,
    { PanelRole::Relationships, PanelRole::Routing, PanelRole::Diagnostics,
        PanelRole::Utility, PanelRole::None, PanelRole::None,
        PanelRole::None, PanelRole::None },
    4u,
    true,
};

inline constexpr TemplateOrder kMacroEffectTemplate {
    PluginClass::MacroEffect,
    { PanelRole::Output, PanelRole::Engine, PanelRole::Relationships,
        PanelRole::Utility, PanelRole::None, PanelRole::None,
        PanelRole::None, PanelRole::None },
    4u,
    { PanelRole::LaneRelationships, PanelRole::Utility, PanelRole::None,
        PanelRole::None, PanelRole::None, PanelRole::None,
        PanelRole::None, PanelRole::None },
    2u,
    true,
};

inline constexpr TemplateOrder kSpatialPannerDecoderTemplate {
    PluginClass::SpatialPannerDecoder,
    { PanelRole::Output, PanelRole::LayoutDecoder, PanelRole::SelectedObject,
        PanelRole::Routing, PanelRole::Utility, PanelRole::None,
        PanelRole::None, PanelRole::None },
    5u,
    { PanelRole::Projection, PanelRole::Listener, PanelRole::Diagnostics,
        PanelRole::Utility, PanelRole::None, PanelRole::None,
        PanelRole::None, PanelRole::None },
    4u,
    true,
};

inline constexpr TemplateOrder kMixerMatrixLaneTemplate {
    PluginClass::MixerMatrixLane,
    { PanelRole::Output, PanelRole::Routing, PanelRole::Lanes,
        PanelRole::Diagnostics, PanelRole::Utility, PanelRole::None,
        PanelRole::None, PanelRole::None },
    5u,
    { PanelRole::SelectedObject, PanelRole::Diagnostics, PanelRole::Utility,
        PanelRole::None, PanelRole::None, PanelRole::None,
        PanelRole::None, PanelRole::None },
    3u,
    true,
};

inline constexpr TemplateOrder kCompactUtilityTemplate {
    PluginClass::CompactUtility,
    { PanelRole::Output, PanelRole::Engine, PanelRole::Utility,
        PanelRole::None, PanelRole::None, PanelRole::None,
        PanelRole::None, PanelRole::None },
    3u,
    {},
    0u,
    true,
};

inline constexpr TemplateOrder kCompactEffectTemplate {
    PluginClass::CompactEffect,
    { PanelRole::Output, PanelRole::Engine, PanelRole::ToneShape,
        PanelRole::EventTiming, PanelRole::Utility, PanelRole::None,
        PanelRole::None, PanelRole::None },
    5u,
    { PanelRole::Motion, PanelRole::Projection, PanelRole::Relationships,
        PanelRole::Diagnostics, PanelRole::Utility, PanelRole::None,
        PanelRole::None, PanelRole::None },
    5u,
    true,
};

inline constexpr TemplateOrder kAnalyzerMonitorTemplate {
    PluginClass::AnalyzerMonitor,
    { PanelRole::Diagnostics, PanelRole::Utility, PanelRole::None,
        PanelRole::None, PanelRole::None, PanelRole::None,
        PanelRole::None, PanelRole::None },
    2u,
    {},
    0u,
    false,
};

inline constexpr TemplateOrder kOutputUtilityTemplate {
    PluginClass::OutputUtility,
    { PanelRole::Output, PanelRole::Routing, PanelRole::Engine,
        PanelRole::LayoutDecoder, PanelRole::Diagnostics,
        PanelRole::Utility, PanelRole::None, PanelRole::None },
    6u,
    {},
    0u,
    true,
};

constexpr const TemplateOrder& templateOrder(PluginClass pluginClass)
{
    switch (pluginClass) {
    case PluginClass::ProceduralEncoder: return kProceduralEncoderTemplate;
    case PluginClass::EffectProcessor: return kEffectProcessorTemplate;
    case PluginClass::MacroEffect: return kMacroEffectTemplate;
    case PluginClass::SpatialPannerDecoder: return kSpatialPannerDecoderTemplate;
    case PluginClass::MixerMatrixLane: return kMixerMatrixLaneTemplate;
    case PluginClass::CompactUtility: return kCompactUtilityTemplate;
    case PluginClass::CompactEffect: return kCompactEffectTemplate;
    case PluginClass::AnalyzerMonitor: return kAnalyzerMonitorTemplate;
    case PluginClass::OutputUtility: return kOutputUtilityTemplate;
    }
    return kCompactUtilityTemplate;
}

constexpr Panel makePanel(PluginClass pluginClass,
                          PanelRole role,
                          Column column,
                          double y,
                          double height,
                          uint32_t rowCount,
                          double rowPitch = kStandardMetrics.rowPitch,
                          double firstRowOffset = kStandardMetrics.firstRowOffset)
{
    return {
        pluginClass,
        role,
        { column.x, y, column.width, height },
        firstRowOffset,
        rowPitch,
        rowCount,
    };
}

constexpr double toolboxFirstRowY(
    double panelY,
    double firstRowOffset = kStandardMetrics.firstRowOffset)
{
    return panelY + firstRowOffset;
}

constexpr double toolboxRowY(
    double panelY,
    uint32_t visibleRow,
    double rowPitch = kStandardMetrics.rowPitch,
    double firstRowOffset = kStandardMetrics.firstRowOffset)
{
    return toolboxFirstRowY(panelY, firstRowOffset)
        + static_cast<double>(visibleRow) * rowPitch;
}

constexpr double toolboxHeightForRows(
    uint32_t visibleRowCount,
    double bottomClearance = kStandardMetrics.toolboxBottomClearance,
    double rowPitch = kStandardMetrics.rowPitch,
    double firstRowOffset = kStandardMetrics.firstRowOffset)
{
    if (visibleRowCount == 0u) return 0.0;
    return firstRowOffset
        + static_cast<double>(visibleRowCount - 1u) * rowPitch
        + bottomClearance;
}

constexpr uint32_t compactedVisibleRow(
    const bool* visibleRows,
    uint32_t rowCount,
    uint32_t sourceRow)
{
    uint32_t visibleRow = 0u;
    const uint32_t limit = sourceRow < rowCount ? sourceRow : rowCount;
    for (uint32_t row = 0u; row < limit; ++row) {
        if (visibleRows[row]) ++visibleRow;
    }
    return visibleRow;
}

constexpr Panel stackPanel(PanelRole role,
                           const Panel& previous,
                           double height,
                           uint32_t rowCount,
                           double rowPitch = kStandardMetrics.rowPitch,
                           double firstRowOffset = kStandardMetrics.firstRowOffset,
                           double panelGap = kStandardMetrics.panelGap)
{
    return makePanel(previous.pluginClass, role,
        { previous.frame.x, previous.frame.width,
            previous.frame.y + previous.frame.height + panelGap },
        previous.frame.y + previous.frame.height + panelGap,
        height, rowCount, rowPitch, firstRowOffset);
}

constexpr Panel fittedPanel(PluginClass pluginClass,
                            PanelRole role,
                            Column column,
                            double y,
                            uint32_t rowCount,
                            double rowPitch = kStandardMetrics.rowPitch,
                            double firstRowOffset = kStandardMetrics.firstRowOffset)
{
    return makePanel(pluginClass, role, column, y,
        toolboxHeightForRows(rowCount, kStandardMetrics.toolboxBottomClearance,
            rowPitch, firstRowOffset),
        rowCount, rowPitch, firstRowOffset);
}

constexpr Panel fittedStackPanel(PanelRole role,
                                 const Panel& previous,
                                 uint32_t rowCount,
                                 double rowPitch = kStandardMetrics.rowPitch,
                                 double firstRowOffset = kStandardMetrics.firstRowOffset,
                                 double panelGap = kStandardMetrics.panelGap)
{
    return stackPanel(role, previous,
        toolboxHeightForRows(rowCount, kStandardMetrics.toolboxBottomClearance,
            rowPitch, firstRowOffset),
        rowCount, rowPitch, firstRowOffset, panelGap);
}

constexpr Panel compactEffectOutputPanel(uint32_t rowCount)
{
    return fittedPanel(PluginClass::CompactEffect, PanelRole::Output,
        kCompactEffectFamilyLayout.firstColumn,
        kCompactEffectFamilyLayout.firstColumn.top, rowCount);
}

constexpr Panel compactEffectLeftPanel(const Panel& previous,
                                       PanelRole role,
                                       uint32_t rowCount)
{
    return fittedStackPanel(role, previous, rowCount);
}

constexpr Panel compactEffectRightPanel(PanelRole role, uint32_t rowCount)
{
    return fittedPanel(PluginClass::CompactEffect, role,
        kCompactEffectFamilyLayout.secondColumn,
        kCompactEffectFamilyLayout.secondColumn.top, rowCount);
}

constexpr Panel ambiEffectDisplacementOutputPanel()
{
    return fittedPanel(PluginClass::EffectProcessor, PanelRole::Output,
        kAmbiEffectFamilyLayout.displacementColumn,
        kAmbiEffectFamilyLayout.displacementColumn.top, 5u, 24.0, 32.0);
}

constexpr Panel outputUtilityPanel(PanelRole role,
                                   double y,
                                   uint32_t rowCount)
{
    return fittedPanel(PluginClass::OutputUtility, role,
        kOutputUtilityFamilyLayout.parameterColumn, y, rowCount);
}

constexpr Rect analyzerToolbarRect(Canvas canvas)
{
    return {
        kAnalyzerFamilyLayout.horizontalInset,
        kAnalyzerFamilyLayout.contentTop,
        canvas.width - kAnalyzerFamilyLayout.horizontalInset * 2.0,
        kAnalyzerFamilyLayout.toolbarHeight,
    };
}

constexpr Rect analyzerContentRect(Canvas canvas)
{
    const double y = kAnalyzerFamilyLayout.contentTop
        + kAnalyzerFamilyLayout.toolbarHeight
        + kAnalyzerFamilyLayout.contentGap;
    return {
        kAnalyzerFamilyLayout.horizontalInset,
        y,
        canvas.width - kAnalyzerFamilyLayout.horizontalInset * 2.0,
        canvas.height - y - kAnalyzerFamilyLayout.bottomInset,
    };
}

constexpr Panel imprintPanel(PanelRole role,
                             double y,
                             uint32_t rowCount)
{
    return fittedPanel(PluginClass::EffectProcessor, role,
        kImprintFamilyLayout.parameterColumn, y, rowCount);
}

constexpr double rowY(const Panel& panel, uint32_t row)
{
    return toolboxRowY(
        panel.frame.y, row, panel.rowPitch, panel.firstRowOffset);
}

constexpr bool sameColumn(Column left, Column right)
{
    return left.x == right.x && left.width == right.width;
}

constexpr bool panelMatchesAnchor(const Panel& panel, const PanelAnchor& anchor)
{
    return panel.pluginClass == anchor.pluginClass
        && panel.role == anchor.role
        && sameColumn({ panel.frame.x, panel.frame.width, panel.frame.y }, anchor.column)
        && panel.frame.y == anchor.y;
}

constexpr bool controlMatchesSlot(const Panel& panel,
                                  const ControlSlot& slot)
{
    return panelMatchesAnchor(panel, slot.panel)
        && slot.row < panel.rowCount
        && rowY(panel, slot.row) == slot.y;
}

constexpr uint32_t topologyRow(SharedControlRole role)
{
    switch (role) {
    case SharedControlRole::TopologyShape: return 0u;
    case SharedControlRole::TopologyMotion: return 1u;
    case SharedControlRole::TopologyRate: return 2u;
    case SharedControlRole::TopologyAmount: return 3u;
    case SharedControlRole::TopologyDepth: return 4u;
    case SharedControlRole::TopologyScale: return 5u;
    case SharedControlRole::TopologyCollapse: return 6u;
    case SharedControlRole::TopologyTwist: return 7u;
    default: return 0u;
    }
}

constexpr bool topologyControlMatches(const Panel& panel,
                                      SharedControlRole role)
{
    const uint32_t row = topologyRow(role);
    return panelMatchesAnchor(panel, kLargeEncoderTopologyAnchor)
        && row < panel.rowCount
        && rowY(panel, row)
            == 78.0 + static_cast<double>(row) * kStandardMetrics.rowPitch;
}

constexpr Rect panelRect(const Panel& panel)
{
    return panel.frame;
}

constexpr Rect menuBoxRect(const Panel& panel,
                           uint32_t row,
                           const Metrics& metrics = kStandardMetrics)
{
    return {
        panel.frame.x + metrics.controlInset,
        rowY(panel, row) - 1.0,
        metrics.menuWidth,
        15.0,
    };
}

constexpr Rect pannerMenuBoxRect(const Panel& panel, uint32_t row)
{
    auto rect = menuBoxRect(panel, row);
    rect.width = kPannerFamilyLayout.menuWidth;
    return rect;
}

constexpr Rect sliderHitRect(const Panel& panel,
                             uint32_t row,
                             const Metrics& metrics = kStandardMetrics)
{
    return {
        panel.frame.x + metrics.hitInset,
        rowY(panel, row) - 8.0,
        panel.frame.width - metrics.hitInset * 2.0,
        metrics.hitHeight,
    };
}

constexpr bool rectFitsCanvas(Rect rect, Canvas canvas)
{
    return rect.x >= 0.0 && rect.y >= 0.0
        && rect.width >= 0.0 && rect.height >= 0.0
        && rect.x + rect.width <= canvas.width
        && rect.y + rect.height <= canvas.height;
}

constexpr bool panelRowsFit(const Panel& panel)
{
    if (panel.rowCount == 0u) return true;
    const double firstTop = rowY(panel, 0u) - 2.0;
    const double lastBottom = rowY(panel, panel.rowCount - 1u) + 14.0;
    return firstTop >= panel.frame.y + kStandardMetrics.headerHeight
        && lastBottom <= panel.frame.y + panel.frame.height;
}

template <size_t Count>
constexpr bool validateColumn(const std::array<Panel, Count>& panels,
                              Canvas canvas,
                              bool requireOutputFirst = true,
                              double minimumGap = kStandardMetrics.panelGap)
{
    if constexpr (Count == 0u) return true;
    if (requireOutputFirst && panels[0].role != PanelRole::Output) return false;
    for (size_t index = 0u; index < Count; ++index) {
        const auto& panel = panels[index];
        if (!rectFitsCanvas(panel.frame, canvas) || !panelRowsFit(panel)) return false;
        if (index == 0u) continue;
        const auto& previous = panels[index - 1u];
        if (panel.frame.x != previous.frame.x
            || panel.frame.width != previous.frame.width
            || panel.frame.y < previous.frame.y + previous.frame.height + minimumGap) {
            return false;
        }
    }
    return true;
}

template <size_t Count>
constexpr bool roleMatchesAnchorIfPresent(
    const std::array<Panel, Count>& panels,
    PanelRole role,
    const PanelAnchor& anchor)
{
    for (const auto& panel : panels) {
        if (panel.role == role) return panelMatchesAnchor(panel, anchor);
    }
    return true;
}

template <size_t Count>
constexpr bool rolesFollowTemplate(const std::array<Panel, Count>& panels,
                                   const TemplateOrder& order,
                                   bool firstColumn)
{
    const auto& expected = firstColumn ? order.firstColumn : order.secondColumn;
    const uint32_t expectedCount =
        firstColumn ? order.firstColumnCount : order.secondColumnCount;
    uint32_t cursor = 0u;
    for (const auto& panel : panels) {
        while (cursor < expectedCount && expected[cursor] != panel.role) ++cursor;
        if (cursor >= expectedCount) return false;
        ++cursor;
    }
    return true;
}

} // namespace s3g::gui_layout
