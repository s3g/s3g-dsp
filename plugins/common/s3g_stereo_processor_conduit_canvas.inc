// Literal Cocoa adaptation: preserve layout, drawing order and hit maps.
namespace stereo_processor_canvas {
namespace drawing = s3g::portable_gui::stereo_drawing;
namespace foundation = s3g::portable_gui::foundation;
constexpr uint32_t kContainmentHistorySize = 160u;
constexpr int kMaterialMenuId = 1;
constexpr int kPedalMenuId = 2;
constexpr int kPedalPositionMenuId = 3;
constexpr int kFactoryPresetMenuId = 4;
constexpr int kInputListenMenuId = 5;

constexpr s3g::gui_layout::MacroShredFamilyLayout conduitFamilyLayout()
{
    auto layout = s3g::gui_layout::kMacroShredFamilyLayout;
    layout.engine.frame.height += 26.0;
    layout.engine.rowCount = 10u;
    layout.relationships.frame.y += 26.0;
    return layout;
}

inline constexpr auto kConduitFamilyLayout = conduitFamilyLayout();

static s3g::gui_layout::EncoderTitleBand conduitTitleBand()
{
    auto band = s3g::gui_layout::macroTitleBand(
        kConduitFamilyLayout.canvas);
    band.randomButton = { 512.0, 13.0, 66.0, 15.0 };
    return band;
}


static VSTGUI::CColor conduitColor(int rgb) { return drawing::calibratedColor(rgb); }

class Editor final : public s3g::portable_gui::routing::Canvas {
public:
s3g::ProcessorConduitParams _snapshot {};

using Canvas = s3g::portable_gui::routing::Canvas;

    Plugin* _plugin;
    int _dragSlider;
    int _openMenu;
    int _factoryPresetIndex;
    float _activityHistory[kContainmentHistorySize];
    float _reductionHistory[kContainmentHistorySize];
    uint32_t _historyWrite;
    char _titlePresetName[64];
#include "s3g_stereo_processor_editor.inc"
explicit Editor(Plugin& plugin) : Canvas(kGuiWidth,kGuiHeight) {_plugin = &plugin;
_snapshot=portableConduitSnapshot(plugin);
        _dragSlider = -1;
        _openMenu = 0;
        _factoryPresetIndex = 0;
        std::fill_n(_activityHistory, kContainmentHistorySize, 0.0f);
        std::fill_n(_reductionHistory, kContainmentHistorySize, 0.0f);
        _historyWrite = 0u;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            s3g::processorConduitFactoryPresetInfo(0u).name);
}

void applyFactoryPreset(int index)  {
    if (!_plugin) return;
    index = std::clamp(index, 0,
        static_cast<int>(s3g::kProcessorConduitFactoryPresetCount - 1u));
    editConduitParams( s3g::processorConduitFactoryPreset(
        static_cast<uint32_t>(index), _snapshot));
    _factoryPresetIndex = index;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
        s3g::processorConduitFactoryPresetInfo(
            static_cast<uint32_t>(index)).name);

}

void service() override {
_snapshot=portableConduitSnapshot(*_plugin);
    
    if (true && _plugin
        && true) {
        const float activity = std::clamp(
            _plugin->materialActivity.load(std::memory_order_relaxed),
            0.0f, 1.0f);
        const float activityDb = 20.0f * std::log10(
            std::max(activity, 0.000001f));
        _activityHistory[_historyWrite] = std::clamp(
            (activityDb + 60.0f) / 60.0f, 0.0f, 1.0f);
        _reductionHistory[_historyWrite] = std::clamp(
            _plugin->governorReduction.load(std::memory_order_relaxed),
            0.0f, 1.0f);
        _historyWrite = (_historyWrite + 1u) % kContainmentHistorySize;
        setNeedsDisplay(true);
    }

}

void captureDocumentationHistorySample()  {
    if (!_plugin) return;
    const float activity = std::clamp(
        _plugin->materialActivity.load(std::memory_order_relaxed),
        0.0f, 1.0f);
    const float activityDb = 20.0f * std::log10(
        std::max(activity, 0.000001f));
    _activityHistory[_historyWrite] = std::clamp(
        (activityDb + 60.0f) / 60.0f, 0.0f, 1.0f);
    _reductionHistory[_historyWrite] = std::clamp(
        _plugin->governorReduction.load(std::memory_order_relaxed),
        0.0f, 1.0f);
    _historyWrite = (_historyWrite + 1u) % kContainmentHistorySize;
    setNeedsDisplay(true);

}

void drawSlider_value_norm_y_panel_attrs(drawing::String  name, drawing::String  value, double norm, double y, const s3g::gui_layout::Panel& panel, drawing::Attrs  attrs)  {
    drawing::Style style;
    drawing::drawProcessorSlider(name, value, norm, y,
        panel.frame.x, panel.frame.width, attrs, attrs, style);

}

void drawContainmentHistory_attrs_style(drawing::Rect field, drawing::Attrs  attrs, const drawing::Style& style)  {
    drawing::setFill(style.strip);
    drawing::rectFill(field);
    drawing::setStroke(style.grid);
    drawing::frameRect(field);

    const float activity = std::clamp(
        _plugin->materialActivity.load(std::memory_order_relaxed),
        0.0f, 1.0f);
    const float activityDb = 20.0f * std::log10(
        std::max(activity, 0.000001f));
    const float reduction = std::clamp(
        _plugin->governorReduction.load(std::memory_order_relaxed),
        0.0f, 1.0f);
    drawing::String  summary = activity > 0.000001f
        ? drawing::stringFormat("ENERGY %+.0f DB   CUT %.0f%%",
            activityDb, reduction * 100.0f)
        : drawing::stringFormat("ENERGY -INF DB   CUT %.0f%%",
            reduction * 100.0f);
    drawing::drawAtPoint_withAttributes(summary, drawing::makePoint(field.origin.x + 10.0,
        field.origin.y + 7.0), attrs);

    const double labelWidth = 38.0;
    const drawing::Rect energyPlot = drawing::makeRect(
        field.origin.x + labelWidth, field.origin.y + 29.0,
        field.size.width - labelWidth - 10.0, 55.0);
    const drawing::Rect reductionPlot = drawing::makeRect(
        energyPlot.origin.x, field.origin.y + 95.0,
        energyPlot.size.width, 55.0);
    const auto drawPlotFrame = [&](drawing::Rect plot) {
        drawing::setFill(style.bg);
        drawing::rectFill(plot);
        drawing::setStroke(style.grid);
        drawing::frameRect(plot);
        drawing::setFill(style.grid);
        drawing::rectFill(drawing::makeRect(plot.origin.x,
            std::floor(drawing::midY(plot)), plot.size.width, 1.0));
        for (uint32_t division = 1u; division < 4u; ++division) {
            const double x = plot.origin.x + plot.size.width
                * static_cast<double>(division) / 4.0;
            drawing::rectFill(drawing::makeRect(std::floor(x), plot.origin.y,
                1.0, plot.size.height));
        }
    };
    drawPlotFrame(energyPlot);
    drawPlotFrame(reductionPlot);

    drawing::drawAtPoint_withAttributes("ENG", drawing::makePoint(field.origin.x + 10.0,
        energyPlot.origin.y + 20.0), attrs);
    drawing::drawAtPoint_withAttributes("CUT", drawing::makePoint(field.origin.x + 10.0,
        reductionPlot.origin.y + 20.0), attrs);

    const auto drawHistory = [&](drawing::Rect plot, const float* history,
        VSTGUI::CColor  color, double width) {
        drawing::Path  path = drawing::Path::bezierPath();
        path.setLineWidth(width);
        for (uint32_t sample = 0u;
             sample < kContainmentHistorySize; ++sample) {
            const uint32_t index = (_historyWrite + sample)
                % kContainmentHistorySize;
            const double normalized = std::clamp<double>(
                history[index], 0.0, 1.0);
            const double x = plot.origin.x + 2.0
                + (plot.size.width - 4.0)
                    * static_cast<double>(sample)
                    / static_cast<double>(kContainmentHistorySize - 1u);
            const double y = drawing::maxY(plot) - 2.0
                - normalized * (plot.size.height - 4.0);
            if (sample == 0u) {
                path.moveToPoint(drawing::makePoint(x, y));
            } else {
                path.lineToPoint(drawing::makePoint(x, y));
            }
        }
        drawing::setStroke(color);
        path.stroke();
    };
    drawHistory(energyPlot, _activityHistory, style.text, 1.5);
    drawHistory(reductionPlot, _reductionHistory, style.fill, 2.0);

    drawing::String  state = activityDb < -54.0f && reduction < 0.01f
        ? "QUIET" : (reduction >= 0.12f
            ? "MASKING" : (reduction >= 0.01f ? "TRIMMING" : "OPEN"));
    drawing::String  footer = drawing::stringFormat("8S HISTORY   %@   BODY %.0f HZ", state,
        _plugin->fundamentalHz.load(std::memory_order_relaxed));
    drawing::drawAtPoint_withAttributes(footer, drawing::makePoint(field.origin.x + 10.0,
        drawing::maxY(field) - 24.0), attrs);

}

void drawOriginal(drawing::Rect dirty)  {
    (void)dirty;
    drawing::Style style;
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs  label = drawing::softLabelAttrs();
    drawing::Attrs  small = drawing::softValueAttrs();
    const auto& family = kConduitFamilyLayout;
    const auto titleBand = conduitTitleBand();
    drawing::drawMacroTitleBand(
        "s3g PROCESSOR CONDUIT",
        drawing::stringFromUtf8(_titlePresetName),
        drawing::peakDbText(
            _plugin->outputPeak.load(std::memory_order_relaxed)),
        titleBand, style);
    drawing::drawHeaderActionButton(
        drawing::cocoaRect(titleBand.randomButton),
        drawing::cocoaRect(titleBand.randomButton),
        "RANDOM", label, style);

    const auto drawPanel = [&](drawing::String  name,
        const s3g::gui_layout::Panel& panel) {
        drawing::drawPanelFrame(panel.frame.x, panel.frame.y,
            panel.frame.width, panel.frame.height, style);
        drawing::drawPanelHeader(name, true, panel.frame.x,
            panel.frame.y, panel.frame.width,
            s3g::gui_layout::kStandardMetrics.headerHeight, label, style);
    };
    drawPanel("OUTPUT", family.output);
    drawPanel("VIRTUAL MATERIAL CHANNEL", family.engine);
    drawPanel("PEDAL", family.relationships);
    drawPanel("CONTAINMENT HISTORY", family.containment);
    drawPanel("PA / TIME / SPACE", family.preview);

    const auto& prm = _snapshot;
    drawSlider_value_norm_y_panel_attrs("OUT", drawing::stringFormat("%+.1f dB", prm.outputGainDb), (prm.outputGainDb + 60.0f) / 66.0f, s3g::gui_layout::rowY(family.output, 0u), family.output, small);
    drawSlider_value_norm_y_panel_attrs("MIX", drawing::stringFormat("%.0f%%", prm.mix * 100.0f), prm.mix, s3g::gui_layout::rowY(family.output, 1u), family.output, small);
    drawing::drawProcessorMenu("MATERIAL",
        drawing::stringFromUtf8(s3g::processorConduitMaterialName(prm.material)),
        s3g::gui_layout::rowY(family.engine, 0u),
        family.engine.frame.x, family.engine.frame.width,
        label, small, style);
    drawing::drawProcessorMenu("INPUT LISTEN",
        drawing::stringFromUtf8(s3g::processorConduitInputListenName(prm.inputListen)),
        s3g::gui_layout::rowY(family.engine, 1u),
        family.engine.frame.x, family.engine.frame.width,
        label, small, style);

    const std::array<drawing::String , 8u> names {
        "INPUT", "DRIVER", "SIZE", "TENSION",
        "DAMPING", "PICKUP", "CONTACT", "FEEDBACK"
    };
    const std::array<float, 8u> values {
        (prm.inputGainDb + 24.0f) / 60.0f, prm.driver, prm.size,
        prm.tension, prm.damping, prm.pickup, prm.contact, prm.feedback
    };
    for (uint32_t i = 0u; i < names.size(); ++i) {
        drawing::String  value = i == 0u
            ? drawing::stringFormat("%+.1f dB", prm.inputGainDb)
            : drawing::stringFormat("%.0f%%", values[i] * 100.0f);
        drawSlider_value_norm_y_panel_attrs(names[i], value, values[i], s3g::gui_layout::rowY(family.engine, i + 2u), family.engine, small);
    }

    drawing::drawProcessorMenu("PEDAL",
        drawing::stringFromUtf8(s3g::processorConduitPedalName(prm.pedal)),
        s3g::gui_layout::rowY(family.relationships, 0u),
        family.relationships.frame.x, family.relationships.frame.width,
        label, small, style);
    drawing::drawProcessorMenu("POSITION",
        drawing::stringFromUtf8(s3g::processorConduitPedalPositionName(prm.pedalPosition)),
        s3g::gui_layout::rowY(family.relationships, 1u),
        family.relationships.frame.x, family.relationships.frame.width,
        label, small, style);
    drawSlider_value_norm_y_panel_attrs("MIX", drawing::stringFormat("%.0f%%", prm.pedalMix * 100.0f), prm.pedalMix, s3g::gui_layout::rowY(family.relationships, 2u), family.relationships, small);
    drawSlider_value_norm_y_panel_attrs("DRIVE", drawing::stringFormat("%.0f%%", prm.pedalDrive * 100.0f), prm.pedalDrive, s3g::gui_layout::rowY(family.relationships, 3u), family.relationships, small);
    drawSlider_value_norm_y_panel_attrs("TONE", drawing::stringFormat("%.0f%%", prm.pedalTone * 100.0f), prm.pedalTone, s3g::gui_layout::rowY(family.relationships, 4u), family.relationships, small);

    drawSlider_value_norm_y_panel_attrs("OCT DOWN", drawing::stringFormat("%.0f%%", prm.octaveDown * 100.0f), prm.octaveDown, s3g::gui_layout::rowY(family.preview, 0u), family.preview, small);
    drawSlider_value_norm_y_panel_attrs("DRAG", drawing::stringFormat("%.0f%%", prm.octaveDrag * 100.0f), prm.octaveDrag, s3g::gui_layout::rowY(family.preview, 1u), family.preview, small);
    drawSlider_value_norm_y_panel_attrs("PA DRIVE", drawing::stringFormat("%.0f%%", prm.paDrive * 100.0f), prm.paDrive, s3g::gui_layout::rowY(family.preview, 2u), family.preview, small);
    drawSlider_value_norm_y_panel_attrs("THROW", drawing::stringFormat("%.0f%%", prm.micMotion * 100.0f), prm.micMotion, s3g::gui_layout::rowY(family.preview, 3u), family.preview, small);
    drawSlider_value_norm_y_panel_attrs("CHAMBER", drawing::stringFormat("%.0f%%", prm.chamber * 100.0f), prm.chamber, s3g::gui_layout::rowY(family.preview, 4u), family.preview, small);
    drawSlider_value_norm_y_panel_attrs("WIDTH", drawing::stringFormat("%.0f%%", prm.stereoWidth * 100.0f), prm.stereoWidth, s3g::gui_layout::rowY(family.preview, 5u), family.preview, small);
    drawing::String  timeText = drawing::stringFormat("%@  PATH %.1fMS  CUT %.0f%%  BREAK %.0f%%  MIC %+.2f",
        _plugin->audioConfigId == kStereoInputConfigId
            ? drawing::stringFromUtf8(s3g::processorConduitInputListenName(prm.inputListen))
            : "MONO PORT",
        _plugin->propagationMs.load(std::memory_order_relaxed),
        _plugin->governorReduction.load(std::memory_order_relaxed) * 100.0f,
        _plugin->breakupActivity.load(std::memory_order_relaxed) * 100.0f,
        _plugin->micPosition.load(std::memory_order_relaxed));
    drawing::drawAtPoint_withAttributes(timeText, drawing::makePoint(family.preview.frame.x + 14.0,
        family.preview.frame.y + family.preview.frame.height - 21.0), small);

    const float materialActivity = std::clamp(
        _plugin->materialActivity.load(std::memory_order_relaxed),
        0.0f, 1.0f);
    const float materialDb = 20.0f * std::log10(
        std::max(materialActivity, 0.000001f));
    drawing::drawAtPoint_withAttributes("BODY", drawing::makePoint(family.containment.frame.x + 16.0,
        family.containmentMeter.y - 2.0), label);

    drawing::Rect governorField =
        drawing::cocoaRect(family.containmentField);
    drawContainmentHistory_attrs_style(governorField, small, style);

    drawing::Rect activityTrack =
        drawing::cocoaRect(family.containmentMeter);
    drawing::setFill(conduitColor(0x111111));
    drawing::rectFill(activityTrack);
    drawing::setStroke(conduitColor(0x444444));
    drawing::frameRect(activityTrack);
    drawing::Rect activityFill = drawing::inset(activityTrack, 1.0, 1.0);
    const double activityDisplay = std::clamp<double>(
        (materialDb + 60.0f) / 60.0f, 0.0, 1.0);
    activityFill.size.width *= activityDisplay;
    drawing::setFill(conduitColor(0xb8b8b8));
    drawing::rectFill(activityFill);

    const drawing::Rect panicRect =
        drawing::cocoaRect(family.panicButton);
    drawing::setFill(conduitColor(0x161616));
    drawing::rectFill(panicRect);
    drawing::setStroke(conduitColor(0x565656));
    drawing::frameRect(panicRect);
    const drawing::Size panicTextSize = drawing::sizeWithAttributes("PANIC", label);
    drawing::drawAtPoint_withAttributes("PANIC", drawing::makePoint(
        panicRect.origin.x + (panicRect.size.width - panicTextSize.width) * 0.5,
        panicRect.origin.y + (panicRect.size.height - panicTextSize.height) * 0.5), label);

    if (_openMenu == 1) {
        static drawing::String  items[] = {
            "METAL", "GLASS", "PLASTIC", "WOOD", "WATER", "SKIN",
            "DIRECT", "METAL VESSEL", "GLASS VESSEL", "PLASTIC VESSEL",
            "DEEP BRONZE", "TIERED BRONZE", "BROAD BRONZE",
            "BRIGHT BRONZE", "CARBON LAM.", "GLASS PLATE", "STEEL SHELL",
            "ALUM. PLATE", "PORCELAIN", "EARTHENWARE", "SPRUCE PLATE",
            "TENSIONED SKIN", "LOADED MEM.", "COUPLED MEM.",
            "CAVITY MEM.", "LOOSE MEM."
        };
        static_assert(std::size(items) == s3g::kProcessorConduitMaterialCount);
        constexpr double itemHeight = 18.0;
        constexpr uint32_t columnItems = 13u;
        const double menuWidth = s3g::gui_layout::processorMenuWidth(
            family.engine.frame.width);
        const drawing::Rect firstMenuRect = drawing::makeRect(
            s3g::gui_layout::processorControlX(family.engine.frame.x),
            s3g::gui_layout::rowY(family.engine, 0u) + 17.0,
            menuWidth, itemHeight * columnItems);
        const drawing::Rect secondMenuRect = drawing::offset(firstMenuRect,
            menuWidth, 0.0);
        const int selected = static_cast<int>(prm.material);
        drawing::drawDropdownMenu(firstMenuRect, itemHeight, items,
            columnItems, selected < static_cast<int>(columnItems)
                ? selected : -1, -1, small, style);
        drawing::drawDropdownMenu(secondMenuRect, itemHeight,
            items + columnItems, columnItems,
            selected >= static_cast<int>(columnItems)
                ? selected - static_cast<int>(columnItems) : -1,
            -1, small, style);
    }
    if (_openMenu == kInputListenMenuId) {
        static drawing::String  items[] = {
            "CHANNEL 1", "CHANNEL 2", "SUM MONO", "STEREO"
        };
        static_assert(std::size(items)
            == s3g::kProcessorConduitInputListenCount);
        constexpr double itemHeight = 18.0;
        const drawing::Rect menuRect = drawing::makeRect(
            s3g::gui_layout::processorControlX(family.engine.frame.x),
            s3g::gui_layout::rowY(family.engine, 1u) + 17.0,
            s3g::gui_layout::processorMenuWidth(family.engine.frame.width),
            itemHeight * static_cast<double>(std::size(items)));
        drawing::drawDropdownMenu(menuRect, itemHeight, items,
            static_cast<uint32_t>(std::size(items)),
            static_cast<int>(prm.inputListen), -1, small, style);
    }
    if (_openMenu == 2) {
        static drawing::String  items[] = {
            "SHRED", "WOOL", "RAT", "ZONE A",
            "ZONE B", "FUZZ I", "FUZZ II", "DIODE"
        };
        static_assert(std::size(items) == s3g::kProcessorConduitPedalCount);
        constexpr double itemHeight = 18.0;
        const drawing::Rect menuRect = drawing::makeRect(
            s3g::gui_layout::processorControlX(
                family.relationships.frame.x),
            s3g::gui_layout::rowY(family.relationships, 0u) + 17.0,
            s3g::gui_layout::processorMenuWidth(
                family.relationships.frame.width),
            itemHeight * static_cast<double>(std::size(items)));
        drawing::drawDropdownMenu(menuRect, itemHeight, items,
            static_cast<uint32_t>(std::size(items)),
            static_cast<int>(prm.pedal), -1, small, style);
    }
    if (_openMenu == kPedalPositionMenuId) {
        static drawing::String  items[] = {
            "PRE DRIVER", "PIEZO > PA", "MIC > LOOP"
        };
        static_assert(std::size(items)
            == s3g::kProcessorConduitPedalPositionCount);
        constexpr double itemHeight = 18.0;
        const drawing::Rect menuRect = drawing::makeRect(
            s3g::gui_layout::processorControlX(
                family.relationships.frame.x),
            s3g::gui_layout::rowY(family.relationships, 1u) + 17.0,
            s3g::gui_layout::processorMenuWidth(
                family.relationships.frame.width),
            itemHeight * static_cast<double>(std::size(items)));
        drawing::drawDropdownMenu(menuRect, itemHeight, items,
            static_cast<uint32_t>(std::size(items)),
            static_cast<int>(prm.pedalPosition), -1, small, style);
    }
    if (_openMenu == kFactoryPresetMenuId) {
        drawing::String  items[s3g::kProcessorConduitFactoryPresetCount] {};
        for (uint32_t index = 0u;
             index < s3g::kProcessorConduitFactoryPresetCount; ++index) {
            items[index] = drawing::stringFromUtf8(s3g::processorConduitFactoryPresetInfo(index).name);
        }
        constexpr double itemHeight = 18.0;
        const auto band = conduitTitleBand();
        const drawing::Rect anchor = drawing::cocoaRect(band.presetMenu);
        const drawing::Rect menuRect = drawing::makeRect(anchor.origin.x,
            drawing::maxY(anchor) + 2.0, anchor.size.width,
            itemHeight * static_cast<double>(
                s3g::kProcessorConduitFactoryPresetCount));
        drawing::drawDropdownMenu(menuRect, itemHeight, items,
            s3g::kProcessorConduitFactoryPresetCount,
            _factoryPresetIndex, -1, small, style);
    }

}

void updateSlider(drawing::Point point)  {
    const auto& family = kConduitFamilyLayout;
    const bool outputSlider = _dragSlider == kMixParamId
        || _dragSlider == kOutputParamId;
    const bool pedalSlider = _dragSlider == kPedalDriveParamId
        || _dragSlider == kPedalToneParamId
        || _dragSlider == kPedalMixParamId;
    const bool spaceSlider = _dragSlider >= kOctaveDownParamId
        && _dragSlider <= kStereoWidthParamId;
    const auto& panel = outputSlider ? family.output
        : (pedalSlider ? family.relationships
            : (spaceSlider ? family.preview : family.engine));
    const double controlX =
        s3g::gui_layout::processorControlX(panel.frame.x);
    const double trackWidth =
        s3g::gui_layout::processorTrackWidth(panel.frame.width);
    const double normalized = std::clamp(
        (point.x - controlX) / trackWidth, 0.0, 1.0);
    switch (_dragSlider) {
    case kInputParamId:
        editValue( kInputParamId, -24.0 + normalized * 60.0); break;
    case kDriverParamId: editValue( kDriverParamId, normalized); break;
    case kSizeParamId: editValue( kSizeParamId, normalized); break;
    case kTensionParamId: editValue( kTensionParamId, normalized); break;
    case kDampingParamId: editValue( kDampingParamId, normalized); break;
    case kPickupParamId: editValue( kPickupParamId, normalized); break;
    case kContactParamId: editValue( kContactParamId, normalized); break;
    case kFeedbackParamId: editValue( kFeedbackParamId, normalized); break;
    case kMixParamId: editValue( kMixParamId, normalized); break;
    case kOutputParamId:
        editValue( kOutputParamId, -60.0 + normalized * 66.0); break;
    case kPedalDriveParamId:
        editValue( kPedalDriveParamId, normalized); break;
    case kPedalToneParamId:
        editValue( kPedalToneParamId, normalized); break;
    case kOctaveDownParamId:
        editValue( kOctaveDownParamId, normalized); break;
    case kOctaveDragParamId:
        editValue( kOctaveDragParamId, normalized); break;
    case kPaDriveParamId:
        editValue( kPaDriveParamId, normalized); break;
    case kMicMotionParamId:
        editValue( kMicMotionParamId, normalized); break;
    case kChamberParamId:
        editValue( kChamberParamId, normalized); break;
    case kStereoWidthParamId:
        editValue( kStereoWidthParamId, normalized); break;
    case kPedalMixParamId:
        editValue( kPedalMixParamId, normalized); break;
    default: break;
    }
    if (_dragSlider != kInputParamId && _dragSlider != kMixParamId
        && _dragSlider != kOutputParamId) {
        _factoryPresetIndex = -1;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "CUSTOM");
    }
    setNeedsDisplay(true);

}

void mouseDown(drawing::Event*  event)  {
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    const auto& family = kConduitFamilyLayout;
    if (_openMenu == kMaterialMenuId) {
        constexpr double itemHeight = 18.0;
        constexpr uint32_t columnItems = 13u;
        const double menuWidth = s3g::gui_layout::processorMenuWidth(
            family.engine.frame.width);
        const drawing::Rect firstMenuRect = drawing::makeRect(
            s3g::gui_layout::processorControlX(family.engine.frame.x),
            s3g::gui_layout::rowY(family.engine, 0u) + 17.0,
            menuWidth, itemHeight * columnItems);
        const drawing::Rect secondMenuRect = drawing::offset(firstMenuRect,
            menuWidth, 0.0);
        int hit = drawing::dropdownHitIndex(point, firstMenuRect,
            itemHeight, columnItems);
        if (hit < 0) {
            const int secondHit = drawing::dropdownHitIndex(point,
                secondMenuRect, itemHeight, columnItems);
            if (secondHit >= 0) hit = secondHit
                + static_cast<int>(columnItems);
        }
        _openMenu = 0;
        if (hit >= 0) {
            editValue( kMaterialParamId, hit);
            _factoryPresetIndex = -1;
            std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                "CUSTOM");
        }
        setNeedsDisplay(true);
        return;
    }
    if (_openMenu == kInputListenMenuId) {
        constexpr double itemHeight = 18.0;
        const drawing::Rect menuRect = drawing::makeRect(
            s3g::gui_layout::processorControlX(family.engine.frame.x),
            s3g::gui_layout::rowY(family.engine, 1u) + 17.0,
            s3g::gui_layout::processorMenuWidth(family.engine.frame.width),
            itemHeight * static_cast<double>(
                s3g::kProcessorConduitInputListenCount));
        const int hit = drawing::dropdownHitIndex(point, menuRect,
            itemHeight, s3g::kProcessorConduitInputListenCount);
        _openMenu = 0;
        if (hit >= 0) {
            editValue( kInputListenParamId, hit);
        }
        setNeedsDisplay(true);
        return;
    }
    if (_openMenu == kPedalMenuId) {
        constexpr double itemHeight = 18.0;
        const drawing::Rect menuRect = drawing::makeRect(
            s3g::gui_layout::processorControlX(
                family.relationships.frame.x),
            s3g::gui_layout::rowY(family.relationships, 0u) + 17.0,
            s3g::gui_layout::processorMenuWidth(
                family.relationships.frame.width),
            itemHeight * static_cast<double>(
                s3g::kProcessorConduitPedalCount));
        const int hit = drawing::dropdownHitIndex(point, menuRect,
            itemHeight, s3g::kProcessorConduitPedalCount);
        _openMenu = 0;
        if (hit >= 0) {
            editValue( kPedalParamId, hit);
            _factoryPresetIndex = -1;
            std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                "CUSTOM");
        }
        setNeedsDisplay(true);
        return;
    }

    if (_openMenu == kPedalPositionMenuId) {
        constexpr double itemHeight = 18.0;
        const drawing::Rect menuRect = drawing::makeRect(
            s3g::gui_layout::processorControlX(
                family.relationships.frame.x),
            s3g::gui_layout::rowY(family.relationships, 1u) + 17.0,
            s3g::gui_layout::processorMenuWidth(
                family.relationships.frame.width),
            itemHeight * static_cast<double>(
                s3g::kProcessorConduitPedalPositionCount));
        const int hit = drawing::dropdownHitIndex(point, menuRect,
            itemHeight, s3g::kProcessorConduitPedalPositionCount);
        _openMenu = 0;
        if (hit >= 0) {
            editValue( kPedalPositionParamId, hit);
            _factoryPresetIndex = -1;
            std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                "CUSTOM");
        }
        setNeedsDisplay(true);
        return;
    }

    if (_openMenu == kFactoryPresetMenuId) {
        constexpr double itemHeight = 18.0;
        const auto band = conduitTitleBand();
        const drawing::Rect anchor = drawing::cocoaRect(band.presetMenu);
        const drawing::Rect menuRect = drawing::makeRect(anchor.origin.x,
            drawing::maxY(anchor) + 2.0, anchor.size.width,
            itemHeight * static_cast<double>(
                s3g::kProcessorConduitFactoryPresetCount));
        const int hit = drawing::dropdownHitIndex(point, menuRect,
            itemHeight, s3g::kProcessorConduitFactoryPresetCount);
        _openMenu = 0;
        if (hit >= 0) applyFactoryPreset(hit);
        setNeedsDisplay(true);
        return;
    }

    const auto titleBand = conduitTitleBand();
    if (drawing::contains(point,
            drawing::cocoaRect(titleBand.presetMenu))) {
        _openMenu = kFactoryPresetMenuId;
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point,
            drawing::cocoaRect(titleBand.randomButton))) {
        editConduitParams( s3g::processorConduitRandomParams(
            _snapshot, drawing::randomBits()));
        _factoryPresetIndex = -1;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "RANDOM");
        setNeedsDisplay(true);
        return;
    }
    if (handleProcessorTitleClick(point,
            &_plugin->plugin, "Processor Conduit", titleBand,
            _titlePresetName, sizeof(_titlePresetName), kOutputParamId)) {
        setNeedsDisplay(true);
        return;
    }

    if (drawing::contains(point,
            drawing::cocoaRect(family.panicButton))) {
        _plugin->panicRequested.store(true, std::memory_order_release);
        setNeedsDisplay(true);
        return;
    }

    const drawing::Rect materialBox = drawing::makeRect(
        s3g::gui_layout::processorControlX(family.engine.frame.x),
        s3g::gui_layout::rowY(family.engine, 0u) - 5.0,
        s3g::gui_layout::processorMenuWidth(family.engine.frame.width),
        24.0);
    if (drawing::contains(point, materialBox)) {
        _openMenu = kMaterialMenuId;
        setNeedsDisplay(true);
        return;
    }

    const drawing::Rect inputListenBox = drawing::makeRect(
        s3g::gui_layout::processorControlX(family.engine.frame.x),
        s3g::gui_layout::rowY(family.engine, 1u) - 5.0,
        s3g::gui_layout::processorMenuWidth(family.engine.frame.width),
        24.0);
    if (drawing::contains(point, inputListenBox)) {
        _openMenu = kInputListenMenuId;
        setNeedsDisplay(true);
        return;
    }

    const drawing::Rect pedalBox = drawing::makeRect(
        s3g::gui_layout::processorControlX(family.relationships.frame.x),
        s3g::gui_layout::rowY(family.relationships, 0u) - 5.0,
        s3g::gui_layout::processorMenuWidth(
            family.relationships.frame.width),
        24.0);
    if (drawing::contains(point, pedalBox)) {
        _openMenu = kPedalMenuId;
        setNeedsDisplay(true);
        return;
    }

    const drawing::Rect positionBox = drawing::makeRect(
        s3g::gui_layout::processorControlX(family.relationships.frame.x),
        s3g::gui_layout::rowY(family.relationships, 1u) - 5.0,
        s3g::gui_layout::processorMenuWidth(
            family.relationships.frame.width),
        24.0);
    if (drawing::contains(point, positionBox)) {
        _openMenu = kPedalPositionMenuId;
        setNeedsDisplay(true);
        return;
    }

    const clap_id engineParamIds[] {
        kInputParamId, kDriverParamId, kSizeParamId, kTensionParamId,
        kDampingParamId, kPickupParamId, kContactParamId, kFeedbackParamId
    };
    const clap_id outputParamIds[] { kOutputParamId, kMixParamId };
    const clap_id pedalParamIds[] {
        kPedalMixParamId, kPedalDriveParamId, kPedalToneParamId
    };
    const clap_id spaceParamIds[] {
        kOctaveDownParamId, kOctaveDragParamId, kPaDriveParamId,
        kMicMotionParamId, kChamberParamId, kStereoWidthParamId
    };
    const auto beginSlider = [&](clap_id paramId) {
        double defaultValue = 0.0;
        if (drawing::sliderDoubleClickDefault(event,
                &_plugin->plugin, paramId, &defaultValue)) {
            editValue( paramId, defaultValue);
            _dragSlider = -1;
        } else {
            _dragSlider = static_cast<int>(paramId);
            beginEdit(paramId);
            updateSlider(point);
        }
        setNeedsDisplay(true);
    };
    for (uint32_t i = 0u; i < std::size(outputParamIds); ++i) {
        if (drawing::contains(point, drawing::cocoaRect(
                s3g::gui_layout::sliderHitRect(family.output, i)))) {
            beginSlider(outputParamIds[i]);
            return;
        }
    }
    for (uint32_t i = 0u; i < std::size(engineParamIds); ++i) {
        if (drawing::contains(point, drawing::cocoaRect(
                s3g::gui_layout::sliderHitRect(family.engine, i + 2u)))) {
            beginSlider(engineParamIds[i]);
            return;
        }
    }
    for (uint32_t i = 0u; i < std::size(pedalParamIds); ++i) {
        if (drawing::contains(point, drawing::cocoaRect(
                s3g::gui_layout::sliderHitRect(
                    family.relationships, i + 2u)))) {
            beginSlider(pedalParamIds[i]);
            return;
        }
    }
    for (uint32_t i = 0u; i < std::size(spaceParamIds); ++i) {
        if (drawing::contains(point, drawing::cocoaRect(
                s3g::gui_layout::sliderHitRect(
                    family.preview, i)))) {
            beginSlider(spaceParamIds[i]);
            return;
        }
    }

}

void mouseDragged(drawing::Event*  event)  {
    if (_dragSlider > 0) {
        updateSlider(convertPoint_fromView(event->locationInWindow(), nullptr));
    }

}

void mouseUp(drawing::Event*  event)  {
    (void)event;
    _dragSlider = -1;

}

};
}
s3g::portable_gui::foundation::ContentView* makePortableCanvas(Plugin& p) { return new stereo_processor_canvas::Editor(p); }
#include "s3g_clap_canvas_gui.inc"
