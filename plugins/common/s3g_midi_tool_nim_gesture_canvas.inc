// Cocoa geometry, drawing order and interaction maps retained.
namespace midi_tool_canvas {
namespace drawing = s3g::portable_gui::midi_drawing;
namespace foundation = s3g::portable_gui::foundation;

struct GuiControl {
    char name[20] {};
    char abbreviation[6] {};
    uint16_t parameter = 0u;
    uint32_t color = 0x777777u;
    bool action = false;
};

struct GuiPage {
    const char* title = "";
    std::array<GuiControl, 16u> controls {};
};

constexpr uint32_t kNetworkColor = 0x607b7d;
constexpr uint32_t kMovementColor = 0x9a6851;
constexpr uint32_t kSurfaceColor = 0x697d8f;
constexpr uint32_t kOutputColor = 0xa4a4a4;
constexpr uint32_t kToneColor = 0x71858b;
constexpr uint32_t kAuxAColor = 0x977642;
constexpr uint32_t kAuxBColor = 0x745f82;
constexpr uint32_t kLevelColor = 0xa4a4a4;
constexpr uint32_t kBodyColor = 0x66806e;
constexpr uint32_t kEqColor = 0x5e7889;
constexpr uint32_t kTuneColor = 0x776a8f;
constexpr uint32_t kMatrixSelfColor = 0x7d654e;
constexpr uint32_t kMatrixNextColor = 0x607b7d;
constexpr uint32_t kMatrixFarColor = 0x735f73;

const std::array<GuiPage, 12u>& guiPages()
{
    static const std::array<GuiPage, 12u> pages = [] {
        std::array<GuiPage, 12u> result {};
        const char* titles[] {
            "LIVE", "MOTION", "MIXER", "MATRIX1", "MATRIX2", "SENDS",
            "AUXTONE", "EQ LOHI", "EQ MID", "TUNING", "RETURNS", "ACTIONS",
        };
        for (uint32_t page = 0u; page < result.size(); ++page) {
            result[page].title = titles[page];
        }
        const auto set = [&](uint32_t page, uint32_t slot, const char* name,
                             const char* abbreviation, uint16_t parameter,
                             uint32_t color, bool action = false) {
            auto& control = result[page].controls[slot];
            std::snprintf(control.name, sizeof(control.name), "%s", name);
            std::snprintf(control.abbreviation,
                sizeof(control.abbreviation), "%s", abbreviation);
            control.parameter = parameter;
            control.color = color;
            control.action = action;
        };
        const auto laneParameter = [](uint32_t lane, uint32_t offset) {
            return static_cast<uint16_t>(1000u + lane * 100u + offset);
        };
        const auto matrixParameter = [](uint32_t destination,
                                        uint32_t source) {
            return static_cast<uint16_t>(100u + destination * 8u + source);
        };

        const char* liveNames[] { "Feedback", "Coupling", "Flow", "Phase",
            "Agency", "Motion", "Spread", "Vortex", "Formant", "Space",
            "Variance", "R Mode", "Aux A Ret", "Aux B Ret", "Drift",
            "Output" };
        const char* liveLabels[] { "FDBK", "COUP", "FLOW", "PHAS", "AGCY",
            "MOTN", "SPRD", "VRTX", "FORM", "SPAC", "VAR", "RMOD",
            "ARET", "BRET", "DRFT", "OUT!" };
        const uint16_t liveIds[] { 5u, 6u, 16u, 7u, 11u, 19u, 17u, 18u,
            9u, 12u, 13u, 44u, 26u, 31u, 8u, 1u };
        const uint32_t liveColors[] { kNetworkColor, kNetworkColor,
            kMovementColor, kNetworkColor, kNetworkColor, kMovementColor,
            kMovementColor, kMovementColor, kToneColor, kNetworkColor,
            kSurfaceColor, kSurfaceColor, kAuxAColor, kAuxBColor,
            kNetworkColor, kOutputColor };
        for (uint32_t slot = 0u; slot < 16u; ++slot) {
            set(0u, slot, liveNames[slot], liveLabels[slot], liveIds[slot],
                liveColors[slot]);
        }

        const char* motionNames[] { "Event Rate", "Event Length", "Density",
            "Chaos", "Slew", "Choke", "Move Rate", "Move Phase",
            "Random Depth", "Random Thresh", "Random Attack", "Random Release",
            "Random Polar", "Shape", "Behavior", "Output" };
        const char* motionLabels[] { "EVRT", "EVLN", "DENS", "CHAO", "SLEW",
            "CHOK", "MVRT", "MVPH", "RDEP", "RTHR", "RATK", "RREL",
            "RPOL", "SHAP", "BEHV", "OUT!" };
        const uint16_t motionIds[] { 36u, 37u, 38u, 39u, 40u, 41u, 21u, 22u,
            45u, 46u, 47u, 48u, 49u, 20u, 35u, 1u };
        for (uint32_t slot = 0u; slot < 16u; ++slot) {
            set(1u, slot, motionNames[slot], motionLabels[slot],
                motionIds[slot], slot >= 8u && slot <= 12u
                    ? kSurfaceColor
                    : (slot == 15u ? kOutputColor : kMovementColor));
        }

        for (uint32_t lane = 0u; lane < 8u; ++lane) {
            char name[20] {};
            char label[6] {};
            std::snprintf(name, sizeof(name), "Lane %u Level", lane + 1u);
            std::snprintf(label, sizeof(label), "%uLVL", lane + 1u);
            set(2u, lane, name, label, laneParameter(lane, 2u), kLevelColor);
            std::snprintf(name, sizeof(name), "Lane %u Body", lane + 1u);
            std::snprintf(label, sizeof(label), "%uBDY", lane + 1u);
            set(2u, lane + 8u, name, label, laneParameter(lane, 0u),
                kBodyColor);
        }

        for (uint32_t lane = 0u; lane < 8u; ++lane) {
            char name[20] {};
            char label[6] {};
            std::snprintf(name, sizeof(name), "M%u Self", lane + 1u);
            std::snprintf(label, sizeof(label), "%uSLF", lane + 1u);
            set(3u, lane, name, label, matrixParameter(lane, lane),
                kMatrixSelfColor);
            const uint32_t destination = (lane + 1u) % 8u;
            std::snprintf(name, sizeof(name), "M%u to %u", lane + 1u,
                destination + 1u);
            std::snprintf(label, sizeof(label), "%u>%u", lane + 1u,
                destination + 1u);
            set(3u, lane + 8u, name, label,
                matrixParameter(destination, lane), kMatrixNextColor);
        }

        for (uint32_t slot = 0u; slot < 8u; ++slot) {
            const uint32_t source = (slot + 1u) % 8u;
            const uint32_t destination = slot;
            char name[20] {};
            char label[6] {};
            std::snprintf(name, sizeof(name), "M%u to %u", source + 1u,
                destination + 1u);
            std::snprintf(label, sizeof(label), "%u>%u", source + 1u,
                destination + 1u);
            set(4u, slot, name, label, matrixParameter(destination, source),
                kMatrixNextColor);
        }
        const uint8_t farPairs[8u][2u] {
            { 0u, 4u }, { 4u, 0u }, { 1u, 5u }, { 5u, 1u },
            { 2u, 6u }, { 6u, 2u }, { 3u, 7u }, { 7u, 3u },
        };
        for (uint32_t slot = 0u; slot < 8u; ++slot) {
            const uint32_t source = farPairs[slot][0u];
            const uint32_t destination = farPairs[slot][1u];
            char name[20] {};
            char label[6] {};
            std::snprintf(name, sizeof(name), "M%u to %u", source + 1u,
                destination + 1u);
            std::snprintf(label, sizeof(label), "%u>%u", source + 1u,
                destination + 1u);
            set(4u, slot + 8u, name, label,
                matrixParameter(destination, source), kMatrixFarColor);
        }

        const auto laneRows = [&](uint32_t page, uint32_t firstOffset,
                                  const char* firstName, const char* firstCode,
                                  uint32_t firstColor, uint32_t secondOffset,
                                  const char* secondName, const char* secondCode,
                                  uint32_t secondColor) {
            for (uint32_t lane = 0u; lane < 8u; ++lane) {
                char name[20] {};
                char label[6] {};
                std::snprintf(name, sizeof(name), "L%u %s", lane + 1u,
                    firstName);
                std::snprintf(label, sizeof(label), "%u%s", lane + 1u,
                    firstCode);
                set(page, lane, name, label, laneParameter(lane, firstOffset),
                    firstColor);
                std::snprintf(name, sizeof(name), "L%u %s", lane + 1u,
                    secondName);
                std::snprintf(label, sizeof(label), "%u%s", lane + 1u,
                    secondCode);
                set(page, lane + 8u, name, label,
                    laneParameter(lane, secondOffset), secondColor);
            }
        };
        for (uint32_t lane = 0u; lane < 8u; ++lane) {
            char name[20] {};
            char label[6] {};
            std::snprintf(name, sizeof(name), "Aux A Lane %u Send",
                lane + 1u);
            std::snprintf(label, sizeof(label), "A%uSD", lane + 1u);
            set(5u, lane, name, label, laneParameter(lane, 8u),
                kAuxAColor);
            std::snprintf(name, sizeof(name), "Aux B Lane %u Send",
                lane + 1u);
            std::snprintf(label, sizeof(label), "B%uSD", lane + 1u);
            set(5u, lane + 8u, name, label, laneParameter(lane, 9u),
                kAuxBColor);
        }

        const char* auxNames[] { "Interaction", "House Tone", "A Type",
            "B Type", "A Gain", "A Tone", "A Bias", "A Return", "B Gain",
            "B Tone", "B Bias", "B Return", "A Feedback", "B Feedback",
            "Ceiling", "Output" };
        const char* auxLabels[] { "INTR", "HOUS", "ATYP", "BTYP", "AGAN",
            "ATON", "ABIA", "AR/M", "BGAN", "BTON", "BBIA", "BR/M",
            "AFBK", "BFBK", "CEIL", "OUT!" };
        const uint16_t auxIds[] { 14u, 15u, 23u, 28u, 24u, 25u, 42u, 26u,
            29u, 30u, 43u, 31u, 27u, 32u, 2u, 1u };
        for (uint32_t slot = 0u; slot < 16u; ++slot) {
            const uint32_t color = slot < 2u ? kToneColor
                : ((slot == 2u) || (slot >= 4u && slot <= 7u)
                        || slot == 12u)
                    ? kAuxAColor
                    : ((slot == 3u) || (slot >= 8u && slot <= 11u)
                            || slot == 13u)
                        ? kAuxBColor : kOutputColor;
            set(6u, slot, auxNames[slot], auxLabels[slot], auxIds[slot], color);
        }

        laneRows(7u, 4u, "Low", "LOW", kEqColor,
            7u, "High", "HIG", kEqColor);
        laneRows(8u, 5u, "Mid Hz", "MHZ", kEqColor,
            6u, "Mid Gain", "MGN", kEqColor);
        laneRows(9u, 10u, "Note", "NOT", kTuneColor,
            11u, "Cents", "CTS", kTuneColor);
        for (uint32_t lane = 0u; lane < 8u; ++lane) {
            char name[20] {};
            char label[6] {};
            std::snprintf(name, sizeof(name), "Aux A Lane %u Return",
                lane + 1u);
            std::snprintf(label, sizeof(label), "A%uRN", lane + 1u);
            set(10u, lane, name, label, laneParameter(lane, 15u),
                kAuxAColor);
            std::snprintf(name, sizeof(name), "Aux B Lane %u Return",
                lane + 1u);
            std::snprintf(label, sizeof(label), "B%uRN", lane + 1u);
            set(10u, lane + 8u, name, label, laneParameter(lane, 16u),
                kAuxBColor);
        }

        const char* actionNames[] { "Record", "Playback", "Clear Last",
            "Clear All", "Cancel", "Matrix Flip", "Matrix Latch",
            "New Sign", "Seed", "Random Low", "Random Mid", "Random High",
            "Forget", "Clear Matrix", "BU16 Ramp", "Output / Panic" };
        const char* actionLabels[] { "REC", "PLAY", "CLRL", "CLRA", "CNCL",
            "FLIP", "LTCH", "SIGN", "SEED", "RLO", "RMD", "RHI", "FORG",
            "MX0", "RAMP", "OUT!" };
        const uint16_t actionIds[] { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
            0u, 0u, 0u, 0u, 0u, 0u, 59u, 1u };
        for (uint32_t slot = 0u; slot < 16u; ++slot) {
            set(11u, slot, actionNames[slot], actionLabels[slot],
                actionIds[slot], slot == 5u ? kMatrixNextColor
                    : (slot == 6u || slot == 7u) ? kMatrixFarColor
                    : slot == 14u ? kMovementColor : kOutputColor,
                slot < 14u);
        }
        return result;
    }();
    return pages;
}

constexpr std::array<uint8_t, 12u> kPageLayoutOrder {
    0u, 1u, 6u, 11u,
    3u, 4u, 5u, 10u,
    2u, 7u, 8u, 9u,
};

drawing::Rect guiCardRect(uint32_t layoutSlot)
{
    constexpr double margin = 18.0;
    constexpr double gap = 12.0;
    constexpr double width = 277.5;
    constexpr double height = 218.0;
    const uint32_t column = layoutSlot % 4u;
    const uint32_t row = layoutSlot / 4u;
    return drawing::makeRect(margin + column * (width + gap),
        124.0 + row * (height + gap), width, height);
}

uint32_t guiLayoutSlotForPage(uint32_t page)
{
    for (uint32_t slot = 0u; slot < kPageLayoutOrder.size(); ++slot) {
        if (kPageLayoutOrder[slot] == page) return slot;
    }
    return 0u;
}

drawing::Rect guiControlCellRect(uint32_t page, uint32_t slot)
{
    const drawing::Rect card = guiCardRect(guiLayoutSlotForPage(page));
    constexpr double inset = 8.0;
    constexpr double headerHeight = 25.0;
    const double cellWidth = (card.size.width - inset * 2.0) / 4.0;
    const double cellHeight = (card.size.height - headerHeight - 5.0) / 4.0;
    return drawing::makeRect(card.origin.x + inset + (slot % 4u) * cellWidth,
        card.origin.y + headerHeight + (slot / 4u) * cellHeight,
        cellWidth, cellHeight);
}

void strokeGuiArc(drawing::Point center, double radius, double startDegrees,
    double spanDegrees, VSTGUI::CColor  color, double width)
{
    constexpr double radians = 3.14159265358979323846 / 180.0;
    const uint32_t steps = std::max<uint32_t>(1u,
        static_cast<uint32_t>(std::ceil(std::fabs(spanDegrees) / 6.0)));
    drawing::Path  path = drawing::Path::bezierPath();
    for (uint32_t step = 0u; step <= steps; ++step) {
        const double angle = (startDegrees + spanDegrees
            * static_cast<double>(step) / static_cast<double>(steps))
            * radians;
        const drawing::Point point = drawing::makePoint(center.x + radius * std::cos(angle),
            center.y + radius * std::sin(angle));
        if (step == 0u) path.moveToPoint(point);
        else path.lineToPoint(point);
    }
    drawing::setStroke(color);
    path.setLineWidth(width);
    drawing::strokeRoundPath(path);
}

class Editor final : public s3g::portable_gui::routing::Canvas {
public:
using Canvas = s3g::portable_gui::routing::Canvas;

    Plugin* _plugin;
    bool _draggingTakeover;
#include "s3g_midi_tool_editor.inc"
explicit Editor(Plugin& plugin) : Canvas(kGuiWidth,kGuiHeight) {_plugin = &plugin;
        _draggingTakeover = false;
}

void service() override {
    
    if (!isHidden() && _plugin
        && true) {
        setNeedsDisplay(true);
    }

}

drawing::Rect buttonRect(uint32_t index)  {
    const double widths[] { 68.0, 68.0, 88.0, 82.0, 68.0 };
    double x = 30.0;
    for (uint32_t button = 0u; button < index; ++button) {
        x += widths[button] + 7.0;
    }
    return drawing::makeRect(x, 76.0, widths[index], 25.0);

}

drawing::Rect takeoverTrackRect()  {
    return drawing::makeRect(510.0, 84.0, 204.0, 8.0);

}

drawing::Rect takeoverHitRect()  {
    return drawing::makeRect(440.0, 67.0, 350.0, 40.0);

}

void drawButton_label_active_semanticColor(uint32_t index, drawing::String  label, bool active, VSTGUI::CColor  semanticColor)  {
    drawing::Style style;
    const drawing::Rect button = buttonRect(index);
    drawing::drawHeaderButton(button, drawing::makeRect(18.0, 42.0,
        kGuiWidth - 36.0, 72.0), label, active,
        drawing::textAttrs(drawing::color(0xb8b8b8), 9.0), style);
    if (active && semanticColor.alpha != 0) {
        drawing::setFill(semanticColor);
        drawing::rectFill(drawing::makeRect(button.origin.x, button.origin.y,
            button.size.width, 2.0));
    }

}

void drawControl_page_slot(const GuiControl& control, uint32_t page, uint32_t slot)  {
    const drawing::Rect cell = guiControlCellRect(page, slot);
    const int32_t index = control.parameter != 0u
        ? parameterIndex(control.parameter) : -1;
    const bool selected = index >= 0 && _plugin->uiSelectedIndex.load(
        std::memory_order_relaxed) == index;
    if (selected) {
        drawing::setFill(drawing::color(0x333333));
        drawing::rectFill(drawing::inset(cell, 2.0, 1.0));
        drawing::setStroke(drawing::color(0x777777));
        drawing::frameRect(drawing::inset(cell, 2.5, 1.5));
    }

    const drawing::Point center = drawing::makePoint(drawing::midX(cell), cell.origin.y + 17.0);
    VSTGUI::CColor  signal = drawing::color(static_cast<int>(control.color));
    if (control.action) {
        strokeGuiArc(center, 12.0, 135.0, 270.0,
            drawing::color(0x464646), 2.0);
        drawing::setFill(drawing::color(0x202020));
        drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
            center.x - 8.0, center.y - 8.0, 16.0, 16.0)).fill();
        drawing::setFill(drawing::color(0x888888));
        drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
            center.x - 2.0, center.y - 2.0, 4.0, 4.0)).fill();
    } else {
        uint8_t flags = 0u;
        uint16_t raw = 0u;
        if (index >= 0) {
            flags = _plugin->uiFlags[static_cast<uint32_t>(index)].load(
                std::memory_order_relaxed);
            raw = _plugin->uiValues[static_cast<uint32_t>(index)].load(
                std::memory_order_relaxed);
        }
        const bool seen = (flags & kUiValueSeen) != 0u;
        const bool loopActive = (flags & kUiLoopActive) != 0u;
        const bool takeTouched = (flags & kUiTakeTouched) != 0u;
        const double normalized = static_cast<double>(raw) / 16383.0;
        if (loopActive || takeTouched) {
            strokeGuiArc(center, 15.0, 135.0, 270.0,
                takeTouched ? drawing::color(0xb56a5e) : signal,
                takeTouched ? 2.5 : 1.5);
        }
        strokeGuiArc(center, 12.0, 135.0, 270.0,
            drawing::color(0x3b3b3b), 2.5);
        if (seen) {
            strokeGuiArc(center, 12.0, 135.0, normalized * 270.0,
                signal, 2.5);
        }
        drawing::setFill(drawing::color(0x181818));
        drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
            center.x - 8.5, center.y - 8.5, 17.0, 17.0)).fill();
        const double angle = (135.0 + normalized * 270.0)
            * 3.14159265358979323846 / 180.0;
        drawing::Path  indicator = drawing::Path::bezierPath();
        indicator.moveToPoint(center);
        indicator.lineToPoint(drawing::makePoint(center.x + std::cos(angle) * 6.0,
            center.y + std::sin(angle) * 6.0));
        drawing::setStroke((seen ? signal : drawing::color(0x4c4c4c)));
        indicator.setLineWidth(1.5);
        indicator.stroke();
        if (loopActive) {
            drawing::setFill(signal);
            drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
                center.x + 10.0, center.y - 13.0, 4.0, 4.0)).fill();
        }
    }

    drawing::String  abbreviation = drawing::stringFromUtf8(control.abbreviation);
    drawing::Attrs  labelAttrs = drawing::textAttrs(
        control.action ? drawing::color(0x888888)
            : drawing::color(0xaaaaaa), 7.5);
    const drawing::Size labelSize = drawing::sizeWithAttributes(abbreviation, labelAttrs);
    drawing::drawAtPoint_withAttributes(abbreviation, drawing::makePoint(drawing::midX(cell) - labelSize.width * 0.5,
        cell.origin.y + 33.0), labelAttrs);

}

void drawOriginal(drawing::Rect dirtyRect)  {
    (void)dirtyRect;
    if (!_plugin) return;
    drawing::Style style;
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());

    drawing::Attrs  titleAttrs = drawing::softTitleAttrs();
    drawing::Attrs  labelAttrs = drawing::softLabelAttrs();
    drawing::Attrs  valueAttrs = drawing::softValueAttrs();
    drawing::drawAtPoint_withAttributes("s3g UTILITY NIM GESTURE", drawing::makePoint(18.0, 14.0), titleAttrs);
    drawing::String  titleStatus = drawing::stringFormat("%u LOOPS  ·  FREE  ·  NRPN CH 16",
        _plugin->uiLoopCount.load(std::memory_order_relaxed));
    drawing::drawRightStatus(titleStatus, kGuiWidth, 14.0, valueAttrs);

    const drawing::Rect transport = drawing::makeRect(18.0, 42.0, kGuiWidth - 36.0, 72.0);
    drawing::drawPanelFrame(transport.origin.x, transport.origin.y,
        transport.size.width, transport.size.height, style);
    drawing::drawPanelHeader("FREE MOTION / E16 CONTROL", true,
        transport.origin.x, transport.origin.y, transport.size.width, 22.0,
        labelAttrs, style);
    drawing::String  legend = "INNER RING  VALUE    OUTER RING  LOOP";
    drawing::drawAtPoint_withAttributes(legend, drawing::makePoint(drawing::maxX(transport)
        - drawing::sizeWithAttributes(legend, valueAttrs).width - 10.0, 47.0), valueAttrs);

    const bool recording = _plugin->uiRecording.load(std::memory_order_relaxed);
    const bool playing = _plugin->uiPlaying.load(std::memory_order_relaxed);
    drawButton_label_active_semanticColor(0u, "RECORD", recording, drawing::color(0xb56a5e));
    drawButton_label_active_semanticColor(1u, "PLAY", playing, drawing::color(0x70866f));
    drawButton_label_active_semanticColor(2u, "CLEAR SEL", false, VSTGUI::CColor(0,0,0,0));
    drawButton_label_active_semanticColor(3u, "CLEAR ALL", false, VSTGUI::CColor(0,0,0,0));
    drawButton_label_active_semanticColor(4u, "CANCEL", false, VSTGUI::CColor(0,0,0,0));

    const double takeover = _plugin->uiTakeoverMs.load(
        std::memory_order_relaxed);
    drawing::drawAtPoint_withAttributes("TAKEOVER", drawing::makePoint(445.0, 78.0), labelAttrs);
    const drawing::Rect takeoverTrack = takeoverTrackRect();
    drawing::setFill(drawing::color(0x101010));
    drawing::rectFill(takeoverTrack);
    drawing::setFill(drawing::color(0x4f4f4f));
    drawing::rectFill(drawing::makeRect(takeoverTrack.origin.x, takeoverTrack.origin.y,
        takeoverTrack.size.width * std::clamp(takeover / 5000.0, 0.0, 1.0),
        takeoverTrack.size.height));
    drawing::setStroke(drawing::color(0x666666));
    drawing::frameRect(takeoverTrack);
    drawing::String  takeoverText = drawing::stringFormat("%.0f ms", takeover);
    drawing::drawAtPoint_withAttributes(takeoverText, drawing::makePoint(725.0, 78.0), valueAttrs);

    const int32_t selected = _plugin->uiSelectedIndex.load(
        std::memory_order_relaxed);
    drawing::String  selectedText = "SELECT AN ENCODER";
    drawing::String  selectedValue = "CLICK TO INSPECT · DOUBLE-CLICK TO CLEAR LOOP";
    if (selected >= 0
        && selected < static_cast<int32_t>(kNimParameterCount)) {
        const uint16_t id = parameterId(static_cast<uint32_t>(selected));
        const uint16_t raw = _plugin->uiValues[
            static_cast<uint32_t>(selected)].load(std::memory_order_relaxed);
        const uint8_t flags = _plugin->uiFlags[
            static_cast<uint32_t>(selected)].load(std::memory_order_relaxed);
        const float length = _plugin->uiLoopLengths[
            static_cast<uint32_t>(selected)].load(std::memory_order_relaxed);
        const char* name = "NIM PARAMETER";
        for (const auto& page : guiPages()) {
            for (const auto& control : page.controls) {
                if (control.parameter == id) {
                    name = control.name;
                    break;
                }
            }
        }
        selectedText = drawing::stringFormat("%s  ·  NRPN %u", name, id);
        selectedValue = drawing::stringFormat("RAW %u  ·  %s%.3f s",
            raw, (flags & kUiLoopActive) != 0u ? "LOOP " : "NO LOOP  ",
            length);
    }
    drawing::drawAtPoint_withAttributes(selectedText, drawing::makePoint(812.0, 74.0), labelAttrs);
    drawing::drawAtPoint_withAttributes(selectedValue, drawing::makePoint(812.0, 91.0), valueAttrs);

    const auto& pages = guiPages();
    for (uint32_t layoutSlot = 0u; layoutSlot < kPageLayoutOrder.size();
         ++layoutSlot) {
        const uint32_t pageIndex = kPageLayoutOrder[layoutSlot];
        const GuiPage& page = pages[pageIndex];
        const drawing::Rect card = guiCardRect(layoutSlot);
        drawing::drawPanelFrame(card.origin.x, card.origin.y,
            card.size.width, card.size.height, style);
        drawing::String  header = drawing::stringFormat("P%02u  %s",
            layoutSlot + 1u, page.title);
        drawing::drawPanelHeader(header, true, card.origin.x,
            card.origin.y, card.size.width, 23.0, labelAttrs, style);
        uint32_t pageLoops = 0u;
        for (const auto& control : page.controls) {
            const int32_t controlIndex = parameterIndex(control.parameter);
            if (controlIndex >= 0
                && (_plugin->uiFlags[static_cast<uint32_t>(controlIndex)].load(
                    std::memory_order_relaxed) & kUiLoopActive) != 0u) {
                ++pageLoops;
            }
        }
        drawing::String  countText = pageLoops == 0u ? "—"
            : drawing::stringFormat("%u", pageLoops);
        drawing::drawAtPoint_withAttributes(countText, drawing::makePoint(drawing::maxX(card)
            - drawing::sizeWithAttributes(countText, valueAttrs).width - 8.0,
            card.origin.y + 5.0), valueAttrs);
        for (uint32_t slot = 0u; slot < 16u; ++slot) {
            drawControl_page_slot(page.controls[slot], pageIndex, slot);
        }
    }

}

void updateTakeover(drawing::Point point)  {
    if (!_plugin) return;
    const drawing::Rect track = takeoverTrackRect();
    const double normalized = std::clamp(
        (point.x - track.origin.x) / track.size.width, 0.0, 1.0);
    const double milliseconds = normalized * 5000.0;
    _plugin->guiTakeoverMs.store(milliseconds, std::memory_order_relaxed);
    _plugin->uiTakeoverMs.store(milliseconds, std::memory_order_relaxed);
    requestGuiCommand(*_plugin, kGuiSetTakeover);
    setNeedsDisplay(true);

}

void mouseDown(drawing::Event*  event)  {
    if (!_plugin) return;
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    const uint32_t commands[] { kGuiToggleRecord, kGuiTogglePlay,
        kGuiClearSelected, kGuiClearAll, kGuiCancelRecord };
    for (uint32_t index = 0u; index < 5u; ++index) {
        if (drawing::contains(point, buttonRect(index))) {
            requestGuiCommand(*_plugin, commands[index]);
            return;
        }
    }
    if (drawing::contains(point, takeoverHitRect())) {
        _draggingTakeover = true;
        if (event->clickCount() >= 2) {
            _plugin->guiTakeoverMs.store(650.0, std::memory_order_relaxed);
            _plugin->uiTakeoverMs.store(650.0, std::memory_order_relaxed);
            requestGuiCommand(*_plugin, kGuiSetTakeover);
        } else {
            updateTakeover(point);
        }
        return;
    }
    const auto& pages = guiPages();
    for (uint32_t page = 0u; page < pages.size(); ++page) {
        for (uint32_t slot = 0u; slot < 16u; ++slot) {
            if (!drawing::contains(point, guiControlCellRect(page, slot))) continue;
            const GuiControl& control = pages[page].controls[slot];
            const int32_t selected = parameterIndex(control.parameter);
            if (selected < 0 || control.action) return;
            _plugin->uiSelectedIndex.store(selected, std::memory_order_relaxed);
            if (event->clickCount() >= 2) {
                requestGuiCommand(*_plugin, kGuiClearSelected);
            }
            setNeedsDisplay(true);
            return;
        }
    }

}

void mouseDragged(drawing::Event*  event)  {
    if (!_draggingTakeover) return;
    updateTakeover(convertPoint_fromView(event->locationInWindow(), nullptr));

}

void mouseUp(drawing::Event*  event)  {
    (void)event;
    _draggingTakeover = false;

}

};
}
s3g::portable_gui::foundation::ContentView* makePortableCanvas(Plugin& p) { return new midi_tool_canvas::Editor(p); }
#include "s3g_clap_canvas_gui.inc"
