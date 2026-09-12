// Adapted from retained Cocoa geometry, drawing order and interaction maps.
namespace feedback_processor_canvas {
namespace drawing = s3g::portable_gui::feedback_drawing;
namespace foundation = s3g::portable_gui::foundation;
constexpr auto& presetState = stateExt;
constexpr auto kMatrixPanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Routing,
    { 12.0, 42.0, 620.0, 716.0 }, 32.0, 24.0, 0u };
constexpr auto kOutputPanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Output,
    { 644.0, 42.0, 344.0, 80.0 }, 32.0, 24.0, 2u };
constexpr auto kInputPanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Source,
    { 644.0, 134.0, 344.0, 132.0 }, 32.0, 27.0, 3u };
constexpr auto kExciterPanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Engine,
    { 644.0, 278.0, 344.0, 106.0 }, 32.0, 27.0, 3u };
constexpr auto kMaterialPanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::ToneShape,
    { 644.0, 396.0, 344.0, 132.0 }, 32.0, 24.0, 4u };
constexpr auto kScenePanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Modulation,
    { 1000.0, 42.0, 344.0, 142.0 }, 118.0, 24.0, 1u };
constexpr auto kObjectPanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 1000.0, 196.0, 344.0, 246.0 }, 80.0, 29.0, 6u };
constexpr auto kFormPanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::EventTiming,
    { 1000.0, 454.0, 344.0, 156.0 }, 32.0, 24.0, 5u };
constexpr auto kGesturePanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Modulation,
    { 1000.0, 622.0, 344.0, 136.0 }, 32.0, 24.0, 1u };
constexpr auto kFracturePanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Modulation,
    { 644.0, 540.0, 344.0, 218.0 }, 32.0, 24.0, 0u };

constexpr double kMatrixX = 142.0;
constexpr double kMatrixY = 184.0;
constexpr double kMatrixCell = 44.0;
constexpr double kPresetMenuItemHeight = 20.0;
constexpr uint32_t kPresetMenuColumns = 2u;

struct FissureUiRow {
    clap_id id;
    const char* label;
    const s3g::gui_layout::Panel* panel;
    uint32_t row;
};

constexpr std::array<FissureUiRow, 17u> kUiRows {{
    { kOutputParamId, "OUT", &kOutputPanel, 0u },
    { kInputParamId, "MIC IN", &kInputPanel, 0u },
    { kContactParamId, "CONTACT", &kInputPanel, 1u },
    { kVoiceParamId, "INPUT COUPLE", &kInputPanel, 2u },
    { kShakerParamId, "BOX IMPACT", &kExciterPanel, 0u },
    { kRattleParamId, "LOOSE RATTLE", &kExciterPanel, 1u },
    { kSpringParamId, "SPRING WIRE", &kExciterPanel, 2u },
    { kPressureParamId, "PRESSURE", &kMaterialPanel, 0u },
    { kMassParamId, "MASS", &kMaterialPanel, 1u },
    { kBodyParamId, "BODY", &kMaterialPanel, 2u },
    { kMemoryParamId, "MEMORY / FRAG", &kMaterialPanel, 3u },
    { kEdgeParamId, "EDGE", &kFormPanel, 0u },
    { kRateParamId, "RATE", &kFormPanel, 1u },
    { kVoidParamId, "VOID PROBABILITY", &kFormPanel, 2u },
    { kSpaceParamId, "SPACE", &kFormPanel, 3u },
    { kMotionParamId, "MOTION", &kFormPanel, 4u },
    { kSeedParamId, "SEED", &kScenePanel, 0u },
}};

// The left puck controls realized cut density; the right shapes each rupture.
constexpr std::array<clap_id, 2u> kFractureXParamIds {{
    kFractureForceParamId, kFractureDistanceParamId,
}};
constexpr std::array<clap_id, 2u> kFractureYParamIds {{
    kFractureVoidParamId, kFractureSpaceParamId,
}};
constexpr std::array<clap_id, 2u> kFractureLatchParamIds {{
    kFractureDensityLatchParamId, kFractureShapeLatchParamId,
}};
constexpr std::array<clap_id, 2u> kFractureMotionXParamIds {{
    kFractureDensityMotionXParamId, kFractureShapeMotionXParamId,
}};
constexpr std::array<clap_id, 2u> kFractureMotionYParamIds {{
    kFractureDensityMotionYParamId, kFractureShapeMotionYParamId,
}};
constexpr std::array<clap_id, 2u> kFractureMotionEnergyParamIds {{
    kFractureDensityEnergyParamId, kFractureShapeEnergyParamId,
}};
constexpr uint32_t kFractureTrailCapacity = 18u;

struct ObjectUiRow {
    clap_id base;
    const char* label;
    uint32_t row;
};

constexpr std::array<ObjectUiRow, 6u> kObjectRows {{
    { kObjectSizeParamBase, "SIZE", 0u },
    { kObjectDecayParamBase, "DECAY", 1u },
    { kObjectHardnessParamBase, "HARDNESS", 2u },
    { kObjectSensitivityParamBase, "SENSITIVITY", 3u },
    { kObjectDriveParamBase, "DRIVE", 4u },
    { kCellLevelParamBase, "LEVEL", 5u },
}};

double uiNormalizedValue(clap_id id, double value)
{
    if (const auto* def = baseParamDef(id)) {
        return std::clamp((value - def->minimum)
            / std::max(1.0e-12, def->maximum - def->minimum), 0.0, 1.0);
    }
    if (isMatrixParam(id)) return std::clamp((value + 1.0) * 0.5, 0.0, 1.0);
    return std::clamp(value, 0.0, 1.0);
}

drawing::String  fissureUiValueText(clap_id id, double value,
    const char* fallback)
{
    if (id == kPressureParamId || id == kMassParamId
        || id == kMemoryParamId || id == kBodyParamId) {
        const float energy = s3g::processorFissureLowEnergyCurve(
            static_cast<float>(value));
        return drawing::stringFormat("E %.1f%%", energy * 100.0f);
    }
    if (id == kEdgeParamId) {
        return drawing::stringFormat("HARD %.0f%%", value * 100.0);
    }
    if (id == kRateParamId) {
        const float rate = s3g::processorFissureEventRateHz(
            static_cast<float>(value));
        if (rate < 0.01f) {
            return drawing::stringFormat("DRONE %.0fs",
                1.0f / std::max(rate, 1.0e-6f));
        }
        if (rate < 1.0f) {
            return drawing::stringFormat("%.2f/s ×8", rate);
        }
        return drawing::stringFormat("%.0f/s ×8", rate);
    }
    if (id == kVoidParamId) {
        const float chance = s3g::processorFissureFormCurve(
            static_cast<float>(value));
        return drawing::stringFormat("%.0f%% / EVENT",
            chance * 100.0f);
    }
    if (id == kSpaceParamId) {
        const float seconds = s3g::processorFissureSpaceSeconds(
            static_cast<float>(value));
        if (seconds < 1.0f) {
            return drawing::stringFormat("GAP %.0fms",
                seconds * 1000.0f);
        }
        return drawing::stringFormat("GAP %.1fs", seconds);
    }
    return drawing::stringFromUtf8(fallback);
}

double uiValueFromNormalized(clap_id id, double normalized)
{
    normalized = std::clamp(normalized, 0.0, 1.0);
    if (const auto* def = baseParamDef(id)) {
        return clampParamValue(id,
            def->minimum + (def->maximum - def->minimum) * normalized);
    }
    if (isMatrixParam(id)) return normalized * 2.0 - 1.0;
    return normalized;
}

drawing::Rect presetDropdownRect()
{
    const auto band = drawing::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    const uint32_t rows = drawing::multiColumnMenuRows(
        kFactoryPresetCount, kPresetMenuColumns);
    return drawing::makeRect(band.presetMenu.x, 32.0, 344.0,
        rows * kPresetMenuItemHeight);
}

drawing::Rect matrixCellRect(uint32_t destination, uint32_t source)
{
    return drawing::makeRect(kMatrixX + source * kMatrixCell,
        kMatrixY + destination * kMatrixCell,
        kMatrixCell - 3.0, kMatrixCell - 3.0);
}

drawing::Rect cutMaskButtonRect(uint32_t cell)
{
    return drawing::makeRect(kMatrixX + cell * kMatrixCell,
        538.0, kMatrixCell - 3.0, 20.0);
}

drawing::Rect topologyButtonRect(uint32_t index)
{
    return drawing::makeRect(kMatrixPanel.frame.x + 16.0 + index * 98.0,
        562.0, 90.0, 25.0);
}

drawing::Rect topologyMixRect()
{
    return drawing::makeRect(kMatrixPanel.frame.x + 112.0, 602.0, 282.0, 24.0);
}

drawing::Rect routeSliderRect()
{
    return drawing::makeRect(kMatrixPanel.frame.x + 112.0, 641.0, 284.0, 26.0);
}

drawing::Rect routePolarityRect()
{
    return drawing::makeRect(420.0, 641.0, 82.0, 26.0);
}

drawing::Rect routeClearRect()
{
    return drawing::makeRect(514.0, 641.0, 82.0, 26.0);
}

drawing::Rect outputModeButtonRect(uint32_t index)
{
    static constexpr std::array<double, 3u> widths {{ 62.0, 62.0, 84.0 }};
    double x = kOutputPanel.frame.x + 112.0;
    for (uint32_t mode = 0u; mode < index; ++mode) x += widths[mode] + 7.0;
    return drawing::makeRect(x, 96.0, widths[index], 20.0);
}

drawing::Rect sceneButtonRect(uint32_t index)
{
    return drawing::makeRect(kScenePanel.frame.x + 14.0 + index * 40.0,
        76.0, 34.0, 25.0);
}

drawing::Rect storeButtonRect() { return drawing::makeRect(1176.0, 76.0, 54.0, 25.0); }
drawing::Rect holdButtonRect() { return drawing::makeRect(1238.0, 76.0, 45.0, 25.0); }
drawing::Rect runButtonRect() { return drawing::makeRect(1291.0, 76.0, 39.0, 25.0); }

drawing::Rect sceneMorphRect()
{
    return drawing::makeRect(kScenePanel.frame.x + 76.0, 121.0, 250.0, 24.0);
}

drawing::Rect cellButtonRect(uint32_t index)
{
    return drawing::makeRect(kObjectPanel.frame.x + 14.0 + index * 40.0,
        230.0, 33.0, 28.0);
}

drawing::Rect actionButtonRect(uint32_t index)
{
    static constexpr std::array<drawing::Rect, 5u> rects {{
        {{ 1014.0, 680.0 }, { 145.0, 27.0 }},
        {{ 1167.0, 680.0 }, { 163.0, 27.0 }},
        {{ 1014.0, 716.0 }, { 96.0, 27.0 }},
        {{ 1118.0, 716.0 }, { 132.0, 27.0 }},
        {{ 1258.0, 716.0 }, { 72.0, 27.0 }},
    }};
    return rects[index];
}

drawing::Rect fracturePadRect(uint32_t index)
{
    return drawing::makeRect(660.0 + static_cast<double>(index) * 108.0,
        580.0, 100.0, 132.0);
}

drawing::Rect fractureLatchButtonRect(uint32_t index)
{
    return drawing::makeRect(886.0,
        714.0 + static_cast<double>(index) * 24.0, 86.0, 20.0);
}

drawing::Rect grabButtonRect() { return drawing::makeRect(886.0, 580.0, 86.0, 30.0); }
drawing::Rect repeatButtonRect() { return drawing::makeRect(886.0, 620.0, 86.0, 42.0); }

float meterNormalized(float peak)
{
    const float db = 20.0f * std::log10(std::max(1.0e-6f, peak));
    return std::clamp((db + 60.0f) / 66.0f, 0.0f, 1.0f);
}

void drawMeterBar(drawing::Rect rect, float amount, VSTGUI::CColor  fill)
{
    amount = std::clamp(amount, 0.0f, 1.0f);
    drawing::setFill(drawing::color(0x171717));
    drawing::rectFill(rect);
    drawing::setStroke(drawing::color(0x4a4a4a));
    drawing::frameRect(rect);
    drawing::setFill(fill);
    drawing::rectFill(drawing::makeRect(rect.origin.x + 2.0, rect.origin.y + 2.0,
        std::max<double>(0.0, (rect.size.width - 4.0) * amount),
        rect.size.height - 4.0));
}

class Editor final : public s3g::portable_gui::routing::Canvas {
public:
using Canvas = s3g::portable_gui::routing::Canvas;

    Plugin* _plugin;
    int _dragParam;
    uint32_t _selectedSource;
    uint32_t _selectedDestination;
    bool _matrixPainting;
    double _matrixPaintValue;
    int _lastPaintedRoute;
    bool _cutMaskPainting;
    double _cutMaskPaintValue;
    int _lastPaintedMask;
    int _fracturePad;
    drawing::Point _fractureTrail[2][kFractureTrailCapacity];
    float _fractureTrailAlpha[2][kFractureTrailCapacity];
    uint32_t _fractureTrailCount[2];
    double _fractureLastX[2];
    double _fractureLastY[2];
    double _fractureLastTime[2];
    bool _fractureMotionReady[2];
    bool _repeatPressed;
    bool _presetMenuOpen;
    int _hoverPreset;
    char _presetName[64];
#include "s3g_feedback_processor_editor.inc"
explicit Editor(Plugin& plugin) : Canvas(kGuiWidth,kGuiHeight) {_plugin = &plugin;
        _dragParam = -1;
        _selectedSource = 0u;
        _selectedDestination = 0u;
        _matrixPainting = false;
        _matrixPaintValue = 0.0;
        _lastPaintedRoute = -1;
        _cutMaskPainting = false;
        _cutMaskPaintValue = 1.0;
        _lastPaintedMask = -1;
        _fracturePad = -1;
        for (uint32_t pad = 0u; pad < 2u; ++pad) {
            _fractureTrailCount[pad] = 0u;
            _fractureLastX[pad] = 0.0;
            _fractureLastY[pad] = 0.0;
            _fractureLastTime[pad] = 0.0;
            _fractureMotionReady[pad] = false;
            for (uint32_t point = 0u; point < kFractureTrailCapacity;
                 ++point) {
                _fractureTrail[pad][point] = drawing::Point{};
                _fractureTrailAlpha[pad][point] = 0.0f;
            }
        }
        _repeatPressed = false;
        _presetMenuOpen = false;
        _hoverPreset = -1;
        auto* p = &plugin;
        const uint32_t preset = p ? static_cast<uint32_t>(paramValue(*p, kPresetParamId)) : 0u;
        std::snprintf(_presetName, sizeof(_presetName), "%s",
            factoryPresetName(preset));
}

void service() override {
    
    for (uint32_t pad = 0u; pad < 2u; ++pad) {
        for (uint32_t point = 0u; point < _fractureTrailCount[pad];
             ++point) {
            _fractureTrailAlpha[pad][point] *= 0.88f;
        }
    }
    if (!isHidden() && _plugin
        && true) {
        setNeedsDisplay(true);
    }

}

void drawOriginal(drawing::Rect dirty)  {
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    drawing::Style style;
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs  labels = drawing::softLabelAttrs();
    drawing::Attrs  values = drawing::softValueAttrs();
    drawing::Attrs  tiny = drawing::textAttrs(
        drawing::color(0x969696), 8.0);
    const uint32_t presetIndex = static_cast<uint32_t>(std::clamp(
        std::round(paramValue(*p, kPresetParamId)), 0.0,
        static_cast<double>(kCustomPresetIndex)));
    if (presetIndex < kFactoryPresetCount) {
        std::snprintf(_presetName, sizeof(_presetName), "%s",
            factoryPresetName(presetIndex));
    } else if (std::strcmp(_presetName, "INIT / PLATE RING") == 0) {
        std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
    }
    const auto titleBand = drawing::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    drawing::drawProcessorTitleBand(
        "s3g PROCESSOR FISSURE",
        drawing::stringFromUtf8(_presetName),
        drawing::peakDbText(
            p->outputPeak.load(std::memory_order_relaxed)),
        titleBand, drawing::softTitleAttrs(), labels, values, style);

    const auto drawPanel = [&](drawing::String  name,
                               const s3g::gui_layout::Panel& panel) {
        drawing::drawPanelFrame(panel, style);
        drawing::drawPanelHeader(name, true, panel, labels, style);
    };
    drawPanel("MATRIX FIELD  ·  MIC → ENERGY / PITCH → OBJECTS → ROUTES",
        kMatrixPanel);
    drawPanel("OUTPUT", kOutputPanel);
    drawPanel("EXTERNAL MIC / CONTACT", kInputPanel);
    drawPanel("ATTACHED OBJECT IDENTITIES", kExciterPanel);
    drawPanel("FIELD ENERGY  ·  EXPANDED LOW RANGE", kMaterialPanel);
    drawPanel("SCENES", kScenePanel);
    drawPanel("PHYSICAL OBJECT  ·  SELECT + STRIKE", kObjectPanel);
    drawPanel("FORM / INTERRUPTION  ·  FOUR AXES", kFormPanel);
    drawPanel("PERFORMANCE GESTURES", kGesturePanel);
    drawPanel("FRACTURE  ·  CUT DENSITY / RUPTURE SHAPE", kFracturePanel);

    for (const auto& row : kUiRows) {
        const double value = paramValue(*p, row.id);
        char text[64] {};
        paramsValueToText(&p->plugin, row.id, value, text, sizeof(text));
        drawing::drawProcessorSlider(
            drawing::stringFromUtf8(row.label),
            fissureUiValueText(row.id, value, text),
            uiNormalizedValue(row.id, value),
            s3g::gui_layout::rowY(*row.panel, row.row),
            row.panel->frame.x, row.panel->frame.width,
            labels, values, style);
    }

    drawing::drawAtPoint_withAttributes("MODE", drawing::makePoint(kOutputPanel.frame.x + 16.0, 99.0), labels);
    static constexpr const char* modeLabels[] {
        "STEREO", "QUAD", "8 DIRECT"
    };
    const uint32_t outputMode = static_cast<uint32_t>(std::clamp(
        std::round(paramValue(*p, kOutputModeParamId)), 0.0, 2.0));
    for (uint32_t mode = 0u; mode < 3u; ++mode) {
        drawing::drawHeaderButton(outputModeButtonRect(mode),
            drawing::cocoaRect(kOutputPanel.frame),
            drawing::stringFromUtf8(modeLabels[mode]),
            outputMode == mode, values, style);
    }

    const float inputPeak = p->inputActivity.load(std::memory_order_relaxed);
    const float inputPeakMarker = p->inputPeakActivity.load(
        std::memory_order_relaxed);
    const float transfer = p->inputTransferActivity.load(
        std::memory_order_relaxed);
    const float pitchHz = p->detectedPitchHz.load(
        std::memory_order_relaxed);
    const float pitchConfidence = p->pitchConfidence.load(
        std::memory_order_relaxed);
    const float sustainedPitch = p->sustainedPitchDrive.load(
        std::memory_order_relaxed);
    const float governor = std::clamp(
        p->minimumGovernor.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const drawing::Rect micMeter = drawing::makeRect(114.0, 76.0, 482.0, 14.0);
    const drawing::Rect contactMeter = drawing::makeRect(114.0, 108.0, 214.0, 14.0);
    drawing::drawAtPoint_withAttributes("MIC POST IN · RMS", drawing::makePoint(28.0, 76.0), labels);
    drawMeterBar(micMeter, meterNormalized(inputPeak),
        inputPeak > .50f
            ? drawing::calibratedRGB(.76, .34, .16, .92)
            : drawing::color(0x8a8a8a));
    const double peakMarkerX = micMeter.origin.x + 2.0
        + (micMeter.size.width - 4.0) * meterNormalized(inputPeakMarker);
    drawing::setFill(drawing::color(0xd4d4d4));
    drawing::rectFill(drawing::makeRect(peakMarkerX, micMeter.origin.y + 1.0,
        1.0, micMeter.size.height - 2.0));
    drawing::drawAtPoint_withAttributes(drawing::stringFormat("%+.1f dB",
        20.0f * std::log10(std::max(1.0e-6f, inputPeak))), drawing::makePoint(526.0, 77.0), tiny);
    drawing::drawAtPoint_withAttributes("ECOLOGY XFER", drawing::makePoint(28.0, 108.0), labels);
    drawMeterBar(contactMeter, transfer,
        drawing::calibratedRGB(.20, .62, .72, .92));
    drawing::drawAtPoint_withAttributes(drawing::stringFormat("%.0f%%", transfer * 100.0f), drawing::makePoint(294.0, 109.0), tiny);
    drawing::setStroke(drawing::color(0x4a4a4a));
    drawing::Path::strokeLineFromPoint_toPoint(drawing::makePoint(332.0, 115.0), drawing::makePoint(352.0, 115.0));
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        const float cellActivity = std::clamp(
            p->activity[cell].load(std::memory_order_relaxed), 0.0f, 1.0f);
        const float sensitivity = static_cast<float>(paramValue(*p,
            kObjectSensitivityParamBase + cell));
        const float objectTransfer = std::clamp(transfer * sensitivity
            * (.48f + cellActivity * .82f), 0.0f, 1.0f);
        const drawing::Rect response = drawing::makeRect(356.0 + cell * 30.0,
            103.0, 23.0, 24.0);
        drawing::setFill(drawing::calibratedRGB(.08 + objectTransfer * .14, .10 + objectTransfer * .42, .11 + objectTransfer * .51, 1.0));
        drawing::rectFill(response);
        drawing::setStroke(drawing::color(0x515151));
        drawing::frameRect(response);
        drawing::drawAtPoint_withAttributes(drawing::stringFormat("%u", cell + 1u), drawing::makePoint(response.origin.x + 8.0,
                response.origin.y + 7.0), tiny);
    }
    drawing::drawAtPoint_withAttributes("INPUT PRESENT", drawing::makePoint(28.0, 136.0), tiny);
    drawing::drawAtPoint_withAttributes("ENERGY / HELD PITCH", drawing::makePoint(114.0, 136.0), tiny);
    drawing::String  pitchText = pitchConfidence > .08f
        ? drawing::stringFormat("F0 %.0f · LOCK %.0f · HOLD %.0f",
            pitchHz, pitchConfidence * 100.0f, sustainedPitch * 100.0f)
        : drawing::stringFormat("F0 — · LOCK %.0f · HOLD %.0f",
            pitchConfidence * 100.0f, sustainedPitch * 100.0f);
    drawing::drawAtPoint_withAttributes(pitchText, drawing::makePoint(248.0, 136.0), tiny);
    drawing::drawAtPoint_withAttributes("OBJECT RESPONSE 1—8", drawing::makePoint(428.0, 136.0), tiny);

    drawing::drawAtPoint_withAttributes("SOURCE", drawing::makePoint(kMatrixX + 142.0,
        kMatrixY - 37.0), labels);
    drawing::drawAtPoint_withAttributes("DEST", drawing::makePoint(kMatrixX - 58.0,
        kMatrixY + 163.0), labels);
    for (uint32_t source = 0u; source < 8u; ++source) {
        drawing::drawAtPoint_withAttributes(drawing::stringFormat("S%u", source + 1u), drawing::makePoint(kMatrixX + source * kMatrixCell + 13.0,
                kMatrixY - 18.0), tiny);
    }
    for (uint32_t destination = 0u; destination < 8u; ++destination) {
        drawing::drawAtPoint_withAttributes(drawing::stringFormat("D%u", destination + 1u), drawing::makePoint(kMatrixX - 28.0,
                kMatrixY + destination * kMatrixCell + 14.0), tiny);
        const float destinationActivity = std::clamp(
            p->activity[destination].load(std::memory_order_relaxed),
            0.0f, 1.0f);
        for (uint32_t source = 0u; source < 8u; ++source) {
            const double route = paramValue(*p,
                kMatrixParamBase + destination * 8u + source);
            const double magnitude = std::abs(route);
            const drawing::Rect rect = matrixCellRect(destination, source);
            drawing::setFill(drawing::calibratedWhite(.070 + magnitude * .055 + destinationActivity * .035, 1.0));
            drawing::rectFill(rect);
            if (magnitude > .002) {
                VSTGUI::CColor  routeColor = route >= 0.0
                    ? drawing::calibratedRGB(.79, .36, .17, .26 + magnitude * .70)
                    : drawing::calibratedRGB(.15, .55, .69, .26 + magnitude * .70);
                drawing::setFill(routeColor);
                const double inset = 4.0 + (1.0 - magnitude) * 9.0;
                drawing::rectFill(drawing::inset(rect, inset, inset));
            }
            const bool selected = source == _selectedSource
                && destination == _selectedDestination;
            drawing::setStroke(drawing::color(selected ? 0xd8d8d8 : 0x343434));
            drawing::frameRectWithWidth(rect, selected ? 2.0 : .7);
            drawing::drawAtPoint_withAttributes(drawing::stringFormat("%c%02.0f",
                route < 0.0 ? '-' : '+', magnitude * 99.0), drawing::makePoint(rect.origin.x + 9.0,
                    rect.origin.y + 15.0), tiny);
        }
        const float cutPulse = std::clamp(
            p->cutActivity[destination].load(std::memory_order_relaxed),
            0.0f, 1.0f);
        if (cutPulse > .015f) {
            const bool negative = p->cutPolarity[destination].load(
                std::memory_order_relaxed) < 0.0f;
            VSTGUI::CColor  cutColor = negative
                ? drawing::calibratedRGB(.15, .63, .82, .18 + cutPulse * .76)
                : drawing::calibratedRGB(.92, .37, .12, .18 + cutPulse * .76);
            drawing::setStroke(cutColor);
            drawing::frameRectWithWidth(drawing::makeRect(kMatrixX - 4.0,
                kMatrixY + destination * kMatrixCell - 4.0,
                kMatrixCell * 8.0 + 5.0, kMatrixCell + 4.0),
                1.0 + cutPulse * 2.0);
        }
    }

    drawing::drawAtPoint_withAttributes("CUT MASK", drawing::makePoint(78.0, 543.0), tiny);
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        const drawing::Rect maskRect = cutMaskButtonRect(cell);
        const bool enabled = paramValue(*p, kCutMaskParamBase + cell) >= .5;
        const float pulse = std::clamp(
            p->cutActivity[cell].load(std::memory_order_relaxed), 0.0f, 1.0f);
        const bool negative = p->cutPolarity[cell].load(
            std::memory_order_relaxed) < 0.0f;
        const float age = std::clamp(p->cutFragmentAge[cell].load(
            std::memory_order_relaxed), 0.0f, 1.0f);
        drawing::setFill(drawing::calibratedRGB(enabled ? .18 + pulse * .48 : .07, enabled ? .13 + pulse * (negative ? .31 : .12) : .07, enabled ? .11 + pulse * (negative ? .48 : .06) : .07, 1.0));
        drawing::rectFill(maskRect);
        drawing::setStroke(drawing::color(enabled ? 0x858585 : 0x303030));
        drawing::frameRect(maskRect);
        drawing::setFill(drawing::color(negative ? 0x45a9c7 : 0xd06a35));
        drawing::rectFill(drawing::makeRect(maskRect.origin.x + 2.0,
            drawing::maxY(maskRect) - 3.0,
            (maskRect.size.width - 4.0) * age, 2.0));
        drawing::drawAtPoint_withAttributes(drawing::stringFormat("%u%c", cell + 1u,
            pulse > .08f ? (negative ? '-' : '+') : ' '), drawing::makePoint(maskRect.origin.x + 12.0,
                maskRect.origin.y + 5.0), tiny);
    }

    static constexpr const char* topologyLabels[] {
        "ISLANDS", "RING", "PAIRS", "HUB", "CLUSTERS", "SCATTER"
    };
    const uint32_t topology = static_cast<uint32_t>(std::clamp(
        std::round(paramValue(*p, kTopologyShapeParamId)), 0.0, 5.0));
    for (uint32_t shape = 0u; shape < 6u; ++shape) {
        drawing::drawHeaderButton(topologyButtonRect(shape),
            drawing::cocoaRect(kMatrixPanel.frame),
            drawing::stringFromUtf8(topologyLabels[shape]),
            topology == shape, values, style);
    }
    drawing::drawAtPoint_withAttributes("STRUCTURE", drawing::makePoint(28.0, 608.0), labels);
    const drawing::Rect topologyTrack = topologyMixRect();
    drawMeterBar(topologyTrack,
        static_cast<float>(paramValue(*p, kTopologyMixParamId)),
        drawing::color(0x686868));
    drawing::drawAtPoint_withAttributes(drawing::stringFormat("BLEND %.0f%% · CHOOSE SHAPE TO APPLY",
        paramValue(*p, kTopologyMixParamId) * 100.0), drawing::makePoint(410.0, 608.0), tiny);

    const clap_id routeId = kMatrixParamBase
        + _selectedDestination * 8u + _selectedSource;
    const double routeValue = paramValue(*p, routeId);
    drawing::drawAtPoint_withAttributes("ROUTE", drawing::makePoint(28.0, 648.0), labels);
    drawing::drawAtPoint_withAttributes(drawing::stringFormat("D%u ← S%u",
        _selectedDestination + 1u, _selectedSource + 1u), drawing::makePoint(68.0, 648.0), tiny);
    const drawing::Rect routeTrack = routeSliderRect();
    drawing::setFill(drawing::color(0x171717));
    drawing::rectFill(routeTrack);
    drawing::setStroke(drawing::color(0x555555));
    drawing::frameRect(routeTrack);
    const double routeCenter = drawing::midX(routeTrack);
    drawing::setStroke(drawing::color(0x707070));
    drawing::frameRect(drawing::makeRect(routeCenter, routeTrack.origin.y, 1.0,
        routeTrack.size.height));
    const double routeX = routeTrack.origin.x
        + uiNormalizedValue(routeId, routeValue) * routeTrack.size.width;
    drawing::setFill(drawing::color(routeValue >= 0.0 ? 0xc26335 : 0x398da8));
    drawing::rectFill(drawing::makeRect(routeX - 3.0, routeTrack.origin.y + 3.0,
        6.0, routeTrack.size.height - 6.0));
    drawing::drawHeaderButton(routePolarityRect(),
        drawing::cocoaRect(kMatrixPanel.frame), "FLIP ±",
        false, values, style);
    drawing::drawHeaderButton(routeClearRect(),
        drawing::cocoaRect(kMatrixPanel.frame), "CLEAR",
        false, values, style);
    drawing::drawAtPoint_withAttributes("CELL CLICK CYCLES OFF → +34 → −34 · DRAG PAINTS · ROUTE BELOW IS PRECISE", drawing::makePoint(74.0, 686.0), tiny);
    drawing::drawAtPoint_withAttributes(drawing::stringFormat("SAFETY GOVERNOR %.0f%%",
        governor * 100.0f), drawing::makePoint(238.0, 716.0), labels);

    const uint32_t selectedScene = static_cast<uint32_t>(std::clamp(
        std::round(paramValue(*p, kSceneParamId)), 1.0, 4.0));
    for (uint32_t scene = 0u; scene < 4u; ++scene) {
        drawing::drawHeaderButton(sceneButtonRect(scene),
            drawing::cocoaRect(kScenePanel.frame),
            drawing::stringFormat("%c", 'A' + scene),
            selectedScene == scene + 1u, values, style);
    }
    drawing::drawHeaderButton(storeButtonRect(),
        drawing::cocoaRect(kScenePanel.frame), "STORE",
        false, values, style);
    drawing::drawHeaderButton(holdButtonRect(),
        drawing::cocoaRect(kScenePanel.frame), "HOLD",
        paramValue(*p, kHoldParamId) >= .5, values, style);
    drawing::drawHeaderButton(runButtonRect(),
        drawing::cocoaRect(kScenePanel.frame), "RUN",
        paramValue(*p, kRunParamId) >= .5, values, style);
    drawing::drawAtPoint_withAttributes("MORPH", drawing::makePoint(kScenePanel.frame.x + 14.0, 127.0), labels);
    const drawing::Rect morphRect = sceneMorphRect();
    drawMeterBar(morphRect,
        static_cast<float>(uiNormalizedValue(kSceneMorphParamId,
            paramValue(*p, kSceneMorphParamId))),
        drawing::color(0x686868));
    for (uint32_t scene = 0u; scene < 4u; ++scene) {
        const double x = morphRect.origin.x
            + morphRect.size.width * scene / 3.0;
        drawing::setStroke(drawing::color(0x8a8a8a));
        drawing::frameRect(drawing::makeRect(x, morphRect.origin.y, 1.0,
            morphRect.size.height));
    }

    const uint32_t selectedCell = static_cast<uint32_t>(std::clamp(
        std::round(paramValue(*p, kSelectedCellParamId)), 1.0, 8.0)) - 1u;
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        const float activity = std::clamp(
            p->activity[cell].load(std::memory_order_relaxed), 0.0f, 1.0f);
        const drawing::Rect rect = cellButtonRect(cell);
        drawing::setFill(drawing::calibratedRGB(.12 + activity * .50, .11 + activity * .26, .13 + activity * .08, 1.0));
        drawing::rectFill(rect);
        drawing::setStroke(drawing::color(selectedCell == cell ? 0xd0d0d0 : 0x555555));
        drawing::frameRectWithWidth(rect, selectedCell == cell ? 2.0 : 1.0);
        drawing::drawAtPoint_withAttributes(drawing::stringFormat("%u", cell + 1u), drawing::makePoint(rect.origin.x + 13.0,
                rect.origin.y + 8.0), tiny);
    }
    for (const auto& row : kObjectRows) {
        const clap_id id = row.base + selectedCell;
        const double value = paramValue(*p, id);
        char text[64] {};
        paramsValueToText(&p->plugin, id, value, text, sizeof(text));
        drawing::drawProcessorSlider(
            drawing::stringFromUtf8(row.label),
            drawing::stringFromUtf8(text), value,
            s3g::gui_layout::rowY(kObjectPanel, row.row),
            kObjectPanel.frame.x, kObjectPanel.frame.width,
            labels, values, style);
    }

    const double variation = paramValue(*p, kVariationParamId);
    const float performedVariation = s3g::processorFissureFormCurve(
        static_cast<float>(variation));
    drawing::drawProcessorSlider("CUT CONTRAST",
        drawing::stringFormat("Δ %.0f%%",
            performedVariation * 100.0f),
        variation, s3g::gui_layout::rowY(kGesturePanel, 0u),
        kGesturePanel.frame.x, kGesturePanel.frame.width,
        labels, values, style);
    static constexpr const char* actionLabels[] {
        "CUT + LINKS", "IMPACT ALL", "DROP OUT", "MUTATE OBJECT", "PANIC"
    };
    for (uint32_t action = 0u; action < 5u; ++action) {
        drawing::drawHeaderButton(actionButtonRect(action),
            drawing::cocoaRect(kGesturePanel.frame),
            drawing::stringFromUtf8(actionLabels[action]),
            paramValue(*p, kCutParamId + action) >= .5, values, style);
    }

    for (uint32_t index = 0u; index < 2u; ++index) {
        const drawing::Rect pad = fracturePadRect(index);
        drawing::setFill(drawing::color(0x111111));
        drawing::rectFill(pad);
        drawing::setStroke(drawing::color(index == 0u ? 0x6f4a3a : 0x355b68));
        drawing::frameRect(pad);
        for (uint32_t division = 1u; division < 4u; ++division) {
            const double x = pad.origin.x
                + pad.size.width * division / 4.0;
            const double y = pad.origin.y
                + pad.size.height * division / 4.0;
            drawing::setStroke(drawing::color(0x292929));
            drawing::Path::strokeLineFromPoint_toPoint(drawing::makePoint(x, pad.origin.y), drawing::makePoint(x, drawing::maxY(pad)));
            drawing::Path::strokeLineFromPoint_toPoint(drawing::makePoint(pad.origin.x, y), drawing::makePoint(drawing::maxX(pad), y));
        }
        if (_fractureTrailCount[index] > 1u) {
            for (uint32_t item = 1u;
                 item < _fractureTrailCount[index]; ++item) {
                const drawing::Point previous = _fractureTrail[index][item - 1u];
                const drawing::Point current = _fractureTrail[index][item];
                const drawing::Point from = drawing::makePoint(
                    pad.origin.x + 6.0
                        + previous.x * (pad.size.width - 12.0),
                    drawing::maxY(pad) - 6.0
                        - previous.y * (pad.size.height - 12.0));
                const drawing::Point to = drawing::makePoint(
                    pad.origin.x + 6.0
                        + current.x * (pad.size.width - 12.0),
                    drawing::maxY(pad) - 6.0
                        - current.y * (pad.size.height - 12.0));
                const float trailAlpha = std::clamp(std::min(
                    _fractureTrailAlpha[index][item - 1u],
                    _fractureTrailAlpha[index][item]), 0.0f, 1.0f);
                drawing::setStroke(drawing::calibratedRGB((index == 0u ? .92 : .28), (index == 0u ? .42 : .70), (index == 0u ? .16 : .88), trailAlpha * .76));
                drawing::Path::strokeLineFromPoint_toPoint(from, to);
            }
        }

        // Eight small telemetry cells show causal traversal (outline),
        // realized splices (bright cap), and current hole occupancy (fill).
        for (uint32_t cell = 0u; cell < 8u; ++cell) {
            const float affected = p->fractureGestureActivity[index][cell]
                .load(std::memory_order_relaxed);
            const float gap = p->gapActivity[cell].load(
                std::memory_order_relaxed);
            const float cut = p->cutActivity[cell].load(
                std::memory_order_relaxed);
            const drawing::Rect marker = drawing::makeRect(pad.origin.x + 4.0
                    + static_cast<double>(cell) * 11.5,
                pad.origin.y + 4.0, 9.0, 7.0);
            drawing::setFill(drawing::calibratedRGB(.08 + gap * .10, .08 + gap * .23, .08 + gap * .30, 1.0));
            drawing::rectFill(marker);
            drawing::setStroke(drawing::calibratedRGB((index == 0u ? .86 : .22), (index == 0u ? .34 : .62), (index == 0u ? .12 : .82), .16 + affected * .84));
            drawing::frameRect(marker);
            if (cut > .04f) {
                drawing::setFill(drawing::calibratedWhite(.88, std::min(1.0f, cut)));
                drawing::rectFill(drawing::makeRect(marker.origin.x + 1.0,
                    marker.origin.y + 1.0,
                    (marker.size.width - 2.0) * cut, 1.5));
            }
        }
        const double xValue = paramValue(*p, kFractureXParamIds[index]);
        const double yValue = paramValue(*p, kFractureYParamIds[index]);
        const drawing::Point point = drawing::makePoint(
            pad.origin.x + 6.0 + xValue * (pad.size.width - 12.0),
            drawing::maxY(pad) - 6.0 - yValue * (pad.size.height - 12.0));
        const float gestureEnergy = p->fractureGestureEnergy[index].load(
            std::memory_order_relaxed);
        const double halo = 10.0 + gestureEnergy * 7.0;
        drawing::setFill(drawing::calibratedRGB((index == 0u ? .86 : .20), (index == 0u ? .31 : .62), (index == 0u ? .12 : .78), .26 + gestureEnergy * .54));
        drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
            point.x - halo, point.y - halo, halo * 2.0, halo * 2.0)).fill();
        drawing::setStroke(drawing::color(index == 0u ? 0xe0a07d : 0x8ac9dc));
        drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
            point.x - 4.0, point.y - 4.0, 8.0, 8.0)).stroke();
    }
    drawing::drawAtPoint_withAttributes("CUT · RATE →", drawing::makePoint(664.0, 716.0), tiny);
    drawing::drawAtPoint_withAttributes("VOID ↑", drawing::makePoint(662.0, 566.0), tiny);
    drawing::drawAtPoint_withAttributes("SHAPE · EDGE →", drawing::makePoint(772.0, 716.0), tiny);
    drawing::drawAtPoint_withAttributes("SPACE ↑", drawing::makePoint(770.0, 566.0), tiny);
    static constexpr const char* latchLabels[] {
        "CUT LATCH", "SHAPE LATCH"
    };
    for (uint32_t index = 0u; index < 2u; ++index) {
        drawing::drawHeaderButton(fractureLatchButtonRect(index),
            drawing::cocoaRect(kFracturePanel.frame),
            drawing::stringFromUtf8(latchLabels[index]),
            paramValue(*p, kFractureLatchParamIds[index]) >= .5,
            values, style);
    }
    const bool grabbing = p->grabbingStatus.load(std::memory_order_relaxed);
    const bool grabbed = p->grabbed.load(std::memory_order_relaxed);
    const float grabDuration = p->grabDuration.load(
        std::memory_order_relaxed);
    drawing::drawHeaderButton(grabButtonRect(),
        drawing::cocoaRect(kFracturePanel.frame),
        grabbing ? "STOP GRAB" : "GRAB", grabbing, values, style);
    drawing::drawHeaderButton(repeatButtonRect(),
        drawing::cocoaRect(kFracturePanel.frame), "REPEAT",
        p->repeatMix.load(std::memory_order_relaxed) > .08f,
        values, style);
    drawing::String  loopStatus = grabbing
        ? drawing::stringFormat("REC %.1fs", grabDuration)
        : (grabbed ? drawing::stringFormat("LOOP %.1fs", grabDuration)
                   : "LOOP EMPTY");
    drawing::drawAtPoint_withAttributes(loopStatus, drawing::makePoint(888.0, 674.0), tiny);
    drawing::drawAtPoint_withAttributes(drawing::stringFormat("RPT %02.0f%%",
        p->repeatPhase.load(std::memory_order_relaxed) * 100.0f), drawing::makePoint(888.0, 690.0), tiny);
    uint32_t occupiedGaps = 0u;
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        if (p->gapActivity[cell].load(std::memory_order_relaxed) > .55f) {
            ++occupiedGaps;
        }
    }
    drawing::Attrs  gapAttrs = occupiedGaps >= 7u
        ? drawing::textAttrs(
            drawing::calibratedRGB(.95, .31, .16, 1.0),
            8.0)
        : tiny;
    drawing::drawAtPoint_withAttributes(drawing::stringFormat(occupiedGaps >= 7u
            ? "SILENCE GUARD %u/8" : "REALIZED GAPS %u/8",
        occupiedGaps), drawing::makePoint(888.0, 700.0), gapAttrs);

    if (_presetMenuOpen) {
        drawing::String  presetItems[kFactoryPresetCount] {};
        for (uint32_t index = 0u; index < kFactoryPresetCount; ++index) {
            presetItems[index] = drawing::stringFromUtf8(factoryPresetName(index));
        }
        drawing::drawMultiColumnDropdownMenu(
            presetDropdownRect(), kPresetMenuItemHeight,
            presetItems, kFactoryPresetCount, kPresetMenuColumns,
            presetIndex < kFactoryPresetCount
                ? static_cast<int>(presetIndex) : -1,
            _hoverPreset, values, style);
    }

}

void updateDraggedParam(drawing::Point point)  {
    if (_dragParam <= 0) return;
    const auto id = static_cast<clap_id>(_dragParam);
    double x = 0.0;
    double width = 0.0;
    const auto row = std::find_if(kUiRows.begin(), kUiRows.end(),
        [=](const FissureUiRow& candidate) { return candidate.id == id; });
    if (row != kUiRows.end()) {
        x = s3g::gui_layout::processorControlX(row->panel->frame.x);
        width = s3g::gui_layout::processorTrackWidth(row->panel->frame.width);
    } else if (id == kVariationParamId) {
        x = s3g::gui_layout::processorControlX(kGesturePanel.frame.x);
        width = s3g::gui_layout::processorTrackWidth(kGesturePanel.frame.width);
    } else if (id == kSceneMorphParamId) {
        const drawing::Rect rect = sceneMorphRect();
        x = rect.origin.x;
        width = rect.size.width;
    } else if (id == kTopologyMixParamId) {
        const drawing::Rect rect = topologyMixRect();
        x = rect.origin.x;
        width = rect.size.width;
    } else if (isMatrixParam(id)) {
        const drawing::Rect rect = routeSliderRect();
        x = rect.origin.x;
        width = rect.size.width;
    } else if (isCellLevelParam(id) || isObjectParam(id)) {
        x = s3g::gui_layout::processorControlX(kObjectPanel.frame.x);
        width = s3g::gui_layout::processorTrackWidth(kObjectPanel.frame.width);
    } else {
        return;
    }
    const double normalized = std::clamp((point.x - x) / width, 0.0, 1.0);
    auto* p = static_cast<Plugin*>(_plugin);
    queueGuiParamValue(*p, id, uiValueFromNormalized(id, normalized));
    std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
    setNeedsDisplay(true);

}

void paintMatrixAt(drawing::Point point)  {
    if (!_matrixPainting) return;
    auto* p = static_cast<Plugin*>(_plugin);
    for (uint32_t destination = 0u; destination < 8u; ++destination) {
        for (uint32_t source = 0u; source < 8u; ++source) {
            if (!drawing::contains(point, matrixCellRect(destination, source))) {
                continue;
            }
            const clap_id id = kMatrixParamBase + destination * 8u + source;
            if (_lastPaintedRoute == static_cast<int>(id)) return;
            _lastPaintedRoute = static_cast<int>(id);
            _selectedSource = source;
            _selectedDestination = destination;
            queueGuiParamGesture(*p, id, _matrixPaintValue);
            std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
            setNeedsDisplay(true);
            return;
        }
    }

}

void paintCutMaskAt(drawing::Point point)  {
    if (!_cutMaskPainting) return;
    auto* p = static_cast<Plugin*>(_plugin);
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        if (!drawing::contains(point, cutMaskButtonRect(cell))) continue;
        const clap_id id = kCutMaskParamBase + cell;
        if (_lastPaintedMask == static_cast<int>(id)) return;
        _lastPaintedMask = static_cast<int>(id);
        queueGuiParamGesture(*p, id, _cutMaskPaintValue);
        std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
        setNeedsDisplay(true);
        return;
    }

}

void updateFracturePad(drawing::Point point)  {
    if (_fracturePad < 0 || _fracturePad > 1) return;
    auto* p = static_cast<Plugin*>(_plugin);
    const uint32_t index = static_cast<uint32_t>(_fracturePad);
    const drawing::Rect rect = fracturePadRect(index);
    const double x = std::clamp((point.x - rect.origin.x)
        / rect.size.width, 0.0, 1.0);
    const double y = std::clamp((drawing::maxY(rect) - point.y)
        / rect.size.height, 0.0, 1.0);
    const clap_id xId = kFractureXParamIds[index];
    const clap_id yId = kFractureYParamIds[index];
    queueGuiParamValue(*p, xId, x);
    queueGuiParamValue(*p, yId, y);

    const double now = drawing::uptime();
    if (_fractureMotionReady[index]) {
        const double deltaX = x - _fractureLastX[index];
        const double deltaY = y - _fractureLastY[index];
        const double distance = std::sqrt(
            deltaX * deltaX + deltaY * deltaY);
        const double elapsed = std::clamp(
            now - _fractureLastTime[index], 1.0 / 240.0, 0.12);
        if (distance > 0.001) {
            if (_fractureTrailCount[index] == kFractureTrailCapacity) {
                for (uint32_t item = 1u;
                     item < kFractureTrailCapacity; ++item) {
                    _fractureTrail[index][item - 1u]
                        = _fractureTrail[index][item];
                    _fractureTrailAlpha[index][item - 1u]
                        = _fractureTrailAlpha[index][item];
                }
                --_fractureTrailCount[index];
            }
            const uint32_t trailIndex = _fractureTrailCount[index]++;
            _fractureTrail[index][trailIndex] = drawing::makePoint(x, y);
            _fractureTrailAlpha[index][trailIndex] = 1.0f;

            const double speed = distance / elapsed;
            const double energy = std::clamp(
                (speed - 0.18) / 5.2, 0.0, 1.0);
            if (energy > 0.005) {
                queueGuiParamValue(*p, kFractureMotionXParamIds[index],
                    deltaX / distance);
                queueGuiParamValue(*p, kFractureMotionYParamIds[index],
                    deltaY / distance);
                // Energy is queued last: it commits the already-published
                // vector as one audio-thread gesture impulse.
                queueGuiParamValue(*p,
                    kFractureMotionEnergyParamIds[index], energy);
            }
        }
    }
    _fractureLastX[index] = x;
    _fractureLastY[index] = y;
    _fractureLastTime[index] = now;
    _fractureMotionReady[index] = true;
    setNeedsDisplay(true);

}

void mouseMoved(drawing::Event*  event)  {
    if (!_presetMenuOpen) return;
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    const int hover = drawing::multiColumnDropdownHitIndex(
        point, presetDropdownRect(), kPresetMenuItemHeight,
        kFactoryPresetCount, kPresetMenuColumns);
    if (hover != _hoverPreset) {
        _hoverPreset = hover;
        setNeedsDisplay(true);
    }

}

void mouseExited(drawing::Event*  event)  {
    (void)event;
    if (_hoverPreset != -1) {
        _hoverPreset = -1;
        setNeedsDisplay(true);
    }

}

void mouseDown(drawing::Event*  event)  {
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    const auto titleBand = drawing::encoderTitleBand(
        kGuiWidth, kGuiHeight);

    if (_presetMenuOpen) {
        const int preset = drawing::multiColumnDropdownHitIndex(
            point, presetDropdownRect(), kPresetMenuItemHeight,
            kFactoryPresetCount, kPresetMenuColumns);
        _presetMenuOpen = false;
        _hoverPreset = -1;
        if (preset >= 0) {
            queueGuiParamGesture(*p, kPresetParamId, preset);
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                factoryPresetName(static_cast<uint32_t>(preset)));
            setNeedsDisplay(true);
            return;
        }
    }
    if (drawing::contains(point, drawing::cocoaRect(
            titleBand.presetMenu))) {
        _presetMenuOpen = !_presetMenuOpen;
        setNeedsDisplay(true);
        return;
    }
    if (handleProcessorTitleClick(point, &p->plugin,
            "Processor Fissure", titleBand,
            _presetName, sizeof(_presetName), kOutputParamId)) {
        setNeedsDisplay(true);
        return;
    }

    for (uint32_t mode = 0u; mode < 3u; ++mode) {
        if (drawing::contains(point, outputModeButtonRect(mode))) {
            queueGuiParamGesture(*p, kOutputModeParamId, mode);
            std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
            return;
        }
    }
    for (uint32_t scene = 0u; scene < 4u; ++scene) {
        if (drawing::contains(point, sceneButtonRect(scene))) {
            queueGuiParamGesture(*p, kSceneParamId, scene + 1u);
            return;
        }
    }
    if (drawing::contains(point, storeButtonRect())) {
        queueGuiPulse(*p, kCaptureParamId);
        return;
    }
    if (drawing::contains(point, holdButtonRect())) {
        queueGuiParamGesture(*p, kHoldParamId,
            paramValue(*p, kHoldParamId) >= .5 ? 0.0 : 1.0);
        return;
    }
    if (drawing::contains(point, runButtonRect())) {
        queueGuiParamGesture(*p, kRunParamId,
            paramValue(*p, kRunParamId) >= .5 ? 0.0 : 1.0);
        return;
    }
    if (drawing::contains(point, sceneMorphRect())) {
        _dragParam = static_cast<int>(kSceneMorphParamId);
        queueGuiParamGestureBegin(*p, kSceneMorphParamId);
        updateDraggedParam(point);
        return;
    }
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        if (drawing::contains(point, cutMaskButtonRect(cell))) {
            const clap_id id = kCutMaskParamBase + cell;
            _cutMaskPaintValue = paramValue(*p, id) >= .5 ? 0.0 : 1.0;
            _cutMaskPainting = true;
            _lastPaintedMask = -1;
            paintCutMaskAt(point);
            return;
        }
    }
    for (uint32_t index = 0u; index < 2u; ++index) {
        if (!drawing::contains(point, fractureLatchButtonRect(index))) continue;
        const clap_id id = kFractureLatchParamIds[index];
        queueGuiParamGesture(*p, id,
            paramValue(*p, id) >= .5 ? 0.0 : 1.0);
        return;
    }
    for (uint32_t index = 0u; index < 2u; ++index) {
        if (!drawing::contains(point, fracturePadRect(index))) continue;
        _fracturePad = static_cast<int>(index);
        const clap_id xId = kFractureXParamIds[index];
        const clap_id yId = kFractureYParamIds[index];
        _fractureLastX[index] = paramValue(*p, xId);
        _fractureLastY[index] = paramValue(*p, yId);
        _fractureLastTime[index]
            = drawing::uptime() - 1.0 / 60.0;
        _fractureMotionReady[index] = true;
        queueGuiParamGestureBegin(*p, xId);
        queueGuiParamGestureBegin(*p, yId);
        updateFracturePad(point);
        return;
    }
    if (drawing::contains(point, grabButtonRect())) {
        queueGuiParamGesture(*p, kGrabParamId,
            paramValue(*p, kGrabParamId) >= .5 ? 0.0 : 1.0);
        return;
    }
    if (drawing::contains(point, repeatButtonRect())) {
        _repeatPressed = true;
        queueGuiParamGestureBegin(*p, kRepeatParamId);
        queueGuiParamValue(*p, kRepeatParamId, 1.0);
        return;
    }
    for (uint32_t shape = 0u; shape < 6u; ++shape) {
        if (drawing::contains(point, topologyButtonRect(shape))) {
            queueGuiParamGesture(*p, kTopologyShapeParamId, shape);
            queueGuiPulse(*p, kApplyTopologyParamId);
            std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
            return;
        }
    }
    if (drawing::contains(point, topologyMixRect())) {
        _dragParam = static_cast<int>(kTopologyMixParamId);
        queueGuiParamGestureBegin(*p, kTopologyMixParamId);
        updateDraggedParam(point);
        return;
    }
    for (uint32_t action = 0u; action < 5u; ++action) {
        if (drawing::contains(point, actionButtonRect(action))) {
            queueGuiPulse(*p, kCutParamId + action);
            return;
        }
    }
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        if (drawing::contains(point, cellButtonRect(cell))) {
            queueGuiParamGesture(*p, kSelectedCellParamId, cell + 1u);
            queueGuiPulse(*p, kStrikeParamId);
            return;
        }
    }
    for (uint32_t destination = 0u; destination < 8u; ++destination) {
        for (uint32_t source = 0u; source < 8u; ++source) {
            if (!drawing::contains(point, matrixCellRect(destination, source))) {
                continue;
            }
            _selectedSource = source;
            _selectedDestination = destination;
            const clap_id id = kMatrixParamBase + destination * 8u + source;
            const double current = paramValue(*p, id);
            _matrixPaintValue = std::abs(current) < .01
                ? .34 : (current > 0.0 ? -.34 : 0.0);
            _matrixPainting = true;
            _lastPaintedRoute = -1;
            paintMatrixAt(point);
            return;
        }
    }
    const clap_id routeId = kMatrixParamBase
        + _selectedDestination * 8u + _selectedSource;
    if (drawing::contains(point, routePolarityRect())) {
        queueGuiParamGesture(*p, routeId, -paramValue(*p, routeId));
        std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
        return;
    }
    if (drawing::contains(point, routeClearRect())) {
        queueGuiParamGesture(*p, routeId, 0.0);
        std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
        return;
    }
    if (drawing::contains(point, routeSliderRect())) {
        _dragParam = static_cast<int>(routeId);
        queueGuiParamGestureBegin(*p, routeId);
        updateDraggedParam(point);
        return;
    }
    if (drawing::contains(point, drawing::cocoaRect(
            s3g::gui_layout::sliderHitRect(kGesturePanel, 0u)))) {
        _dragParam = static_cast<int>(kVariationParamId);
        queueGuiParamGestureBegin(*p, kVariationParamId);
        updateDraggedParam(point);
        return;
    }
    const uint32_t selectedCell = static_cast<uint32_t>(std::clamp(
        std::round(paramValue(*p, kSelectedCellParamId)), 1.0, 8.0)) - 1u;
    for (const auto& row : kObjectRows) {
        if (!drawing::contains(point, drawing::cocoaRect(
                s3g::gui_layout::sliderHitRect(kObjectPanel, row.row)))) {
            continue;
        }
        _dragParam = static_cast<int>(row.base + selectedCell);
        queueGuiParamGestureBegin(*p, static_cast<clap_id>(_dragParam));
        updateDraggedParam(point);
        return;
    }
    for (const auto& row : kUiRows) {
        if (!drawing::contains(point, drawing::cocoaRect(
                s3g::gui_layout::sliderHitRect(*row.panel, row.row)))) {
            continue;
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
        return;
    }

}

void mouseDragged(drawing::Event*  event)  {
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    if (_matrixPainting) paintMatrixAt(point);
    else if (_cutMaskPainting) paintCutMaskAt(point);
    else if (_fracturePad >= 0) updateFracturePad(point);
    else if (_dragParam > 0) updateDraggedParam(point);

}

void mouseUp(drawing::Event*  event)  {
    (void)event;
    if (_dragParam > 0) {
        queueGuiParamGestureEnd(*static_cast<Plugin*>(_plugin),
            static_cast<clap_id>(_dragParam));
    }
    auto* p = static_cast<Plugin*>(_plugin);
    if (_fracturePad >= 0 && p) {
        const uint32_t index = static_cast<uint32_t>(_fracturePad);
        const clap_id xId = kFractureXParamIds[index];
        const clap_id yId = kFractureYParamIds[index];
        if (paramValue(*p, kFractureLatchParamIds[index]) < .5) {
            queueGuiParamValue(*p, xId, 0.0);
            queueGuiParamValue(*p, yId, 0.0);
        }
        queueGuiParamGestureEnd(*p, xId);
        queueGuiParamGestureEnd(*p, yId);
        _fractureMotionReady[index] = false;
    }
    if (_repeatPressed && p) {
        queueGuiParamValue(*p, kRepeatParamId, 0.0);
        queueGuiParamGestureEnd(*p, kRepeatParamId);
    }
    _dragParam = -1;
    _matrixPainting = false;
    _cutMaskPainting = false;
    _fracturePad = -1;
    _repeatPressed = false;
    _lastPaintedRoute = -1;
    _lastPaintedMask = -1;

}

};
}
s3g::portable_gui::foundation::ContentView* makePortableCanvas(Plugin& p) { return new feedback_processor_canvas::Editor(p); }
#include "s3g_clap_canvas_gui.inc"
