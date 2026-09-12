// Adapted from retained Cocoa geometry, drawing order and interaction maps.
namespace feedback_processor_canvas {
namespace drawing = s3g::portable_gui::feedback_drawing;
namespace foundation = s3g::portable_gui::foundation;
constexpr auto& presetState = stateExt;
struct EnvelopeGraphGeometry {
    drawing::Rect frame;
    double top;
    double bottom;
    drawing::Point start;
    drawing::Point attack;
    drawing::Point decay;
    drawing::Point sustain;
    drawing::Point release;
};

EnvelopeGraphGeometry envelopeGraphGeometry(const FaultReadout& plugin)
{
    EnvelopeGraphGeometry graph {};
    graph.frame = drawing::makeRect(kEnvelopeX, kEnvelopeY, kEnvelopeWidth, kEnvelopeHeight);
    graph.top = graph.frame.origin.y + 10.0;
    graph.bottom = drawing::maxY(graph.frame) - 10.0;
    const double sustainY = graph.bottom
        - static_cast<double>(std::clamp(plugin.sustain, 0.0f, 1.0f)) * (graph.bottom - graph.top);
    graph.start = drawing::makePoint(kEnvelopeX + 14.0, graph.bottom);
    graph.attack = drawing::makePoint(
        kEnvelopeX + 54.0
            + static_cast<double>(normalizedPerformanceParam(plugin, kAttackParamId)) * 126.0,
        graph.top);
    graph.decay = drawing::makePoint(
        kEnvelopeX + 220.0
            + static_cast<double>(normalizedPerformanceParam(plugin, kDecayParamId)) * 140.0,
        sustainY);
    graph.sustain = drawing::makePoint(kEnvelopeX + 460.0, sustainY);
    graph.release = drawing::makePoint(
        kEnvelopeX + 520.0
            + static_cast<double>(normalizedPerformanceParam(plugin, kReleaseParamId)) * 160.0,
        graph.bottom);
    return graph;
}

double squaredDistance(drawing::Point a, drawing::Point b)
{
    const double x = a.x - b.x;
    const double y = a.y - b.y;
    return x * x + y * y;
}

drawing::Rect midiReceiveDropdownRect()
{
    constexpr double itemHeight = 18.0;
    constexpr double itemCount = 17.0;
    return drawing::makeRect(
        s3g::gui_layout::processorControlX(kMidiReceivePanelX),
        kPerformanceRowY - 3.0 - itemHeight * itemCount,
        s3g::gui_layout::processorMenuWidth(kMidiReceivePanelWidth),
        itemHeight * itemCount);
}

drawing::Rect outputFormatDropdownRect()
{
    constexpr double itemHeight = 18.0;
    constexpr double itemCount = 3.0;
    return drawing::makeRect(
        s3g::gui_layout::processorControlX(kOutputPanelX),
        kOutputFormatRowY + 14.0,
        s3g::gui_layout::processorMenuWidth(kOutputPanelWidth),
        itemHeight * itemCount);
}

class Editor final : public s3g::portable_gui::routing::Canvas {
public:
using Canvas = s3g::portable_gui::routing::Canvas;

    Plugin* _plugin;
    FaultReadout _readout;
    int _dragSlider;
    int _openMenu;
    int _hoverMenuItem;
    int _editorPage;
    char _titlePresetName[64];
#include "s3g_feedback_processor_editor.inc"
explicit Editor(Plugin& plugin) : Canvas(kGuiWidth,kGuiHeight) {_plugin = &plugin;
        refreshFaultReadout(plugin, _readout);
        _dragSlider = -1;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _editorPage = 0;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CURRENT");
}

void service() override {
    refreshFaultReadout(*_plugin, _readout);
    
    if (!isHidden() && _plugin && true) setNeedsDisplay(true);

}

void drawRow_value_norm_panelX_panelWidth_y_attrs_small(drawing::String  name, drawing::String  value, double norm, double panelX, double panelWidth, double y, drawing::Attrs  attrs, drawing::Attrs  small)  {
    drawing::Style style;
    drawing::drawProcessorSlider(
        name, value, norm, y, panelX, panelWidth, attrs, small, style);

}

void drawMenuControl_value_panelX_panelWidth_y_attrs_small_style(drawing::String  name, drawing::String  value, double panelX, double panelWidth, double y, drawing::Attrs  attrs, drawing::Attrs  small, const drawing::Style& style)  {
    drawing::drawProcessorMenu(
        name, value, y, panelX, panelWidth, attrs, small, style);

}

void drawPerformanceMode_attrs_small_style(FaultReadout* plugin, drawing::Attrs  attrs, drawing::Attrs  small, const drawing::Style& style)  {
    drawing::drawAtPoint_withAttributes("MODE", drawing::makePoint(
        s3g::gui_layout::processorLabelX(kLeftToolboxX), kPerformanceRowY - 5.0), attrs);
    const drawing::Rect freeRect = drawing::makeRect(140.0, kPerformanceRowY - 10.0, 74.0, 22.0);
    const drawing::Rect midiRect = drawing::makeRect(214.0, kPerformanceRowY - 10.0, 74.0, 22.0);
    const bool midi = plugin->performanceMode == PerformanceMode::Midi;
    for (uint32_t item = 0u; item < 2u; ++item) {
        const drawing::Rect rect = item == 0u ? freeRect : midiRect;
        const bool active = item == (midi ? 1u : 0u);
        VSTGUI::CColor  fill = active
            ? drawing::calibratedRGB(0.13, 0.31, 0.24, 1.0)
            : drawing::calibratedWhite(0.12, 1.0);
        drawing::setFill(fill);
        drawing::rectFill(rect);
        drawing::setStroke((active ? style.text : style.grid));
        drawing::frameRect(rect);
        drawing::String  label = item == 0u ? "FREE" : "MIDI";
        const drawing::Size size = drawing::sizeWithAttributes(label, small);
        drawing::drawAtPoint_withAttributes(label, drawing::makePoint(drawing::midX(rect) - size.width * 0.5, drawing::midY(rect) - size.height * 0.5), small);
    }
    char receiveText[16] {};
    s3g::drum_midi::valueToText(
        plugin->midiReceive, receiveText, sizeof(receiveText));
    drawing::drawProcessorMenu("MIDI RECEIVE",
        drawing::stringFromUtf8(receiveText), kPerformanceRowY,
        kMidiReceivePanelX, kMidiReceivePanelWidth,
        attrs, small, style);
    const std::string note = midi ? midiNoteName(plugin->displayNote.load(std::memory_order_relaxed)) : "CONTINUOUS";
    drawing::drawAtPoint_withAttributes(drawing::stringFormat("NOTE %s   ·   ENV %.0f%%   ·   FIELD %08X",
        note.c_str(),
        plugin->displayEnvelope.load(std::memory_order_relaxed) * 100.0f,
        plugin->params.seed), drawing::makePoint(
            s3g::gui_layout::processorLabelX(kLeftToolboxX),
            kPerformanceRowY + 25.0), small);

}

void drawEnvelopeEditor_attrs_style(FaultReadout* plugin, drawing::Attrs  attrs, const drawing::Style& style)  {
    const EnvelopeGraphGeometry graph = envelopeGraphGeometry(*plugin);
    drawing::setFill(style.strip);
    drawing::rectFill(graph.frame);
    drawing::setStroke(style.grid);
    drawing::frameRect(graph.frame);

    drawing::Path  grid = drawing::Path::bezierPath();
    for (uint32_t division = 1u; division < 4u; ++division) {
        const double y = graph.top
            + (graph.bottom - graph.top) * static_cast<double>(division) * 0.25;
        grid.moveToPoint(drawing::makePoint(graph.frame.origin.x + 1.0, y));
        grid.lineToPoint(drawing::makePoint(drawing::maxX(graph.frame) - 1.0, y));
    }
    grid.setLineWidth(0.5);
    grid.stroke();

    drawing::Path  area = drawing::Path::bezierPath();
    area.moveToPoint(graph.start);
    area.lineToPoint(graph.attack);
    area.lineToPoint(graph.decay);
    area.lineToPoint(graph.sustain);
    area.lineToPoint(graph.release);
    area.closePath();
    drawing::setFill(drawing::withAlpha(style.fill, 0.18));
    area.fill();

    drawing::Path  envelope = drawing::Path::bezierPath();
    envelope.moveToPoint(graph.start);
    envelope.lineToPoint(graph.attack);
    envelope.lineToPoint(graph.decay);
    envelope.lineToPoint(graph.sustain);
    envelope.lineToPoint(graph.release);
    drawing::setStroke(style.text);
    envelope.setLineWidth(1.5);
    envelope.stroke();

    const bool midi = plugin->performanceMode == PerformanceMode::Midi;
    const float level = std::clamp(plugin->displayEnvelope.load(std::memory_order_relaxed), 0.0f, 1.0f);
    if (midi && level > 0.0001f) {
        const double y = graph.bottom - static_cast<double>(level) * (graph.bottom - graph.top);
        double dash[] = { 3.0, 4.0 };
        drawing::Path  levelLine = drawing::Path::bezierPath();
        levelLine.moveToPoint(drawing::makePoint(graph.frame.origin.x + 1.0, y));
        levelLine.lineToPoint(drawing::makePoint(drawing::maxX(graph.frame) - 1.0, y));
        levelLine.setLineDash_count_phase(dash, 2, 0.0);
        drawing::setStroke(drawing::calibratedRGB(0.34, 0.78, 0.55, 0.72));
        levelLine.setLineWidth(1.0);
        levelLine.stroke();
    }

    const auto displayedStage = static_cast<EnvelopeStage>(
        plugin->displayEnvelopeStage.load(std::memory_order_relaxed));
    auto drawHandle = [&](drawing::Point point, int dragId, EnvelopeStage stage) {
        const bool active = _dragSlider == dragId || (midi && displayedStage == stage);
        drawing::Path  handle = drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(point.x - 5.0, point.y - 5.0, 10.0, 10.0));
        VSTGUI::CColor  fill = active
            ? drawing::calibratedRGB(0.19, 0.55, 0.36, 1.0)
            : style.cellBg;
        drawing::setFill(fill);
        handle.fill();
        drawing::setStroke((active ? style.text : style.dim));
        handle.setLineWidth(1.0);
        handle.stroke();
    };
    drawHandle(graph.attack, 201, EnvelopeStage::Attack);
    drawHandle(graph.decay, 202, EnvelopeStage::Decay);
    drawHandle(graph.sustain, 203, EnvelopeStage::Sustain);
    drawHandle(graph.release, 204, EnvelopeStage::Release);

    const std::string attackText = compactEnvelopeTimeText(plugin->attackMs);
    const std::string decayText = compactEnvelopeTimeText(plugin->decayMs);
    const std::string releaseText = compactEnvelopeTimeText(plugin->releaseMs);
    drawing::drawAtPoint_withAttributes(drawing::stringFormat("A  %s", attackText.c_str()), drawing::makePoint(kEnvelopeX + 14.0, 678.0), attrs);
    drawing::drawAtPoint_withAttributes(drawing::stringFormat("D  %s", decayText.c_str()), drawing::makePoint(kEnvelopeX + 200.0, 678.0), attrs);
    drawing::drawAtPoint_withAttributes(drawing::stringFormat("S  %.0f%%", plugin->sustain * 100.0f), drawing::makePoint(kEnvelopeX + 390.0, 678.0), attrs);
    drawing::drawAtPoint_withAttributes(drawing::stringFormat("R  %s", releaseText.c_str()), drawing::makePoint(kEnvelopeX + 575.0, 678.0), attrs);

}

void drawButton_rect_attrs(drawing::String  label, drawing::Rect rect, drawing::Attrs  attrs)  {
    drawing::setFill(drawing::calibratedWhite(0.18, 1.0));
    drawing::rectFill(rect);
    drawing::setStroke(drawing::calibratedWhite(0.72, 1.0));
    drawing::frameRect(rect);
    const drawing::Size size = drawing::sizeWithAttributes(label, attrs);
    drawing::drawAtPoint_withAttributes(label, drawing::makePoint(drawing::midX(rect) - size.width * 0.5, drawing::midY(rect) - size.height * 0.5), attrs);

}

void drawTransportButton_rect_attrs_active(drawing::String  label, drawing::Rect rect, drawing::Attrs  attrs, bool active)  {
    VSTGUI::CColor  fill = drawing::calibratedWhite(0.18, 1.0);
    if (active) {
        fill = (label == "STOP")
            ? drawing::calibratedRGB(0.42, 0.17, 0.15, 1.0)
            : drawing::calibratedRGB(0.12, 0.34, 0.25, 1.0);
    }
    drawing::setFill(fill);
    drawing::rectFill(rect);
    drawing::setStroke(drawing::calibratedWhite((active ? 0.90 : 0.58), 1.0));
    drawing::frameRect(rect);
    const drawing::Size size = drawing::sizeWithAttributes(label, attrs);
    drawing::drawAtPoint_withAttributes(label, drawing::makePoint(drawing::midX(rect) - size.width * 0.5, drawing::midY(rect) - size.height * 0.5), attrs);

}

void drawAlgorithmChart_rect_attrs_style(FaultReadout* plugin, drawing::Rect rect, drawing::Attrs  attrs, const drawing::Style& style)  {
    drawing::setFill(style.strip);
    drawing::rectFill(rect);
    drawing::setStroke(style.grid);
    drawing::frameRect(rect);

    auto node = [&](double cx, double cy, drawing::String  label, bool output = false) {
        const drawing::Rect box = drawing::makeRect(cx - 29.0, cy - 13.0, 58.0, 26.0);
        VSTGUI::CColor  fill = output
            ? drawing::calibratedRGB(0.12, 0.34, 0.25, 1.0)
            : drawing::calibratedRGB(0.12, 0.22, 0.29, 1.0);
        drawing::setFill(fill);
        drawing::rectFill(box);
        drawing::setStroke(style.dim);
        drawing::frameRect(box);
        const drawing::Size size = drawing::sizeWithAttributes(label, attrs);
        drawing::drawAtPoint_withAttributes(label, drawing::makePoint(drawing::midX(box) - size.width * 0.5,
            drawing::midY(box) - size.height * 0.5), attrs);
    };
    auto arrow = [&](drawing::Point from, drawing::Point to) {
        const double angle = std::atan2(to.y - from.y, to.x - from.x);
        const double trim = 31.0;
        from.x += std::cos(angle) * trim;
        from.y += std::sin(angle) * trim;
        to.x -= std::cos(angle) * trim;
        to.y -= std::sin(angle) * trim;
        drawing::Path  path = drawing::Path::bezierPath();
        path.moveToPoint(from);
        path.lineToPoint(to);
        drawing::setStroke(style.text);
        path.setLineWidth(1.2);
        path.stroke();
        drawing::Path  head = drawing::Path::bezierPath();
        head.moveToPoint(to);
        head.lineToPoint(drawing::makePoint(to.x - std::cos(angle - 0.55) * 7.0,
            to.y - std::sin(angle - 0.55) * 7.0));
        head.lineToPoint(drawing::makePoint(to.x - std::cos(angle + 0.55) * 7.0,
            to.y - std::sin(angle + 0.55) * 7.0));
        head.closePath();
        drawing::setFill(style.text);
        head.fill();
    };
    auto operatorLabel = [&](uint32_t index, s3g::PsdRawFieldModTarget target) {
        return drawing::stringFormat("M%u:%s", index, modTargetShortName(target));
    };

    const double left = rect.origin.x;
    const double top = rect.origin.y;
    const double width = rect.size.width;
    const double height = rect.size.height;
    auto point = [&](double nx, double ny) {
        return drawing::makePoint(left + nx * width, top + ny * height);
    };
    const auto algorithm = plugin->params.modAlgorithm;
    drawing::String  m1 = operatorLabel(1u, plugin->params.modTarget);
    drawing::String  m2 = operatorLabel(2u, plugin->params.modTarget2);
    drawing::String  m3 = operatorLabel(3u, plugin->params.modTarget3);

    if (algorithm == s3g::PsdRawFieldModAlgorithm::Relay
        || algorithm == s3g::PsdRawFieldModAlgorithm::Transcode
        || algorithm == s3g::PsdRawFieldModAlgorithm::Regenerator) {
        const bool transcode = algorithm == s3g::PsdRawFieldModAlgorithm::Transcode;
        const bool regenerate = algorithm == s3g::PsdRawFieldModAlgorithm::Regenerator;
        const drawing::Point a = point(0.10, 0.54);
        const drawing::Point b = point(0.31, 0.54);
        const drawing::Point c = point(0.52, 0.54);
        const drawing::Point d = point(0.73, 0.54);
        const drawing::Point e = point(0.91, 0.54);
        node(a.x, a.y, transcode ? "DATA" : (regenerate ? "CODEC" : m3), regenerate || transcode);
        node(b.x, b.y, transcode ? m1 : (regenerate ? m3 : m2));
        node(c.x, c.y, transcode ? m2 : (regenerate ? m2 : m1));
        node(d.x, d.y, transcode ? m3 : (regenerate ? m1 : "DESTS"), !transcode);
        node(e.x, e.y, "CODEC", true);
        arrow(a, b); arrow(b, c); arrow(c, d); arrow(d, e);
        if (regenerate) {
            drawing::drawAtPoint_withAttributes(drawing::stringFromUtf8("↺"), drawing::makePoint(
                drawing::midX(rect) - 5.0, drawing::maxY(rect) - 27.0), attrs);
        }
    } else if (algorithm == s3g::PsdRawFieldModAlgorithm::CrossedMachines) {
        const drawing::Point a = point(0.24, 0.28);
        const drawing::Point b = point(0.24, 0.76);
        const drawing::Point c = point(0.56, 0.52);
        const drawing::Point out = point(0.86, 0.52);
        node(a.x, a.y, m1); node(b.x, b.y, m2); node(c.x, c.y, m3);
        node(out.x, out.y, "DESTS", true);
        arrow(a, b); arrow(b, c); arrow(c, a); arrow(c, out);
    } else {
        const bool multiplex = algorithm == s3g::PsdRawFieldModAlgorithm::Multiplex;
        const drawing::Point a = point(0.18, 0.25);
        const drawing::Point b = point(0.18, 0.52);
        const drawing::Point c = point(0.18, 0.79);
        const drawing::Point bus = point(0.58, 0.52);
        const drawing::Point out = point(0.86, 0.52);
        node(a.x, a.y, m1); node(b.x, b.y, m2); node(c.x, c.y, m3);
        node(bus.x, bus.y, multiplex ? "MUX" : "BUS");
        node(out.x, out.y, "DESTS", true);
        arrow(a, bus); arrow(b, bus); arrow(c, bus); arrow(bus, out);
    }
    if (plugin->params.modulationEnabled == 0u) {
        drawing::setFill(drawing::calibratedWhite(0.04, 0.68));
        drawing::rectFill(rect);
        drawing::String  bypassed = "MODULATION BYPASSED";
        const drawing::Size size = drawing::sizeWithAttributes(bypassed, attrs);
        drawing::drawAtPoint_withAttributes(bypassed, drawing::makePoint(drawing::midX(rect) - size.width * 0.5,
            drawing::midY(rect) - size.height * 0.5), attrs);
    }

}

void drawWaveforms_style(FaultReadout* plugin, drawing::Style& style)  {
    const double x = 18.0;
    const double y = 42.0;
    const double w = kGuiWidth - 36.0;
    const double h = 170.0;
    drawing::drawPanelFrame(x, y, w, h, style);
    drawing::Attrs  labelAttrs = drawing::softLabelAttrs();
    drawing::Attrs  small = drawing::softValueAttrs();
    drawing::drawAtPoint_withAttributes("8 CHANNEL OUTPUT", drawing::makePoint(
        x + s3g::gui_layout::kStandardMetrics.headerLabelInset, y + 7.0), labelAttrs);
    const std::string sourceStatus = sourceStatusText(*plugin);
    drawing::drawMiddleTruncated(sourceStatus, drawing::makeRect(x + 148.0, y + 7.0, 820.0, 18.0), small);
    const bool playing = plugin->playing.load(std::memory_order_relaxed);
    drawTransportButton_rect_attrs_active("PLAY", drawing::makeRect(1000.0, y + 7.0, 46.0, 18.0), small, playing);
    drawTransportButton_rect_attrs_active("STOP", drawing::makeRect(1054.0, y + 7.0, 48.0, 18.0), small, !playing);
    drawButton_rect_attrs("OPEN ANY", drawing::makeRect(1110.0, y + 7.0, 104.0, 18.0), small);
    drawButton_rect_attrs("GEN FIELD", drawing::makeRect(1224.0, y + 7.0, 108.0, 18.0), small);

    const double originX = x + 12.0;
    const double originY = y + 30.0;
    const double traceW = w - 24.0;
    const double traceH = (h - 44.0) / static_cast<double>(kOutputChannels);
    const uint32_t write = plugin->waveWrite.load(std::memory_order_relaxed);
    for (uint32_t ch = 0; ch < kOutputChannels; ++ch) {
        const double top = originY + static_cast<double>(ch) * traceH;
        const double mid = top + traceH * 0.5;
        drawing::setFill(drawing::calibratedWhite(0.13, 1.0));
        drawing::rectFill(drawing::makeRect(originX, top + 1.0, traceW, traceH - 2.0));
        drawing::setStroke(drawing::calibratedWhite(0.25, 1.0));
        drawing::Path  center = drawing::Path::bezierPath();
        center.moveToPoint(drawing::makePoint(originX, mid));
        center.lineToPoint(drawing::makePoint(originX + traceW, mid));
        center.setLineWidth(0.5);
        center.stroke();

        const double hue = 0.53 + static_cast<double>(ch) * 0.045;
        drawing::setStroke(drawing::calibratedHSV(hue, 0.55, 0.92, 0.96));
        drawing::Path  path = drawing::Path::bezierPath();
        float peak = 0.0001f;
        for (uint32_t i = 0; i < kWaveHistory; ++i) {
            peak = std::max(peak, std::abs(plugin->waveHistory[ch][(write + i) & (kWaveHistory - 1u)]));
        }
        const float displayGain = std::min(24.0f, 0.92f / peak);
        for (uint32_t i = 0; i < kWaveHistory; ++i) {
            const uint32_t index = (write + i) & (kWaveHistory - 1u);
            const float sample = std::clamp(plugin->waveHistory[ch][index] * displayGain, -1.0f, 1.0f);
            const double px = originX + static_cast<double>(i) * traceW / static_cast<double>(kWaveHistory - 1u);
            const double py = mid - static_cast<double>(sample) * traceH * 0.43;
            if (i == 0u) path.moveToPoint(drawing::makePoint(px, py));
            else path.lineToPoint(drawing::makePoint(px, py));
        }
        path.setLineWidth(1.0);
        path.stroke();
        drawing::drawAtPoint_withAttributes(drawing::stringFormat("%u", ch + 1u), drawing::makePoint(originX + 4.0, top + 2.0), small);
    }

}

void drawOpenMenu_style(drawing::Attrs  attrs, const drawing::Style& style)  {
    auto* p = &_readout;
    if (_openMenu == 1) {
        drawing::String  const items[] = {
            "INIT", "SUB CLOCK", "SCAR DRUM", "FAX BODY", "GATED BREAKS", "SYNC METAL",
            "BAUDOT DRUM", "DELTA KNOCK", "ADPCM SUB", "MORSE BODY", "SPARK IMPACT",
            "WIDE FAX BASS", "WAVE TRACE", "CUSTOM"
        };
        drawing::drawDropdownMenu(drawing::makeRect(kLeftControlX, 315.0, kToolboxMenuWidth, 18.0 * 14.0), 18.0, items, 14,
            static_cast<int>(std::min(p->selectedPreset, kCustomPreset)), _hoverMenuItem, attrs, style);
    } else if (_openMenu == 2) {
        drawing::String  const items[] = { "PARALLEL", "DEINTERLEAVE", "PLANES", "SHUFFLED", "DIVERGENT" };
        drawing::drawDropdownMenu(drawing::makeRect(kLeftControlX, 489.0, kToolboxMenuWidth, 18.0 * 5.0), 18.0, items, 5,
            static_cast<int>(p->params.channelScheme), _hoverMenuItem, attrs, style);
    } else if (_openMenu == 3) {
        drawing::String  const items[] = {
            "PCM", "DELTA", "ADPCM", "MU-LAW", "A-LAW", "CELP", "DISC", "CVSD",
            "SUBBAND", "LPC", "TRANSFORM", "PREDICT", "MODEM", "FAX", "SIGMA 1-BIT", "HYBRID", "APT",
            "HF FAX", "HELL", "MORSE", "SPARK CW", "BAUDOT RTTY", "SSTV"
        };
        drawing::drawDropdownMenu(drawing::makeRect(kRightControlX, 281.0, kToolboxMenuWidth, 18.0 * kCodecModeCount), 18.0,
            items, kCodecModeCount,
            static_cast<int>(p->params.codecMode), _hoverMenuItem, attrs, style);
    } else if (_openMenu == 4) {
        drawing::String  const items[] = {
            "BROADCAST", "RELAY", "MULTIPLEX", "CROSSED MACHINES", "REGENERATOR", "TRANSCODE"
        };
        const double panelX = _editorPage == 0 ? kModToolboxX : kLabChartX;
        const double panelWidth = _editorPage == 0 ? kModToolboxWidth : kLabChartWidth;
        drawing::drawDropdownMenu(drawing::makeRect(
            s3g::gui_layout::processorControlX(panelX), 281.0,
            s3g::gui_layout::processorMenuWidth(panelWidth),
            18.0 * s3g::kPsdRawFieldModAlgorithmCount), 18.0, items, s3g::kPsdRawFieldModAlgorithmCount,
            static_cast<int>(p->params.modAlgorithm), _hoverMenuItem, attrs, style);
    } else if (_openMenu == 5 || _openMenu == 7 || _openMenu == 9) {
        drawing::String  const items[] = {
            "OFF", "SINE", "TRIANGLE", "NOISE", "FIELD", "SYNC", "GATE", "APT",
            "HF FAX", "HELL", "MORSE", "SPARK CW", "BAUDOT RTTY", "SSTV", "FEEDBACK"
        };
        const uint32_t index = static_cast<uint32_t>((_openMenu - 5) / 2);
        const double cardX = labCardX(index);
        const int selected = _openMenu == 5 ? static_cast<int>(p->params.modSource)
            : (_openMenu == 7 ? static_cast<int>(p->params.modSource2)
                              : static_cast<int>(p->params.modSource3));
        drawing::drawDropdownMenu(drawing::makeRect(
            s3g::gui_layout::processorControlX(cardX), kLabSourceRowY + 14.0,
            s3g::gui_layout::processorMenuWidth(kLabCardWidth),
            18.0 * s3g::kPsdRawFieldModSourceCount), 18.0, items, s3g::kPsdRawFieldModSourceCount,
            selected, _hoverMenuItem, attrs, style);
    } else if (_openMenu == 6 || _openMenu == 8 || _openMenu == 10) {
        drawing::String  const items[] = {
            "CARRIER", "DEVIATION", "CLOCK", "DATA", "DAMAGE", "OFF",
            "BODY", "RING", "STRIKE", "FOLD", "SCAN"
        };
        const uint32_t index = static_cast<uint32_t>((_openMenu - 6) / 2);
        const double cardX = labCardX(index);
        const int selected = _openMenu == 6 ? static_cast<int>(p->params.modTarget)
            : (_openMenu == 8 ? static_cast<int>(p->params.modTarget2)
                              : static_cast<int>(p->params.modTarget3));
        drawing::drawDropdownMenu(drawing::makeRect(
            s3g::gui_layout::processorControlX(cardX), kLabTargetRowY + 14.0,
            s3g::gui_layout::processorMenuWidth(kLabCardWidth),
            18.0 * s3g::kPsdRawFieldModTargetCount), 18.0, items, s3g::kPsdRawFieldModTargetCount,
            selected, _hoverMenuItem, attrs, style);
    } else if (_openMenu >= 11 && _openMenu <= 13) {
        drawing::String  const items[] = { "FIXED", "ADSR" };
        const uint32_t index = static_cast<uint32_t>(_openMenu - 11);
        const uint32_t selected[] = {
            p->params.modEnvelope1, p->params.modEnvelope2, p->params.modEnvelope3
        };
        const double cardX = labCardX(index);
        drawing::drawDropdownMenu(drawing::makeRect(
            s3g::gui_layout::processorControlX(cardX), kLabEnvelopeRowY + 14.0,
            s3g::gui_layout::processorMenuWidth(kLabCardWidth), 36.0),
            18.0, items, 2u, static_cast<int>(selected[index]), _hoverMenuItem, attrs, style);
    } else if (_openMenu == 14) {
        drawing::String  const items[] = { "DIRECT", "DEMOD", "DIVIDE", "ERROR" };
        drawing::drawDropdownMenu(drawing::makeRect(
            s3g::gui_layout::processorControlX(kBassReceiverPanelX), 281.0,
            s3g::gui_layout::processorMenuWidth(kBassReceiverPanelWidth), 72.0),
            18.0, items, 4u, static_cast<int>(p->params.bassReceiver),
            _hoverMenuItem, attrs, style);
    } else if (_openMenu == 15) {
        drawing::String  const items[] = { "SCAN", "BODY", "BODY + SCAN" };
        drawing::drawDropdownMenu(drawing::makeRect(
            s3g::gui_layout::processorControlX(kBassReceiverPanelX), 307.0,
            s3g::gui_layout::processorMenuWidth(kBassReceiverPanelWidth), 54.0),
            18.0, items, 3u, static_cast<int>(p->params.bassPitchTracking),
            _hoverMenuItem, attrs, style);
    } else if (_openMenu == 16) {
        drawing::String  const items[] = { "-2 OCT", "-1 OCT", "0 OCT" };
        drawing::drawDropdownMenu(drawing::makeRect(
            s3g::gui_layout::processorControlX(kBassReceiverPanelX), 333.0,
            s3g::gui_layout::processorMenuWidth(kBassReceiverPanelWidth), 54.0),
            18.0, items, 3u, static_cast<int>(p->params.bassOctave),
            _hoverMenuItem, attrs, style);
    } else if (_openMenu == 17) {
        drawing::String  items[17] {};
        for (uint32_t index = 0u; index < 17u; ++index) {
            char text[16] {};
            s3g::drum_midi::valueToText(
                static_cast<double>(index), text, sizeof(text));
            items[index] = drawing::stringFromUtf8(text);
        }
        drawing::drawDropdownMenu(midiReceiveDropdownRect(), 18.0,
            items, 17u,
            s3g::drum_midi::receiveChannel(p->midiReceive),
            _hoverMenuItem, attrs, style);
    } else if (_openMenu == 18) {
        drawing::String  const items[] = {
            "8CH DIRECT", "QUAD RING", "STEREO RING"
        };
        drawing::drawDropdownMenu(outputFormatDropdownRect(), 18.0,
            items, 3u, static_cast<int>(p->outputFormat),
            _hoverMenuItem, attrs, style);
    }

}

void updateMenuHover(drawing::Point point)  {
    drawing::Rect rect = drawing::Rect{};
    uint32_t count = 0u;
    if (_openMenu == 1) { rect = drawing::makeRect(kLeftControlX, 315.0, kToolboxMenuWidth, 18.0 * 14.0); count = 14u; }
    else if (_openMenu == 2) { rect = drawing::makeRect(kLeftControlX, 489.0, kToolboxMenuWidth, 18.0 * 5.0); count = 5u; }
    else if (_openMenu == 3) {
        rect = drawing::makeRect(kRightControlX, 281.0, kToolboxMenuWidth, 18.0 * kCodecModeCount);
        count = kCodecModeCount;
    }
    else if (_openMenu == 4) {
        const double panelX = _editorPage == 0 ? kModToolboxX : kLabChartX;
        const double panelWidth = _editorPage == 0 ? kModToolboxWidth : kLabChartWidth;
        rect = drawing::makeRect(s3g::gui_layout::processorControlX(panelX), 281.0,
            s3g::gui_layout::processorMenuWidth(panelWidth),
            18.0 * s3g::kPsdRawFieldModAlgorithmCount);
        count = s3g::kPsdRawFieldModAlgorithmCount;
    }
    else if (_openMenu == 5 || _openMenu == 7 || _openMenu == 9) {
        const double cardX = labCardX(static_cast<uint32_t>((_openMenu - 5) / 2));
        rect = drawing::makeRect(s3g::gui_layout::processorControlX(cardX), kLabSourceRowY + 14.0,
            s3g::gui_layout::processorMenuWidth(kLabCardWidth),
            18.0 * s3g::kPsdRawFieldModSourceCount);
        count = s3g::kPsdRawFieldModSourceCount;
    }
    else if (_openMenu == 6 || _openMenu == 8 || _openMenu == 10) {
        const double cardX = labCardX(static_cast<uint32_t>((_openMenu - 6) / 2));
        rect = drawing::makeRect(s3g::gui_layout::processorControlX(cardX), kLabTargetRowY + 14.0,
            s3g::gui_layout::processorMenuWidth(kLabCardWidth),
            18.0 * s3g::kPsdRawFieldModTargetCount);
        count = s3g::kPsdRawFieldModTargetCount;
    }
    else if (_openMenu >= 11 && _openMenu <= 13) {
        const double cardX = labCardX(static_cast<uint32_t>(_openMenu - 11));
        rect = drawing::makeRect(s3g::gui_layout::processorControlX(cardX), kLabEnvelopeRowY + 14.0,
            s3g::gui_layout::processorMenuWidth(kLabCardWidth), 36.0);
        count = 2u;
    }
    else if (_openMenu == 14) {
        rect = drawing::makeRect(s3g::gui_layout::processorControlX(kBassReceiverPanelX),
            281.0, s3g::gui_layout::processorMenuWidth(kBassReceiverPanelWidth), 72.0);
        count = 4u;
    }
    else if (_openMenu == 15) {
        rect = drawing::makeRect(s3g::gui_layout::processorControlX(kBassReceiverPanelX),
            307.0, s3g::gui_layout::processorMenuWidth(kBassReceiverPanelWidth), 54.0);
        count = 3u;
    }
    else if (_openMenu == 16) {
        rect = drawing::makeRect(s3g::gui_layout::processorControlX(kBassReceiverPanelX),
            333.0, s3g::gui_layout::processorMenuWidth(kBassReceiverPanelWidth), 54.0);
        count = 3u;
    }
    else if (_openMenu == 17) {
        rect = midiReceiveDropdownRect();
        count = 17u;
    }
    else if (_openMenu == 18) {
        rect = outputFormatDropdownRect();
        count = 3u;
    }
    if (count == 0u) return;
    const int next = drawing::dropdownHitIndex(point, rect, 18.0, count);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        setNeedsDisplay(true);
    }

}

void drawOriginal(drawing::Rect dirty)  {
    (void)dirty;
    auto* p = &_readout;
    drawing::Style style;
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs  titleAttrs = drawing::softTitleAttrs();
    drawing::Attrs  labels = drawing::softLabelAttrs();
    drawing::Attrs  values = drawing::softValueAttrs();
    drawing::drawProcessorTitleBand(
        "s3g PROCESSOR FAULT 8CH",
        drawing::stringFromUtf8(_titlePresetName),
        drawing::peakDbText(
            p->outputPeak.load(std::memory_order_relaxed)),
        drawing::encoderTitleBand(kGuiWidth, kGuiHeight),
        titleAttrs, labels, values, style);
    drawWaveforms_style(p, style);

    if (_editorPage == 0) {
        drawing::drawPanelFrame(
            kLeftToolboxX, kToolboxTop, kToolboxWidth,
            s3g::gui_layout::toolboxHeightForRows(9), style);
        drawing::drawPanelHeader("FIELD / SPACE / OUTPUT", true,
            kLeftToolboxX, kToolboxTop, kToolboxWidth, 21, labels, style);
        drawing::drawPanelFrame(
            kRightToolboxX, kToolboxTop, kToolboxWidth,
            s3g::gui_layout::toolboxHeightForRows(9), style);
        drawing::drawPanelHeader("CODEC / SHAPE", true,
            kRightToolboxX, kToolboxTop, kToolboxWidth, 21, labels, style);
        drawing::drawPanelFrame(
            kModToolboxX, kToolboxTop, kModToolboxWidth,
            s3g::gui_layout::toolboxHeightForRows(9), style);
        drawing::drawPanelHeader("MODULATION / QUICK VIEW", true,
            kModToolboxX, kToolboxTop, kModToolboxWidth, 21, labels, style);
    } else if (_editorPage == 1) {
        drawing::drawPanelFrame(
            18.0, kToolboxTop, kGuiWidth - 36.0,
            s3g::gui_layout::toolboxHeightForRows(9), style);
        drawing::drawPanelHeader("MOD LAB / THREE OPERATORS", true,
            18.0, kToolboxTop, kGuiWidth - 36.0, 21, labels, style);
    } else {
        drawing::drawPanelFrame(
            18.0, kToolboxTop, kGuiWidth - 36.0,
            s3g::gui_layout::toolboxHeightForRows(9), style);
        drawing::drawPanelHeader("BASS LAB / CODEC-DRIVEN LOW CORE", true,
            18.0, kToolboxTop, kGuiWidth - 36.0, 21, labels, style);
    }
    auto drawPageTab = [&](drawing::String  title, drawing::Rect rect, bool active) {
        VSTGUI::CColor  fill = active
            ? drawing::calibratedRGB(0.13, 0.31, 0.24, 1.0)
            : drawing::calibratedWhite(0.12, 1.0);
        drawing::setFill(fill);
        drawing::rectFill(rect);
        drawing::setStroke((active ? style.text : style.grid));
        drawing::frameRect(rect);
        const drawing::Size size = drawing::sizeWithAttributes(title, values);
        drawing::drawAtPoint_withAttributes(title, drawing::makePoint(drawing::midX(rect) - size.width * 0.5,
            drawing::midY(rect) - size.height * 0.5), values);
    };
    drawPageTab("SOUND", drawing::makeRect(1066.0, 232.0, 76.0, 17.0), _editorPage == 0);
    drawPageTab("BASS LAB", drawing::makeRect(1148.0, 232.0, 88.0, 17.0), _editorPage == 2);
    drawPageTab("MOD LAB", drawing::makeRect(1242.0, 232.0, 88.0, 17.0), _editorPage == 1);
    const bool modulationEnabled = p->params.modulationEnabled != 0u;
    drawTransportButton_rect_attrs_active((modulationEnabled ? "MOD ON" : "MOD OFF"), drawing::makeRect(950.0, 232.0, 108.0, 17.0), values, modulationEnabled);
    drawing::drawPanelFrame(18, kPatchPanelY, kGuiWidth - 36.0, 188.0, style);
    drawing::drawPanelHeader("PATCH / PERFORMANCE", true,
        18, kPatchPanelY, kGuiWidth - 36.0, 21, labels, style);

    const auto& params = p->params;
    const std::string scanText = p->rawSource && p->rawSource->waveform
        ? waveformSpeedText(params.scanRate)
        : rateText(scanBytesPerSecond(params.scanRate, p->sampleRate), "B/s");
    const std::string codecRateText = rateText(codecUpdatesPerSecond(params.codecRate, p->sampleRate), "Hz");
    const std::string carrierText = carrierTuneText(params.carrierTune, static_cast<uint32_t>(params.codecMode));
    const std::string fieldEvolveText = evolveText(params.evolve);
    if (_editorPage == 0) {
    drawRow_value_norm_panelX_panelWidth_y_attrs_small("OUT", drawing::stringFormat("%+.1f dB", params.gainDb), normalizedParam(params, kGainParamId), kLeftToolboxX, kToolboxWidth, 266, labels, values);
    drawRow_value_norm_panelX_panelWidth_y_attrs_small("SCAN", drawing::stringFromUtf8(scanText.c_str()), params.scanRate, kLeftToolboxX, kToolboxWidth, 292, labels, values);
    drawRow_value_norm_panelX_panelWidth_y_attrs_small("TEXTURE", drawing::stringFormat("%.0f%%", params.texture * 100.0f), params.texture, kLeftToolboxX, kToolboxWidth, 318, labels, values);
    drawRow_value_norm_panelX_panelWidth_y_attrs_small("GEOMETRY", drawing::stringFormat("%.0f%%", params.geometry * 100.0f), params.geometry, kLeftToolboxX, kToolboxWidth, 344, labels, values);
    drawRow_value_norm_panelX_panelWidth_y_attrs_small("CHAOS", drawing::stringFormat("%.0f%%", params.chaos * 100.0f), params.chaos, kLeftToolboxX, kToolboxWidth, 370, labels, values);
    drawRow_value_norm_panelX_panelWidth_y_attrs_small("FOLD", drawing::stringFormat("%.0f%%", params.fold * 100.0f), params.fold, kLeftToolboxX, kToolboxWidth, 396, labels, values);
    drawRow_value_norm_panelX_panelWidth_y_attrs_small("EVOLVE", drawing::stringFromUtf8(fieldEvolveText.c_str()), params.evolve, kLeftToolboxX, kToolboxWidth, 422, labels, values);
    drawMenuControl_value_panelX_panelWidth_y_attrs_small_style("ROUTE", drawing::stringFromUtf8(channelSchemeName(static_cast<uint32_t>(params.channelScheme))), kLeftToolboxX, kToolboxWidth, 448, labels, values, style);
    drawRow_value_norm_panelX_panelWidth_y_attrs_small("SPREAD", drawing::stringFormat("%.0f%%", params.channelSpread * 100.0f), params.channelSpread, kLeftToolboxX, kToolboxWidth, 474, labels, values);

    drawMenuControl_value_panelX_panelWidth_y_attrs_small_style("CODEC", drawing::stringFromUtf8(codecModeName(static_cast<uint32_t>(params.codecMode))), kRightToolboxX, kToolboxWidth, 266, labels, values, style);
    drawRow_value_norm_panelX_panelWidth_y_attrs_small("RATE", drawing::stringFromUtf8(codecRateText.c_str()), params.codecRate, kRightToolboxX, kToolboxWidth, 292, labels, values);
    drawRow_value_norm_panelX_panelWidth_y_attrs_small("BITS", drawing::stringFormat("%.0f", params.bitDepth), normalizedParam(params, kBitDepthParamId), kRightToolboxX, kToolboxWidth, 318, labels, values);
    drawRow_value_norm_panelX_panelWidth_y_attrs_small("CARRIER", drawing::stringFromUtf8(carrierText.c_str()), normalizedParam(params, kCarrierTuneParamId), kRightToolboxX, kToolboxWidth, 344, labels, values);
    drawRow_value_norm_panelX_panelWidth_y_attrs_small("DAMAGE", drawing::stringFormat("%.0f%%", params.codecDamage * 100.0f), params.codecDamage, kRightToolboxX, kToolboxWidth, 370, labels, values);
    drawRow_value_norm_panelX_panelWidth_y_attrs_small("DRIVE", drawing::stringFormat("%.0f%%", params.drive * 100.0f), params.drive, kRightToolboxX, kToolboxWidth, 396, labels, values);
    drawRow_value_norm_panelX_panelWidth_y_attrs_small("SHRED", drawing::stringFormat("%.0f%%", params.shred * 100.0f), params.shred, kRightToolboxX, kToolboxWidth, 422, labels, values);
    drawRow_value_norm_panelX_panelWidth_y_attrs_small("RESONANCE", drawing::stringFormat("%.0f%%", params.resonance * 100.0f), params.resonance, kRightToolboxX, kToolboxWidth, 448, labels, values);
    drawRow_value_norm_panelX_panelWidth_y_attrs_small("BASS CORE", drawing::stringFormat("%.0f%%", params.bassBody * 100.0f), params.bassBody, kRightToolboxX, kToolboxWidth, 474, labels, values);

    drawMenuControl_value_panelX_panelWidth_y_attrs_small_style("CURATED CIRCUIT", drawing::stringFromUtf8(modAlgorithmName(static_cast<uint32_t>(params.modAlgorithm))), kModToolboxX, kModToolboxWidth, 266, labels, values, style);
    drawAlgorithmChart_rect_attrs_style(p, drawing::makeRect(kModToolboxX + 12.0, 292.0,
        kModToolboxWidth - 24.0, 124.0), values, style);
    auto drawSummary = [&](uint32_t number, s3g::PsdRawFieldModSource source,
                           s3g::PsdRawFieldModTarget target, uint32_t envelope, double y) {
        drawing::String  summary = drawing::stringFormat("M%u   %s  →  %s   ·   %s",
            number, modSourceName(static_cast<uint32_t>(source)),
            modTargetName(static_cast<uint32_t>(target)), modEnvelopeName(envelope));
        drawing::drawAtPoint_withAttributes(summary, drawing::makePoint(kModToolboxX + 18.0, y), values);
    };
    drawSummary(1u, params.modSource, params.modTarget, params.modEnvelope1, 428.0);
    drawSummary(2u, params.modSource2, params.modTarget2, params.modEnvelope2, 450.0);
    drawSummary(3u, params.modSource3, params.modTarget3, params.modEnvelope3, 472.0);
    drawing::drawAtPoint_withAttributes("MOD LAB: RATE · RATIO · INDEX · FEEDBACK · CLOCK · ENVELOPE", drawing::makePoint(kModToolboxX + 18.0, 492.0), labels);
    } else if (_editorPage == 1) {
        drawMenuControl_value_panelX_panelWidth_y_attrs_small_style("ALGORITHM", drawing::stringFromUtf8(modAlgorithmName(static_cast<uint32_t>(params.modAlgorithm))), kLabChartX, kLabChartWidth, 266, labels, values, style);
        drawAlgorithmChart_rect_attrs_style(p, drawing::makeRect(kLabChartX, 294.0, kLabChartWidth, 190.0), values, style);
        auto drawLabOperator = [&](uint32_t number,
                                   s3g::PsdRawFieldModSource source,
                                   s3g::PsdRawFieldModTarget target,
                                   float rate, float ratio, float index, float feedback,
                                   uint32_t clock, uint32_t envelope,
                                   clap_id ratioId, clap_id feedbackId) {
            const double cardX = labCardX(number - 1u);
            drawing::drawPanelFrame(cardX, 258.0, kLabCardWidth, 234.0, style);
            drawing::setFill(style.strip);
            drawing::rectFill(drawing::makeRect(cardX + 1.0, 259.0, kLabCardWidth - 2.0, 18.0));
            drawing::String  title = drawing::stringFormat("M%u OPERATOR", number);
            const drawing::Size titleSize = drawing::sizeWithAttributes(title, labels);
            drawing::drawAtPoint_withAttributes(title, drawing::makePoint(cardX + (kLabCardWidth - titleSize.width) * 0.5, 263.0), labels);
            drawMenuControl_value_panelX_panelWidth_y_attrs_small_style("SOURCE", drawing::stringFromUtf8(modSourceName(static_cast<uint32_t>(source))), cardX, kLabCardWidth, kLabSourceRowY, labels, values, style);
            drawMenuControl_value_panelX_panelWidth_y_attrs_small_style("DEST", drawing::stringFromUtf8(modTargetName(static_cast<uint32_t>(target))), cardX, kLabCardWidth, kLabTargetRowY, labels, values, style);
            const std::string rateValue = rateText(modulatorRateHz(rate), "Hz");
            drawRow_value_norm_panelX_panelWidth_y_attrs_small("RATE", drawing::stringFromUtf8(rateValue.c_str()), rate, cardX, kLabCardWidth, kLabRateRowY, labels, values);
            drawRow_value_norm_panelX_panelWidth_y_attrs_small("RATIO", drawing::stringFormat("%.2fx", ratio), normalizedParam(params, ratioId), cardX, kLabCardWidth, kLabRatioRowY, labels, values);
            drawRow_value_norm_panelX_panelWidth_y_attrs_small("INDEX", drawing::stringFormat("%.0f%%", index * 100.0f), index, cardX, kLabCardWidth, kLabIndexRowY, labels, values);
            drawRow_value_norm_panelX_panelWidth_y_attrs_small("FEEDBACK", drawing::stringFormat("%.0f%%", feedback * (100.0f / 0.98f)), normalizedParam(params, feedbackId), cardX, kLabCardWidth, kLabFeedbackRowY, labels, values);
            drawMenuControl_value_panelX_panelWidth_y_attrs_small_style("CLOCK", drawing::stringFromUtf8(modClockName(clock)), cardX, kLabCardWidth, kLabClockRowY, labels, values, style);
            drawMenuControl_value_panelX_panelWidth_y_attrs_small_style("ENVELOPE", drawing::stringFromUtf8(modEnvelopeName(envelope)), cardX, kLabCardWidth, kLabEnvelopeRowY, labels, values, style);
        };
        drawLabOperator(1u, params.modSource, params.modTarget, params.modRate, params.modRatio,
            params.modIndex, params.modFeedback, params.modClockLock, params.modEnvelope1,
            kModRatioParamId, kModFeedbackParamId);
        drawLabOperator(2u, params.modSource2, params.modTarget2, params.modRate2, params.modRatio2,
            params.modIndex2, params.modFeedback2, params.modClockLock2, params.modEnvelope2,
            kModRatio2ParamId, kModFeedback2ParamId);
        drawLabOperator(3u, params.modSource3, params.modTarget3, params.modRate3, params.modRatio3,
            params.modIndex3, params.modFeedback3, params.modClockLock3, params.modEnvelope3,
            kModRatio3ParamId, kModFeedback3ParamId);
    } else {
        drawing::drawPanelFrame(kBassReceiverPanelX, 258.0,
            kBassReceiverPanelWidth, 234.0, style);
        drawing::drawPanelHeader("RECEIVER / PITCH", true,
            kBassReceiverPanelX, 258.0, kBassReceiverPanelWidth, 21, labels, style);
        drawMenuControl_value_panelX_panelWidth_y_attrs_small_style("RECEIVER", drawing::stringFromUtf8(bassReceiverName(
            static_cast<uint32_t>(params.bassReceiver))), kBassReceiverPanelX, kBassReceiverPanelWidth, 286, labels, values, style);
        drawMenuControl_value_panelX_panelWidth_y_attrs_small_style("KEY TRACK", drawing::stringFromUtf8(bassPitchTrackingName(
            static_cast<uint32_t>(params.bassPitchTracking))), kBassReceiverPanelX, kBassReceiverPanelWidth, 312, labels, values, style);
        drawMenuControl_value_panelX_panelWidth_y_attrs_small_style("OCTAVE", drawing::stringFromUtf8(bassOctaveName(
            static_cast<uint32_t>(params.bassOctave))), kBassReceiverPanelX, kBassReceiverPanelWidth, 338, labels, values, style);
        drawing::drawAtPoint_withAttributes("DIRECT   codec waveform clocks the divider", drawing::makePoint(kBassReceiverPanelX + 18.0, 378.0), values);
        drawing::drawAtPoint_withAttributes("DEMOD    envelope weights divided crossings", drawing::makePoint(kBassReceiverPanelX + 18.0, 400.0), values);
        drawing::drawAtPoint_withAttributes("DIVIDE   carrier crossings generate subharmonics", drawing::makePoint(kBassReceiverPanelX + 18.0, 422.0), values);
        drawing::drawAtPoint_withAttributes("ERROR    damage gates divided codec crossings", drawing::makePoint(kBassReceiverPanelX + 18.0, 444.0), values);

        drawing::drawPanelFrame(kBassControlPanelX, 258.0,
            kBassControlPanelWidth, 234.0, style);
        drawing::drawPanelHeader("SUBHARMONIC / HIGH GAIN", true,
            kBassControlPanelX, 258.0, kBassControlPanelWidth, 21, labels, style);
        drawRow_value_norm_panelX_panelWidth_y_attrs_small("BODY", drawing::stringFormat("%.0f%%", params.bassBody * 100.0f), params.bassBody, kBassControlPanelX, kBassControlPanelWidth, 286, labels, values);
        drawRow_value_norm_panelX_panelWidth_y_attrs_small("EXCITE", drawing::stringFormat("%.0f%%", params.bassPunch * 100.0f), params.bassPunch, kBassControlPanelX, kBassControlPanelWidth, 312, labels, values);
        drawRow_value_norm_panelX_panelWidth_y_attrs_small("TRACE", drawing::stringFormat("%.0f%%", params.bassTrace * 100.0f), params.bassTrace, kBassControlPanelX, kBassControlPanelWidth, 338, labels, values);
        drawRow_value_norm_panelX_panelWidth_y_attrs_small("FUZZ", drawing::stringFormat("%.0f%%", params.bassFuzz * 100.0f), params.bassFuzz, kBassControlPanelX, kBassControlPanelWidth, 364, labels, values);
        drawRow_value_norm_panelX_panelWidth_y_attrs_small("METAL", drawing::stringFormat("%.0f%%", params.bassMetal * 100.0f), params.bassMetal, kBassControlPanelX, kBassControlPanelWidth, 390, labels, values);
        drawRow_value_norm_panelX_panelWidth_y_attrs_small("FEEDBACK", drawing::stringFormat("%.0f%%", params.bassFeedback * 100.0f), params.bassFeedback, kBassControlPanelX, kBassControlPanelWidth, 416, labels, values);
        drawRow_value_norm_panelX_panelWidth_y_attrs_small("GLIDE", drawing::stringFormat("%.0f%%", params.bassGlide * 100.0f), params.bassGlide, kBassControlPanelX, kBassControlPanelWidth, 442, labels, values);
        drawRow_value_norm_panelX_panelWidth_y_attrs_small("LOW WIDTH", drawing::stringFormat("%.0f%%", params.bassLowWidth * 100.0f), params.bassLowWidth, kBassControlPanelX, kBassControlPanelWidth, 468, labels, values);

        drawing::drawPanelFrame(kBassPathPanelX, 258.0,
            kBassPathPanelWidth, 234.0, style);
        drawing::drawPanelHeader("RECONSTRUCTION PATH", true,
            kBassPathPanelX, 258.0, kBassPathPanelWidth, 21, labels, style);
        drawing::drawAtPoint_withAttributes("CODEC / MODULATION  →  RECEIVER  →  DIVIDER", drawing::makePoint(kBassPathPanelX + 24.0, 300.0), values);
        drawing::drawAtPoint_withAttributes("MIDI ROOT  →  GLIDE / OCTAVE  →  MODAL FILTER", drawing::makePoint(kBassPathPanelX + 24.0, 326.0), values);
        drawing::drawAtPoint_withAttributes("LOW ANCHOR  ───────────────────────────┐", drawing::makePoint(kBassPathPanelX + 24.0, 352.0), values);
        drawing::drawAtPoint_withAttributes("UPPER BODY  →  FUZZ  →  METAL  →  FEEDBACK", drawing::makePoint(kBassPathPanelX + 24.0, 378.0), values);
        drawing::drawAtPoint_withAttributes("                                  ↓", drawing::makePoint(kBassPathPanelX + 24.0, 404.0), values);
        drawing::drawAtPoint_withAttributes("TRACE MIX  →  LOW WIDTH  →  8CH OUTPUT", drawing::makePoint(kBassPathPanelX + 24.0, 430.0), values);
        drawing::drawAtPoint_withAttributes("The clean low anchor remains outside the feedback loop.", drawing::makePoint(kBassPathPanelX + 24.0, 462.0), labels);
    }

    drawMenuControl_value_panelX_panelWidth_y_attrs_small_style("PRESET", drawing::stringFromUtf8(presetName(p->selectedPreset)), kLeftToolboxX, kToolboxWidth, kPresetRowY, labels, values, style);
    drawButton_rect_attrs("CURATED RANDOM", drawing::makeRect(466, kPresetRowY - 7.0, 126, 24), values);
    drawButton_rect_attrs("MUTATE", drawing::makeRect(604, kPresetRowY - 7.0, 90, 24), values);
    drawButton_rect_attrs("UNDO", drawing::makeRect(706, kPresetRowY - 7.0, 90, 24), values);
    drawPerformanceMode_attrs_small_style(p, labels, values, style);
    drawEnvelopeEditor_attrs_style(p, values, style);
    drawMenuControl_value_panelX_panelWidth_y_attrs_small_style("FORMAT", drawing::stringFromUtf8(outputFormatName(p->outputFormat)), kOutputPanelX, kOutputPanelWidth, kOutputFormatRowY, labels, values, style);
    drawRow_value_norm_panelX_panelWidth_y_attrs_small("ROTATE", drawing::stringFormat("%+.1f deg", p->outputRotationDeg), normalizedPerformanceParam(*p, kOutputRotationParamId), kOutputPanelX, kOutputPanelWidth, kOutputRotationRowY, labels, values);
    drawOpenMenu_style(values, style);

}

void updateSlider(drawing::Point point)  {
    auto* p = &_readout;
    if (_dragSlider >= 1 && _dragSlider <= 8) {
        static const clap_id ids[] = {
            kScanRateParamId, kTextureParamId, kGeometryParamId, kChaosParamId,
            kFoldParamId, kEvolveParamId, kChannelSpreadParamId, kGainParamId
        };
        const double normalized = std::clamp(
            (point.x - kLeftControlX) / kToolboxTrackWidth, 0.0, 1.0);
        applyNormalizedParam(*p, ids[_dragSlider - 1], normalized);
    } else if (_dragSlider >= 101 && _dragSlider <= 108) {
        static const clap_id ids[] = {
            kCodecRateParamId, kBitDepthParamId, kCarrierTuneParamId, kCodecDamageParamId,
            kDriveParamId, kShredParamId, kResonanceParamId, kBassBodyParamId
        };
        const double normalized = std::clamp(
            (point.x - kRightControlX) / kToolboxTrackWidth, 0.0, 1.0);
        applyNormalizedParam(*p, ids[_dragSlider - 101], normalized);
    } else if (_dragSlider >= 301 && _dragSlider <= 304) {
        static const clap_id ids[] = {
            kModRateParamId, kModRatioParamId, kModIndexParamId, kModFeedbackParamId
        };
        const double normalized = std::clamp(
            (point.x - s3g::gui_layout::processorControlX(labCardX(0u)))
                / s3g::gui_layout::processorTrackWidth(kLabCardWidth), 0.0, 1.0);
        applyNormalizedParam(*p, ids[_dragSlider - 301], normalized);
    } else if (_dragSlider >= 401 && _dragSlider <= 404) {
        static const clap_id ids[] = {
            kModRate2ParamId, kModRatio2ParamId, kModIndex2ParamId, kModFeedback2ParamId
        };
        const double normalized = std::clamp(
            (point.x - s3g::gui_layout::processorControlX(labCardX(1u)))
                / s3g::gui_layout::processorTrackWidth(kLabCardWidth), 0.0, 1.0);
        applyNormalizedParam(*p, ids[_dragSlider - 401], normalized);
    } else if (_dragSlider >= 501 && _dragSlider <= 504) {
        static const clap_id ids[] = {
            kModRate3ParamId, kModRatio3ParamId, kModIndex3ParamId, kModFeedback3ParamId
        };
        const double normalized = std::clamp(
            (point.x - s3g::gui_layout::processorControlX(labCardX(2u)))
                / s3g::gui_layout::processorTrackWidth(kLabCardWidth), 0.0, 1.0);
        applyNormalizedParam(*p, ids[_dragSlider - 501], normalized);
    } else if (_dragSlider >= 601 && _dragSlider <= 608) {
        static const clap_id ids[] = {
            kBassBodyParamId, kBassPunchParamId, kBassTraceParamId,
            kBassFuzzParamId, kBassMetalParamId, kBassFeedbackParamId,
            kBassGlideParamId, kBassLowWidthParamId
        };
        const double normalized = std::clamp(
            (point.x - s3g::gui_layout::processorControlX(kBassControlPanelX))
                / s3g::gui_layout::processorTrackWidth(kBassControlPanelWidth), 0.0, 1.0);
        applyNormalizedParam(*p, ids[_dragSlider - 601], normalized);
    } else if (_dragSlider == 701) {
        const double normalized = std::clamp(
            (point.x - s3g::gui_layout::processorControlX(kOutputPanelX))
                / s3g::gui_layout::processorTrackWidth(kOutputPanelWidth),
            0.0, 1.0);
        applyNormalizedParam(*p, kOutputRotationParamId, normalized);
    } else if (_dragSlider >= 201 && _dragSlider <= 204) {
        const EnvelopeGraphGeometry graph = envelopeGraphGeometry(*p);
        const double sustain = std::clamp(
            static_cast<double>((graph.bottom - point.y) / (graph.bottom - graph.top)), 0.0, 1.0);
        if (_dragSlider == 201) {
            applyNormalizedParam(*p, kAttackParamId,
                std::clamp(static_cast<double>((point.x - (kEnvelopeX + 54.0)) / 126.0), 0.0, 1.0));
        } else if (_dragSlider == 202) {
            applyNormalizedParam(*p, kDecayParamId,
                std::clamp(static_cast<double>((point.x - (kEnvelopeX + 220.0)) / 140.0), 0.0, 1.0));
            applyNormalizedParam(*p, kSustainParamId, sustain);
        } else if (_dragSlider == 203) {
            applyNormalizedParam(*p, kSustainParamId, sustain);
        } else {
            applyNormalizedParam(*p, kReleaseParamId,
                std::clamp(static_cast<double>((point.x - (kEnvelopeX + 520.0)) / 160.0), 0.0, 1.0));
        }
        markHostStateDirty(*p);
    }
    setNeedsDisplay(true);

}

void mouseDown(drawing::Event*  event)  {
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    auto* p = &_readout;
    const auto titleBand = drawing::encoderTitleBand(kGuiWidth, kGuiHeight);
    if (handleProcessorTitleClick(
            point, &p->plugin, "Processor Fault", titleBand,
            _titlePresetName, sizeof(_titlePresetName), kGainParamId)) {
        setNeedsDisplay(true);
        return;
    }
    if (_openMenu != 0) {
        drawing::Rect rect = drawing::Rect{};
        uint32_t count = 0u;
        clap_id id = CLAP_INVALID_ID;
        if (_openMenu == 1) { rect = drawing::makeRect(kLeftControlX, 315.0, kToolboxMenuWidth, 18.0 * 14.0); count = 14u; id = kPresetParamId; }
        else if (_openMenu == 2) { rect = drawing::makeRect(kLeftControlX, 489.0, kToolboxMenuWidth, 18.0 * 5.0); count = 5u; id = kChannelSchemeParamId; }
        else if (_openMenu == 3) {
            rect = drawing::makeRect(kRightControlX, 281.0, kToolboxMenuWidth, 18.0 * kCodecModeCount);
            count = kCodecModeCount;
            id = kCodecModeParamId;
        }
        else if (_openMenu == 4) {
            const double panelX = _editorPage == 0 ? kModToolboxX : kLabChartX;
            const double panelWidth = _editorPage == 0 ? kModToolboxWidth : kLabChartWidth;
            rect = drawing::makeRect(s3g::gui_layout::processorControlX(panelX), 281.0,
                s3g::gui_layout::processorMenuWidth(panelWidth),
                18.0 * s3g::kPsdRawFieldModAlgorithmCount);
            count = s3g::kPsdRawFieldModAlgorithmCount;
            id = kModAlgorithmParamId;
        }
        else if (_openMenu == 5 || _openMenu == 7 || _openMenu == 9) {
            const double cardX = labCardX(static_cast<uint32_t>((_openMenu - 5) / 2));
            rect = drawing::makeRect(s3g::gui_layout::processorControlX(cardX), kLabSourceRowY + 14.0,
                s3g::gui_layout::processorMenuWidth(kLabCardWidth),
                18.0 * s3g::kPsdRawFieldModSourceCount);
            count = s3g::kPsdRawFieldModSourceCount;
            id = _openMenu == 5 ? kModSourceParamId
                : (_openMenu == 7 ? kModSource2ParamId : kModSource3ParamId);
        }
        else if (_openMenu == 6 || _openMenu == 8 || _openMenu == 10) {
            const double cardX = labCardX(static_cast<uint32_t>((_openMenu - 6) / 2));
            rect = drawing::makeRect(s3g::gui_layout::processorControlX(cardX), kLabTargetRowY + 14.0,
                s3g::gui_layout::processorMenuWidth(kLabCardWidth),
                18.0 * s3g::kPsdRawFieldModTargetCount);
            count = s3g::kPsdRawFieldModTargetCount;
            id = _openMenu == 6 ? kModTargetParamId
                : (_openMenu == 8 ? kModTarget2ParamId : kModTarget3ParamId);
        }
        else if (_openMenu >= 11 && _openMenu <= 13) {
            const uint32_t index = static_cast<uint32_t>(_openMenu - 11);
            const double cardX = labCardX(index);
            rect = drawing::makeRect(s3g::gui_layout::processorControlX(cardX), kLabEnvelopeRowY + 14.0,
                s3g::gui_layout::processorMenuWidth(kLabCardWidth), 36.0);
            count = 2u;
            constexpr clap_id ids[] = {
                kModEnvelope1ParamId, kModEnvelope2ParamId, kModEnvelope3ParamId
            };
            id = ids[index];
        }
        else if (_openMenu == 14) {
            rect = drawing::makeRect(s3g::gui_layout::processorControlX(kBassReceiverPanelX),
                281.0, s3g::gui_layout::processorMenuWidth(kBassReceiverPanelWidth), 72.0);
            count = 4u;
            id = kBassReceiverParamId;
        }
        else if (_openMenu == 15) {
            rect = drawing::makeRect(s3g::gui_layout::processorControlX(kBassReceiverPanelX),
                307.0, s3g::gui_layout::processorMenuWidth(kBassReceiverPanelWidth), 54.0);
            count = 3u;
            id = kBassPitchTrackingParamId;
        }
        else if (_openMenu == 16) {
            rect = drawing::makeRect(s3g::gui_layout::processorControlX(kBassReceiverPanelX),
                333.0, s3g::gui_layout::processorMenuWidth(kBassReceiverPanelWidth), 54.0);
            count = 3u;
            id = kBassOctaveParamId;
        }
        else if (_openMenu == 17) {
            rect = midiReceiveDropdownRect();
            count = 17u;
            id = kMidiReceiveParamId;
        }
        else if (_openMenu == 18) {
            rect = outputFormatDropdownRect();
            count = 3u;
            id = kOutputFormatParamId;
        }
        const int hit = drawing::dropdownHitIndex(point, rect, 18.0, count);
        if (hit >= 0 && id != CLAP_INVALID_ID) {
            if (id == kModAlgorithmParamId && _editorPage == 0)
                applyCuratedAlgorithm(*p, static_cast<uint32_t>(hit));
            else
                applyParam(*p, id, hit);
            markHostStateDirty(*p);
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point, drawing::makeRect(1066.0, 232.0, 76.0, 17.0))) {
        _editorPage = 0;
        _dragSlider = -1;
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point, drawing::makeRect(1148.0, 232.0, 88.0, 17.0))) {
        _editorPage = 2;
        _dragSlider = -1;
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point, drawing::makeRect(1242.0, 232.0, 88.0, 17.0))) {
        _editorPage = 1;
        _dragSlider = -1;
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point, drawing::makeRect(950.0, 232.0, 108.0, 17.0))) {
        applyParam(*p, kModulationEnabledParamId,
            p->params.modulationEnabled != 0u ? 0.0 : 1.0);
        markHostStateDirty(*p);
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point, drawing::makeRect(kLeftControlX, kPresetRowY - 1.0, kToolboxMenuWidth, 16))) { _openMenu = 1; setNeedsDisplay(true); return; }
    if (_editorPage == 0) {
        if (drawing::contains(point, drawing::makeRect(kLeftControlX, 447.0, kToolboxMenuWidth, 18))) { _openMenu = 2; setNeedsDisplay(true); return; }
        if (drawing::contains(point, drawing::makeRect(kRightControlX, 265.0, kToolboxMenuWidth, 16))) { _openMenu = 3; setNeedsDisplay(true); return; }
        if (drawing::contains(point, drawing::makeRect(kModControlX, 265.0, kModToolboxMenuWidth, 18))) { _openMenu = 4; setNeedsDisplay(true); return; }
    } else if (_editorPage == 1) {
        const double algorithmX = s3g::gui_layout::processorControlX(kLabChartX);
        const double algorithmWidth = s3g::gui_layout::processorMenuWidth(kLabChartWidth);
        if (drawing::contains(point, drawing::makeRect(algorithmX, 265.0, algorithmWidth, 18.0))) {
            _openMenu = 4;
            setNeedsDisplay(true);
            return;
        }
        constexpr clap_id clockIds[] = {
            kModClockLockParamId, kModClockLock2ParamId, kModClockLock3ParamId
        };
        for (int operatorIndex = 0; operatorIndex < 3; ++operatorIndex) {
            const double cardX = labCardX(static_cast<uint32_t>(operatorIndex));
            const double controlX = s3g::gui_layout::processorControlX(cardX);
            const double menuWidth = s3g::gui_layout::processorMenuWidth(kLabCardWidth);
            if (drawing::contains(point, drawing::makeRect(controlX, kLabSourceRowY - 1.0, menuWidth, 18.0))) {
                _openMenu = 5 + operatorIndex * 2;
                setNeedsDisplay(true);
                return;
            }
            if (drawing::contains(point, drawing::makeRect(controlX, kLabTargetRowY - 1.0, menuWidth, 18.0))) {
                _openMenu = 6 + operatorIndex * 2;
                setNeedsDisplay(true);
                return;
            }
            if (drawing::contains(point, drawing::makeRect(controlX, kLabClockRowY - 1.0, menuWidth, 18.0))) {
                const uint32_t locks[] = {
                    p->params.modClockLock, p->params.modClockLock2, p->params.modClockLock3
                };
                applyParam(*p, clockIds[operatorIndex], locks[operatorIndex] == 0u ? 1.0 : 0.0);
                markHostStateDirty(*p);
                setNeedsDisplay(true);
                return;
            }
            if (drawing::contains(point, drawing::makeRect(controlX, kLabEnvelopeRowY - 1.0, menuWidth, 18.0))) {
                _openMenu = 11 + operatorIndex;
                setNeedsDisplay(true);
                return;
            }
        }
    } else {
        const double receiverControlX = s3g::gui_layout::processorControlX(kBassReceiverPanelX);
        const double receiverMenuWidth = s3g::gui_layout::processorMenuWidth(kBassReceiverPanelWidth);
        if (drawing::contains(point, drawing::makeRect(receiverControlX, 285.0, receiverMenuWidth, 18.0))) {
            _openMenu = 14;
            setNeedsDisplay(true);
            return;
        }
        if (drawing::contains(point, drawing::makeRect(receiverControlX, 311.0, receiverMenuWidth, 18.0))) {
            _openMenu = 15;
            setNeedsDisplay(true);
            return;
        }
        if (drawing::contains(point, drawing::makeRect(receiverControlX, 337.0, receiverMenuWidth, 18.0))) {
            _openMenu = 16;
            setNeedsDisplay(true);
            return;
        }
    }
    if (drawing::contains(point, drawing::makeRect(
            kMidiReceivePanelX + s3g::gui_layout::kStandardMetrics.hitInset,
            kPerformanceRowY - 9.0,
            kMidiReceivePanelWidth
                - s3g::gui_layout::kStandardMetrics.hitInset * 2.0,
            s3g::gui_layout::kStandardMetrics.hitHeight))) {
        _openMenu = 17;
        _hoverMenuItem = -1;
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point, drawing::makeRect(
            kOutputPanelX + s3g::gui_layout::kStandardMetrics.hitInset,
            kOutputFormatRowY - 9.0,
            kOutputPanelWidth
                - s3g::gui_layout::kStandardMetrics.hitInset * 2.0,
            s3g::gui_layout::kStandardMetrics.hitHeight))) {
        _openMenu = 18;
        _hoverMenuItem = -1;
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point, drawing::makeRect(
            kOutputPanelX, kOutputRotationRowY - 9.0,
            kOutputPanelWidth, 24.0))) {
        double defaultValue = 0.0;
        if (drawing::sliderDoubleClickDefault(
                event, &p->plugin, kOutputRotationParamId, &defaultValue)) {
            applyParam(*p, kOutputRotationParamId, defaultValue);
            markHostStateDirty(*p);
            _dragSlider = -1;
        } else {
            _dragSlider = 701;
            updateSlider(point);
        }
        return;
    }
    if (drawing::contains(point, drawing::makeRect(140, kPerformanceRowY - 10.0, 148, 22))) {
        applyParam(*p, kPerformanceModeParamId,
            point.x < 214.0 ? static_cast<double>(PerformanceMode::Free) : static_cast<double>(PerformanceMode::Midi));
        markHostStateDirty(*p);
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point, drawing::makeRect(1000, 49, 46, 18))) {
        applyParam(*p, kRunParamId, 1.0);
        markHostStateDirty(*p);
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point, drawing::makeRect(1054, 49, 48, 18))) {
        applyParam(*p, kRunParamId, 0.0);
        markHostStateDirty(*p);
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point, drawing::makeRect(1110, 49, 104, 18))) {
        chooseRawFile();
        return;
    }
    if (drawing::contains(point, drawing::makeRect(1224, 49, 108, 18))) {
        applyParam(*p, kRandomizeFieldParamId, std::fmod(static_cast<double>(p->params.seed) * 0.61803398875 + 0.37, 1.0));
        markHostStateDirty(*p);
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point, drawing::makeRect(466, kPresetRowY - 7.0, 126, 24))) {
        applyParam(*p, kRandomizePatchParamId, std::fmod(static_cast<double>(p->params.seed) * 0.754877666 + 0.21, 1.0));
        markHostStateDirty(*p);
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point, drawing::makeRect(604, kPresetRowY - 7.0, 90, 24))) {
        applyParam(*p, kMutateParamId, std::fmod(static_cast<double>(p->params.seed) * 0.569840291 + 0.43, 1.0));
        markHostStateDirty(*p);
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point, drawing::makeRect(706, kPresetRowY - 7.0, 90, 24))) {
        applyParam(*p, kUndoParamId, 1.0);
        markHostStateDirty(*p);
        setNeedsDisplay(true);
        return;
    }

    const EnvelopeGraphGeometry graph = envelopeGraphGeometry(*p);
    if (drawing::contains(point, graph.frame)) {
        const std::array<drawing::Point, 4> handles {
            graph.attack, graph.decay, graph.sustain, graph.release
        };
        int selected = -1;
        double bestDistance = 14.0 * 14.0;
        for (int index = 0; index < static_cast<int>(handles.size()); ++index) {
            const double distance = squaredDistance(point, handles[index]);
            if (distance <= bestDistance) {
                selected = index;
                bestDistance = distance;
            }
        }
        if (selected < 0) {
            if (point.x < kEnvelopeX + 190.0) selected = 0;
            else if (point.x < kEnvelopeX + 410.0) selected = 1;
            else if (point.x < kEnvelopeX + 500.0) selected = 2;
            else selected = 3;
        }
        _dragSlider = selected + 201;
        updateSlider(point);
        return;
    }

    if (_editorPage == 0) {
    static const double leftRows[] = { 292, 318, 344, 370, 396, 422, 474, 266 };
    static const clap_id leftIds[] = {
        kScanRateParamId, kTextureParamId, kGeometryParamId, kChaosParamId,
        kFoldParamId, kEvolveParamId, kChannelSpreadParamId, kGainParamId
    };
    for (int i = 0; i < 8; ++i) {
        if (drawing::contains(point, drawing::makeRect(
                kLeftToolboxX, leftRows[i] - 9.0, kToolboxWidth, 24))) {
            double defaultValue = 0.0;
            if (drawing::sliderDoubleClickDefault(
                    event, &p->plugin, leftIds[i], &defaultValue)) {
                applyParam(*p, leftIds[i], defaultValue);
                _dragSlider = -1;
            } else {
                _dragSlider = i + 1;
                updateSlider(point);
            }
            return;
        }
    }
    static const double rightRows[] = { 292, 318, 344, 370, 396, 422, 448, 474 };
    static const clap_id rightIds[] = {
        kCodecRateParamId, kBitDepthParamId, kCarrierTuneParamId, kCodecDamageParamId,
        kDriveParamId, kShredParamId, kResonanceParamId, kBassBodyParamId
    };
    for (int i = 0; i < 8; ++i) {
        if (drawing::contains(point, drawing::makeRect(
                kRightToolboxX, rightRows[i] - 9.0, kToolboxWidth, 24))) {
            double defaultValue = 0.0;
            if (drawing::sliderDoubleClickDefault(
                    event, &p->plugin, rightIds[i], &defaultValue)) {
                applyParam(*p, rightIds[i], defaultValue);
                if (rightIds[i] == kBassBodyParamId) markHostStateDirty(*p);
                _dragSlider = -1;
            } else {
                _dragSlider = i + 101;
                updateSlider(point);
            }
            return;
        }
    }
    }
    if (_editorPage == 2) {
    static const double bassRows[] = {
        286, 312, 338, 364, 390, 416, 442, 468
    };
    static const clap_id bassIds[] = {
        kBassBodyParamId, kBassPunchParamId, kBassTraceParamId,
        kBassFuzzParamId, kBassMetalParamId, kBassFeedbackParamId,
        kBassGlideParamId, kBassLowWidthParamId
    };
    for (int i = 0; i < 8; ++i) {
        if (drawing::contains(point, drawing::makeRect(
                kBassControlPanelX, bassRows[i] - 9.0, kBassControlPanelWidth, 24))) {
            double defaultValue = 0.0;
            if (drawing::sliderDoubleClickDefault(
                    event, &p->plugin, bassIds[i], &defaultValue)) {
                applyParam(*p, bassIds[i], defaultValue);
                markHostStateDirty(*p);
                _dragSlider = -1;
            } else {
                _dragSlider = i + 601;
                updateSlider(point);
            }
            return;
        }
    }
    }
    if (_editorPage == 1) {
    static const double modRows[] = { kLabRateRowY, kLabRatioRowY, kLabIndexRowY, kLabFeedbackRowY };
    static const clap_id modIds[] = {
        kModRateParamId, kModRatioParamId, kModIndexParamId, kModFeedbackParamId
    };
    for (int i = 0; i < 4; ++i) {
        if (drawing::contains(point, drawing::makeRect(
                labCardX(0u), modRows[i] - 9.0, kLabCardWidth, 22))) {
            double defaultValue = 0.0;
            if (drawing::sliderDoubleClickDefault(
                    event, &p->plugin, modIds[i], &defaultValue)) {
                applyParam(*p, modIds[i], defaultValue);
                _dragSlider = -1;
            } else {
                _dragSlider = i + 301;
                updateSlider(point);
            }
            return;
        }
    }
    static const clap_id mod2Ids[] = {
        kModRate2ParamId, kModRatio2ParamId, kModIndex2ParamId, kModFeedback2ParamId
    };
    for (int i = 0; i < 4; ++i) {
        if (drawing::contains(point, drawing::makeRect(
                labCardX(1u), modRows[i] - 9.0, kLabCardWidth, 22))) {
            double defaultValue = 0.0;
            if (drawing::sliderDoubleClickDefault(
                    event, &p->plugin, mod2Ids[i], &defaultValue)) {
                applyParam(*p, mod2Ids[i], defaultValue);
                _dragSlider = -1;
            } else {
                _dragSlider = i + 401;
                updateSlider(point);
            }
            return;
        }
    }
    static const clap_id mod3Ids[] = {
        kModRate3ParamId, kModRatio3ParamId, kModIndex3ParamId, kModFeedback3ParamId
    };
    for (int i = 0; i < 4; ++i) {
        if (drawing::contains(point, drawing::makeRect(
                labCardX(2u), modRows[i] - 9.0, kLabCardWidth, 22))) {
            double defaultValue = 0.0;
            if (drawing::sliderDoubleClickDefault(
                    event, &p->plugin, mod3Ids[i], &defaultValue)) {
                applyParam(*p, mod3Ids[i], defaultValue);
                _dragSlider = -1;
            } else {
                _dragSlider = i + 501;
                updateSlider(point);
            }
            return;
        }
    }
    }

}

void mouseDragged(drawing::Event*  event)  {
    if (_dragSlider > 0) updateSlider(convertPoint_fromView(event->locationInWindow(), nullptr));

}

void mouseMoved(drawing::Event*  event)  {
    updateMenuHover(convertPoint_fromView(event->locationInWindow(), nullptr));

}


void mouseUp(drawing::Event*  event)  {
    (void)event;
    if (_dragSlider == 108 || (_dragSlider >= 601 && _dragSlider <= 608)
        || _dragSlider == 701) {
        markHostStateDirty(*&_readout);
    }
    _dragSlider = -1;

}

};
}
s3g::portable_gui::foundation::ContentView* makePortableCanvas(Plugin& p) { return new feedback_processor_canvas::Editor(p); }
#include "s3g_clap_canvas_gui.inc"
