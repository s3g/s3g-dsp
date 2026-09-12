// Adapted from retained Cocoa geometry, drawing order and interaction maps.
namespace feedback_processor_canvas {
namespace drawing = s3g::portable_gui::feedback_drawing;
namespace foundation = s3g::portable_gui::foundation;
constexpr auto& presetState = stateExt;

constexpr double kLeftPanelX = 16.0;
constexpr double kRightPanelX = 470.0;
constexpr double kPanelWidth = 434.0;
constexpr auto kGrammarPanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::EventTiming,
    { kLeftPanelX, 50.0, kPanelWidth, 216.0 }, 36.0, 24.0, 7u };
constexpr auto kVoicePanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Source,
    { kLeftPanelX, 278.0, kPanelWidth, 288.0 }, 36.0, 24.0, 10u };
constexpr auto kStereoPanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Relationships,
    { kRightPanelX, 50.0, kPanelWidth, 96.0 }, 36.0, 24.0, 2u };
constexpr auto kPerformancePanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Output,
    { kRightPanelX, 158.0, kPanelWidth, 144.0 }, 36.0, 24.0, 4u };
constexpr s3g::gui_layout::Rect kAncestryFrame {
    kRightPanelX, 314.0, kPanelWidth, 200.0 };

struct ErrantUiRow {
    clap_id id;
    const char* label;
    const s3g::gui_layout::Panel* panel;
    uint32_t row;
};

constexpr std::array<ErrantUiRow, kSavedParamCount> kUiRows {{
    { kModeParamId, "MODE", &kGrammarPanel, 0u },
    { kSpanParamId, "SPAN", &kGrammarPanel, 1u },
    { kDensityParamId, "DENSITY", &kGrammarPanel, 2u },
    { kAncestryParamId, "ANCESTRY", &kGrammarPanel, 3u },
    { kMutationParamId, "MUTATION", &kGrammarPanel, 4u },
    { kRepeatParamId, "REPEAT", &kGrammarPanel, 5u },
    { kCoherenceParamId, "COHERENCE", &kGrammarPanel, 6u },
    { kMaterialParamId, "GROWL", &kVoicePanel, 0u },
    { kRegisterParamId, "REGISTER", &kVoicePanel, 1u },
    { kKeyRoleParamId, "KEY ROLE", &kVoicePanel, 2u },
    { kNoteTrackingParamId, "PITCH GRAVITY", &kVoicePanel, 3u },
    { kToneParamId, "CUTOFF", &kVoicePanel, 4u },
    { kResonanceParamId, "RESONANCE", &kVoicePanel, 5u },
    { kFilterContourParamId, "CONTOUR", &kVoicePanel, 6u },
    { kSubParamId, "SUB", &kVoicePanel, 7u },
    { kDriveParamId, "DRIVE", &kVoicePanel, 8u },
    { kCrosswireParamId, "CROSSWIRE", &kVoicePanel, 9u },
    { kTopologyParamId, "TOPOLOGY", &kStereoPanel, 0u },
    { kWidthParamId, "WIDTH", &kStereoPanel, 1u },
    { kOutputParamId, "OUTPUT", &kPerformancePanel, 0u },
    { kSeedParamId, "SEED", &kPerformancePanel, 1u },
    { kVelocityParamId, "VELOCITY", &kPerformancePanel, 2u },
    { kMidiReceiveParamId, "MIDI RECEIVE", &kPerformancePanel, 3u },
}};

double uiNormalizedValue(clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return 0.0;
    return std::clamp((value - def->minimum)
        / std::max(1.0e-12, def->maximum - def->minimum), 0.0, 1.0);
}

double uiValueFromNormalized(clap_id id, double normalized)
{
    const auto* def = paramDef(id);
    if (!def) return 0.0;
    normalized = std::clamp(normalized, 0.0, 1.0);
    return clampValue(*def,
        def->minimum + (def->maximum - def->minimum) * normalized);
}


class Editor final : public s3g::portable_gui::routing::Canvas {
public:
using Canvas = s3g::portable_gui::routing::Canvas;

    Plugin* _plugin;
    int _dragParam;
    bool _midiReceiveMenuOpen;
    int _midiReceiveMenuHover;
    char _presetName[64];
#include "s3g_feedback_processor_editor.inc"
explicit Editor(Plugin& plugin) : Canvas(kGuiWidth,kGuiHeight) {_plugin = &plugin;
        _dragParam = -1;
        _midiReceiveMenuOpen = false;
        _midiReceiveMenuHover = -1;
        std::snprintf(_presetName, sizeof(_presetName), "%s", "INIT");
}

void service() override {
    
    auto* p = static_cast<Plugin*>(_plugin);
    if (!isHidden() && p && true) {
        setNeedsDisplay(true);
    }

}

drawing::Rect midiReceiveMenuRect()  {
    const double rowY = s3g::gui_layout::rowY(kPerformancePanel, 3u);
    const drawing::Rect anchor = drawing::makeRect(
        s3g::gui_layout::processorControlX(kPerformancePanel.frame.x),
        rowY - 1.0,
        s3g::gui_layout::processorMenuWidth(kPerformancePanel.frame.width),
        15.0);
    const double height = 18.0 * static_cast<double>(
        drawing::multiColumnMenuRows(
            drawing::kMidiReceiveMenuItemCount,
            drawing::kMidiChannelMenuColumns));
    double y = drawing::maxY(anchor) + 2.0;
    if (y + height > kGuiHeight) y = anchor.origin.y - 2.0 - height;
    return drawing::makeRect(anchor.origin.x, y, anchor.size.width, height);

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
    const auto titleBand = drawing::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    const float activity = std::clamp(
        p->visualActivity.load(std::memory_order_relaxed), 0.0f, 1.0f);
    drawing::drawProcessorTitleBand(
        "s3g PROCESSOR ERRANT",
        drawing::stringFromUtf8(_presetName),
        drawing::stringFormat("PK %3.0f%%", activity * 100.0f),
        titleBand, drawing::softTitleAttrs(), labels, values, style);

    const auto drawPanel = [&](drawing::String  name,
                               const s3g::gui_layout::Panel& panel) {
        drawing::drawPanelFrame(panel.frame.x, panel.frame.y,
            panel.frame.width, panel.frame.height, style);
        drawing::drawPanelHeader(name, true,
            panel.frame.x, panel.frame.y, panel.frame.width,
            s3g::gui_layout::kStandardMetrics.headerHeight, labels, style);
    };
    drawPanel("GENEALOGY / EVENT GRAMMAR", kGrammarPanel);
    drawPanel("BASS LAB / MIDI-ROOTED LOW CORE", kVoicePanel);
    drawPanel("STEREO / UPPER BODY", kStereoPanel);
    drawPanel("PATCH / PERFORMANCE", kPerformancePanel);
    drawing::drawPanelFrame(kAncestryFrame.x, kAncestryFrame.y,
        kAncestryFrame.width, kAncestryFrame.height, style);
    drawing::drawPanelHeader("2 CHANNEL OUTPUT / FAMILY ARCHIVE", true,
        kAncestryFrame.x, kAncestryFrame.y, kAncestryFrame.width,
        s3g::gui_layout::kStandardMetrics.headerHeight, labels, style);

    for (const auto& row : kUiRows) {
        const double value = paramValue(*p, row.id);
        char text[64] {};
        paramsValueToText(&p->plugin, row.id, value, text, sizeof(text));
        if (row.id == kMidiReceiveParamId) {
            drawing::drawProcessorMenu(
                drawing::stringFromUtf8(row.label),
                drawing::stringFromUtf8(text),
                s3g::gui_layout::rowY(*row.panel, row.row),
                row.panel->frame.x, row.panel->frame.width,
                labels, values, style);
        } else {
            drawing::drawProcessorSlider(
                drawing::stringFromUtf8(row.label),
                drawing::stringFromUtf8(text),
                uiNormalizedValue(row.id, value),
                s3g::gui_layout::rowY(*row.panel, row.row),
                row.panel->frame.x, row.panel->frame.width,
                labels, values, style);
        }
    }

    const drawing::Rect trigger = drawing::makeRect(
        kAncestryFrame.x + 16.0, kAncestryFrame.y + 154.0,
        kAncestryFrame.width - 32.0, 25.0);
    drawing::drawHeaderButton(trigger,
        drawing::cocoaRect(kAncestryFrame), "GENERATE",
        activity > 0.02f, values, style);

    const drawing::Rect scope = drawing::makeRect(kAncestryFrame.x + 16.0,
        kAncestryFrame.y + 32.0, kAncestryFrame.width - 32.0, 70.0);
    drawing::setFill(style.cellBg);
    drawing::rectFill(scope);
    drawing::setStroke(drawing::calibratedWhite(0.34, 0.36));
    for (uint32_t lane = 0u; lane < 2u; ++lane) {
        const double y = scope.origin.y + scope.size.height
            * (0.25 + 0.50 * lane);
        drawing::Path  rail = drawing::Path::bezierPath();
        rail.moveToPoint(drawing::makePoint(scope.origin.x, y));
        rail.lineToPoint(drawing::makePoint(drawing::maxX(scope), y));
        rail.setLineWidth(0.7);
        rail.stroke();
    }
    const uint32_t scopeWrite = p->visualScopeWrite.load(
        std::memory_order_acquire);
    const std::array<VSTGUI::CColor , 2u> traceColors {{
        drawing::calibratedRGB(0.35, 0.73, 0.79, 0.58 + activity * 0.38),
        drawing::calibratedRGB(0.56, 0.46, 0.78, 0.58 + activity * 0.38),
    }};
    for (uint32_t lane = 0u; lane < 2u; ++lane) {
        drawing::Path  trace = drawing::Path::bezierPath();
        trace.setLineWidth(0.9 + activity * 0.8);
        const double center = scope.origin.y + scope.size.height
            * (0.25 + 0.50 * lane);
        for (uint32_t point = 0u; point < kVisualScopeFrames; ++point) {
            const uint32_t index = (scopeWrite + point) % kVisualScopeFrames;
            const float sample = lane == 0u
                ? p->visualScopeL[index].load(std::memory_order_relaxed)
                : p->visualScopeR[index].load(std::memory_order_relaxed);
            const double x = scope.origin.x + scope.size.width
                * point / static_cast<double>(kVisualScopeFrames - 1u);
            const double y = center - std::clamp(sample, -1.0f, 1.0f) * 13.0;
            if (point == 0u) trace.moveToPoint(drawing::makePoint(x, y));
            else trace.lineToPoint(drawing::makePoint(x, y));
        }
        drawing::setStroke(traceColors[lane]);
        trace.stroke();
        drawing::drawAtPoint_withAttributes(drawing::stringFormat("%u", lane + 1u), drawing::makePoint(scope.origin.x + 5.0, center - 7.0), labels);
    }
    const int32_t note = p->visualNote.load(std::memory_order_relaxed);
    const int32_t interval = p->visualInterval.load(std::memory_order_relaxed);
    const uint32_t generation = p->visualGeneration.load(
        std::memory_order_relaxed);
    static const char* noteNames[] {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };
    drawing::String  noteText = note >= 0
        ? drawing::stringFormat("MIDI %s%d    INTERVAL %+d    BRANCH %03u",
            noteNames[note % 12], note / 12 - 1, interval, generation)
        : "MIDI —    FAMILY ARCHIVE READY";
    drawing::drawAtPoint_withAttributes(noteText, drawing::makePoint(kAncestryFrame.x + 24.0,
        kAncestryFrame.y + 110.0), labels);
    drawing::drawAtPoint_withAttributes("MIDI ROOT → OSC SPINE → LADDER → PROTECTED SUB", drawing::makePoint(kAncestryFrame.x + 24.0,
            kAncestryFrame.y + 126.0), labels);
    drawing::drawAtPoint_withAttributes("FAMILY ARCHIVE → CROSSWIRE → UPPER BODY", drawing::makePoint(kAncestryFrame.x + 24.0,
            kAncestryFrame.y + 141.0), labels);
    if (_midiReceiveMenuOpen) {
        drawing::drawMidiReceiveDropdownMenu(
            midiReceiveMenuRect(), 18.0,
            static_cast<int>(std::lround(
                paramValue(*p, kMidiReceiveParamId))),
            _midiReceiveMenuHover, values, style);
    }

}

void updateDraggedParam(drawing::Point point)  {
    if (_dragParam <= 0) return;
    const auto id = static_cast<clap_id>(_dragParam);
    const auto row = std::find_if(kUiRows.begin(), kUiRows.end(),
        [=](const ErrantUiRow& candidate) { return candidate.id == id; });
    if (row == kUiRows.end()) return;
    const double controlX = s3g::gui_layout::processorControlX(
        row->panel->frame.x);
    const double trackWidth = s3g::gui_layout::processorTrackWidth(
        row->panel->frame.width);
    const double normalized = std::clamp(
        (point.x - controlX) / trackWidth, 0.0, 1.0);
    auto* p = static_cast<Plugin*>(_plugin);
    queueGuiParamValue(*p, id, uiValueFromNormalized(id, normalized));
    std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
    setNeedsDisplay(true);

}

void mouseDown(drawing::Event*  event)  {
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    if (_midiReceiveMenuOpen) {
        const int hit = drawing::midiReceiveDropdownHitIndex(
            point, midiReceiveMenuRect(), 18.0);
        if (hit >= 0)
            queueGuiParamGesture(*p, kMidiReceiveParamId, hit);
        _midiReceiveMenuOpen = false;
        _midiReceiveMenuHover = -1;
        setNeedsDisplay(true);
        return;
    }
    const auto titleBand = drawing::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    if (handleProcessorTitleClick(point, &p->plugin,
            "Processor Errant", titleBand,
            _presetName, sizeof(_presetName), kOutputParamId)) {
        setNeedsDisplay(true);
        return;
    }
    const drawing::Rect trigger = drawing::makeRect(
        kAncestryFrame.x + 16.0, kAncestryFrame.y + 154.0,
        kAncestryFrame.width - 32.0, 25.0);
    if (drawing::contains(point, trigger)) {
        queueGuiTrigger(*p);
        setNeedsDisplay(true);
        return;
    }
    for (const auto& row : kUiRows) {
        if (!drawing::contains(point, drawing::cocoaRect(
                s3g::gui_layout::sliderHitRect(*row.panel, row.row)))) {
            continue;
        }
        if (row.id == kMidiReceiveParamId) {
            _midiReceiveMenuOpen = true;
            _midiReceiveMenuHover = -1;
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
        if (row.id != kOutputParamId && row.id != kMidiReceiveParamId) {
            std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
        }
        return;
    }

}

void mouseDragged(drawing::Event*  event)  {
    if (_dragParam > 0) {
        updateDraggedParam(convertPoint_fromView(event->locationInWindow(), nullptr));
    }

}

void mouseUp(drawing::Event*  event)  {
    (void)event;
    if (_dragParam > 0) {
        queueGuiParamGestureEnd(*static_cast<Plugin*>(_plugin),
            static_cast<clap_id>(_dragParam));
    }
    _dragParam = -1;

}

void mouseMoved(drawing::Event*  event)  {
    if (!_midiReceiveMenuOpen) return;
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    const int hover = drawing::midiReceiveDropdownHitIndex(
        point, midiReceiveMenuRect(), 18.0);
    if (hover != _midiReceiveMenuHover) {
        _midiReceiveMenuHover = hover;
        setNeedsDisplay(true);
    }

}

};
}
s3g::portable_gui::foundation::ContentView* makePortableCanvas(Plugin& p) { return new feedback_processor_canvas::Editor(p); }
#include "s3g_clap_canvas_gui.inc"
