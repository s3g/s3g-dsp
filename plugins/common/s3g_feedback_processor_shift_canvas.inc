// Adapted from retained Cocoa geometry, drawing order and interaction maps.
namespace feedback_processor_canvas {
namespace drawing = s3g::portable_gui::feedback_drawing;
namespace foundation = s3g::portable_gui::foundation;
constexpr auto& presetState = stateExtension;
// The owning translation unit hosts this canvas in the shared
// ResponsiveViewport; the controls below follow the Processor-family contract.
constexpr s3g::gui_layout::Canvas kFeedbackCanvas {
    static_cast<double>(kGuiWidth), static_cast<double>(kGuiHeight) };
constexpr drawing::Rect kFeedbackPageBand {
    18.0, 42.0, 1064.0, 24.0 };
constexpr s3g::gui_layout::Panel kMatrixPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Routing,
    { 18.0, 74.0, 640.0, 668.0 }, 36.0, 26.0, 0u };
constexpr s3g::gui_layout::Panel kPatchScenePanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Modulation,
    { 670.0, 74.0, 412.0, 188.0 }, 54.0, 30.0, 4u };
constexpr s3g::gui_layout::Panel kNodeStripPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 18.0, 74.0, 1064.0, 62.0 }, 0.0, 0.0, 0u };
constexpr s3g::gui_layout::Panel kNodePanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 18.0, 148.0, 340.0, 594.0 }, 54.0, 45.0, 9u };
constexpr s3g::gui_layout::Panel kInsertSelectorPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 370.0, 148.0, 712.0, 106.0 }, 52.0, 30.0, 2u };
constexpr s3g::gui_layout::Panel kAuxSendPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 18.0, 74.0, 1064.0, 250.0 }, 0.0, 0.0, 0u };
constexpr s3g::gui_layout::Panel kAuxSendLeftPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 18.0, 74.0, 532.0, 250.0 }, 62.0, 38.0, 4u };
constexpr s3g::gui_layout::Panel kAuxSendRightPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 550.0, 74.0, 532.0, 250.0 }, 62.0, 38.0, 4u };
constexpr s3g::gui_layout::Panel kAuxFeedbackPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::ToneShape,
    { 18.0, 336.0, 520.0, 406.0 }, 58.0, 48.0, 6u };
constexpr s3g::gui_layout::Panel kPostGrainPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::ToneShape,
    { 550.0, 336.0, 532.0, 406.0 }, 50.0, 34.0, 10u };
constexpr s3g::gui_layout::Panel kExcitationPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Engine,
    { 18.0, 74.0, 340.0, 410.0 }, 58.0, 48.0, 5u };
constexpr s3g::gui_layout::Panel kMorphPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Modulation,
    { 370.0, 74.0, 712.0, 410.0 }, 58.0, 48.0, 5u };
constexpr s3g::gui_layout::Panel kMorphLeftPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Modulation,
    { 370.0, 74.0, 356.0, 410.0 }, 58.0, 48.0, 5u };
constexpr s3g::gui_layout::Panel kMorphRightPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Modulation,
    { 726.0, 74.0, 356.0, 410.0 }, 58.0, 48.0, 5u };
constexpr s3g::gui_layout::Panel kOutputPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Output,
    { 18.0, 496.0, 340.0, 246.0 }, 58.0, 44.0, 4u };
constexpr s3g::gui_layout::Panel kEcologyActivityPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    { 370.0, 496.0, 712.0, 246.0 }, 0.0, 0.0, 0u };
constexpr s3g::gui_layout::Panel kCrosspointPanel {
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Routing,
    { 670.0, 274.0, 412.0, 468.0 }, 62.0, 42.0, 2u };

constexpr double kFeedbackMatrixX = 150.0;
constexpr double kFeedbackMatrixY = 145.0;
constexpr double kFeedbackMatrixCell = 50.0;
constexpr double kFeedbackMenuItemHeight = 18.0;

enum FeedbackPage : uint32_t {
    kFeedbackPagePatch = 0u,
    kFeedbackPageInsert,
    kFeedbackPageEcology,
    kFeedbackPageAux,
    kFeedbackPageCount,
};

enum FeedbackMenu : int {
    kFeedbackMenuNone = 0,
    kFeedbackMenuPreset,
    kFeedbackMenuNodeMode,
    kFeedbackMenuInsertCategory,
    kFeedbackMenuInsertEffect,
    kFeedbackMenuExciterSource,
    kFeedbackMenuMorphSource,
    kFeedbackMenuOutputMode,
    kFeedbackMenuRun,
    kFeedbackMenuMorphHold,
    kFeedbackMenuMorphSync,
    kFeedbackMenuMorphDivision,
    kFeedbackMenuMorphShape,
    kFeedbackMenuEffectBase = 100,
};

s3g::gui_layout::Panel feedbackEffectPanel(uint32_t rows)
{
    const double rowPitch = s3g::gui_layout::kStandardMetrics.rowPitch;
    (void)rows;
    return {
        s3g::gui_layout::PluginClass::MixerMatrixLane,
        s3g::gui_layout::PanelRole::ToneShape,
        { 370.0, 266.0, 712.0, 476.0 },
        s3g::gui_layout::kStandardMetrics.firstRowOffset, rowPitch, rows,
    };
}

drawing::Rect feedbackMatrixCellRect(uint32_t destination, uint32_t source)
{
    return drawing::makeRect(kFeedbackMatrixX
            + static_cast<double>(source) * kFeedbackMatrixCell,
        kFeedbackMatrixY
            + static_cast<double>(destination) * kFeedbackMatrixCell,
        34.0, 34.0);
}

drawing::Rect feedbackPageButtonRect(uint32_t page)
{
    return drawing::makeRect(28.0 + static_cast<double>(page) * 92.0,
        44.0, 86.0, 20.0);
}

drawing::Rect feedbackNodeButtonRect(uint32_t page, uint32_t node)
{
    if (page == kFeedbackPageInsert) {
        return drawing::makeRect(34.0 + static_cast<double>(node) * 129.0,
            104.0, 58.0, 22.0);
    }
    return drawing::makeRect(386.0 + static_cast<double>(node) * 84.0,
        528.0, 72.0, 22.0);
}

drawing::Rect feedbackNodeRandomButtonRect(uint32_t node)
{
    return drawing::makeRect(96.0 + static_cast<double>(node) * 129.0,
        104.0, 58.0, 22.0);
}

drawing::Rect feedbackSceneButtonRect(bool sceneB)
{
    return drawing::makeRect(sceneB ? 748.0 : 710.0, 44.0, 32.0, 20.0);
}

drawing::Rect feedbackRandomButtonRect(bool sceneB)
{
    return drawing::makeRect(sceneB ? 882.0 : 790.0, 44.0, 86.0, 20.0);
}

drawing::Rect feedbackSceneCopyButtonRect(bool toSceneB)
{
    return drawing::makeRect(686.0, toSceneB ? 140.0 : 170.0,
        380.0, 20.0);
}

drawing::Rect feedbackProcessorMenuRect(
    const s3g::gui_layout::Panel& panel, uint32_t row)
{
    return drawing::makeRect(
        s3g::gui_layout::processorControlX(panel.frame.x),
        s3g::gui_layout::rowY(panel, row) - 1.0,
        s3g::gui_layout::processorMenuWidth(panel.frame.width), 15.0);
}

drawing::Rect feedbackActionButtonRect(uint32_t index)
{
    const double y = s3g::gui_layout::rowY(kExcitationPanel, 4u) - 2.0;
    return drawing::makeRect(34.0 + static_cast<double>(index) * 150.0,
        y, 140.0, 20.0);
}

drawing::Rect feedbackEngineBankButtonRect(uint32_t index)
{
    return drawing::makeRect(221.0 + static_cast<double>(index) * 40.0,
        80.0, 35.0, 18.0);
}

drawing::Rect feedbackCrosspointActionRect(uint32_t index)
{
    const double x = s3g::gui_layout::processorControlX(
        kCrosspointPanel.frame.x);
    const double y = s3g::gui_layout::rowY(kCrosspointPanel, 1u) - 2.0;
    return drawing::makeRect(x + static_cast<double>(index) * 140.0,
        y, 130.0, 20.0);
}

clap_id feedbackPedalStorageParamId(uint32_t node, uint32_t storageSlot)
{
    if (storageSlot < 4u) {
        return nodeParamId(node, kNodePedalAmountOffset + storageSlot);
    }
    return nodeParamId(node,
        kNodePedalExtra0Offset + storageSlot - 4u);
}

uint32_t feedbackVisiblePedalControlCount(s3g::FeedbackPedalType pedal)
{
    const uint32_t count = s3g::feedbackPedalControlCount(pedal);
    return pedal == s3g::FeedbackPedalType::Fracture && count > 0u
        ? count - 1u : count;
}

uint32_t feedbackVisiblePedalControlIndex(s3g::FeedbackPedalType pedal,
    uint32_t row)
{
    return pedal == s3g::FeedbackPedalType::Fracture ? row + 1u : row;
}

double feedbackNormalizedParam(const Plugin& plugin, clap_id id)
{
    uint32_t node = 0u;
    clap_id offset = 0u;
    const double value = paramValue(plugin, id);
    bool sceneB = false;
    if (decodeSceneParam(id, sceneB, node, offset)
        && offset == kSceneFrequencyOffset) {
        return s3g::feedbackShiftFrequencyControlNorm(
            static_cast<float>(value));
    }
    ParamRange range;
    if (!paramRange(id, range)) return 0.0;
    return static_cast<double>((value - range.minimum)
        / std::max(1.0e-9, range.maximum - range.minimum));
}

drawing::String  feedbackParamText(const Plugin& plugin, clap_id id)
{
    char text[48] {};
    paramsValueToText(&plugin.plugin, id, paramValue(plugin, id),
        text, sizeof(text));
    return drawing::stringFromUtf8(text);
}

drawing::String  feedbackPedalControlText(
    const s3g::FeedbackPedalControlInfo& info,
    double normalized, s3g::FeedbackPedalType pedal, uint32_t control)
{
    normalized = std::clamp(normalized, 0.0, 1.0);
    using Display = s3g::FeedbackPedalControlDisplay;
    switch (info.display) {
    case Display::Percent:
        return drawing::stringFormat("%3.0f%%", normalized * 100.0);
    case Display::Bipolar: {
        const double value = info.displayMinimum
            + normalized * (info.displayMaximum - info.displayMinimum);
        return drawing::stringFormat("%+4.0f%%", value);
    }
    case Display::HertzLog: {
        const double value = info.displayMinimum * std::pow(
            info.displayMaximum / info.displayMinimum, normalized);
        if (value < 1.0)
            return drawing::stringFormat("%.2f Hz", value);
        if (value < 10.0)
            return drawing::stringFormat("%.1f Hz", value);
        return value < 1000.0
            ? drawing::stringFormat("%.0f Hz", value)
            : drawing::stringFormat("%.2f k", value * 0.001);
    }
    case Display::MillisecondsLog: {
        const double value = info.displayMinimum * std::pow(
            info.displayMaximum / info.displayMinimum, normalized);
        if (value < 1.0)
            return drawing::stringFormat("%.3f ms", value);
        if (value < 10.0)
            return drawing::stringFormat("%.2f ms", value);
        return drawing::stringFormat("%.0f ms", value);
    }
    case Display::MillisecondsSpacing: {
        const double value = normalized <= 0.001 ? 0.0
            : 2000.0 * std::pow((normalized - 0.001) / 0.999, 2.0);
        if (value < 10.0)
            return drawing::stringFormat("%.2f ms", value);
        return drawing::stringFormat("%.0f ms", value);
    }
    case Display::Count: {
        const double value = info.displayMinimum + normalized
            * (info.displayMaximum - info.displayMinimum);
        return drawing::stringFormat("%.0f", value);
    }
    case Display::Semitones: {
        const double value = info.displayMinimum + normalized
            * (info.displayMaximum - info.displayMinimum);
        return drawing::stringFormat("%+.1f st", value);
    }
    case Display::Decibels: {
        const double value = info.displayMinimum + normalized
            * (info.displayMaximum - info.displayMinimum);
        return drawing::stringFormat("%+.1f dB", value);
    }
    case Display::Menu: {
        const uint32_t item = info.menuCount > 1u
            ? static_cast<uint32_t>(std::lround(normalized
                * static_cast<double>(info.menuCount - 1u))) : 0u;
        return drawing::stringFromUtf8(s3g::feedbackPedalMenuItem(pedal, control, item));
    }
    case Display::Ratio: {
        const double value = info.displayMinimum + normalized
            * (info.displayMaximum - info.displayMinimum);
        return drawing::stringFormat("%.2fx", value);
    }
    }
    return "";
}

drawing::String  feedbackAuxControlText(
    const s3g::FeedbackPedalControlInfo& info,
    double normalized, uint32_t control)
{
    normalized = std::clamp(normalized, 0.0, 1.0);
    if (info.display == s3g::FeedbackPedalControlDisplay::Menu) {
        const uint32_t item = info.menuCount > 1u
            ? static_cast<uint32_t>(std::lround(normalized
                * static_cast<double>(info.menuCount - 1u))) : 0u;
        return drawing::stringFromUtf8(s3g::feedbackAuxMenuItem(control, item));
    }
    return feedbackPedalControlText(
        info, normalized, s3g::FeedbackPedalType::Bypass, control);
}

void drawFeedbackActionButton(drawing::Rect rect, drawing::String  title, bool active,
    const drawing::Style& style)
{
    drawing::setFill((active ? drawing::color(0x58675e) : style.strip));
    drawing::rectFill(rect);
    drawing::setStroke(style.grid);
    drawing::frameRect(rect);
    drawing::Attrs  attrs = drawing::softLabelAttrs();
    const drawing::Size size = drawing::sizeWithAttributes(title, attrs);
    drawing::drawAtPoint_withAttributes(title, drawing::makePoint(drawing::midX(rect) - size.width * 0.5,
        drawing::midY(rect) - size.height * 0.5), attrs);
}


class Editor final : public s3g::portable_gui::routing::Canvas {
public:
using Canvas = s3g::portable_gui::routing::Canvas;

    Plugin* _plugin;
    int _dragParam;
    double _dragPanelX;
    double _dragPanelWidth;
    uint32_t _selectedNode;
    bool _editingSceneB;
    bool _editingAux;
    uint32_t _selectedSource;
    uint32_t _selectedDestination;
    uint32_t _page;
    int _factoryPresetIndex;
    uint32_t _randomSeed;
    uint32_t _engineBank;
    char _titlePresetName[64];
    int _openMenu;
    uint32_t _openMenuControl;
    int _hoverMenuItem;
    drawing::Point _menuOrigin;
    double _menuWidth;
    void*  _timer;
#include "s3g_feedback_processor_editor.inc"
explicit Editor(Plugin& plugin) : Canvas(kGuiWidth,kGuiHeight) {_plugin = &plugin;
        _dragParam = -1;
        _dragPanelX = 0.0;
        _dragPanelWidth = 0.0;
        _selectedNode = 0u;
        _editingSceneB = false;
        _editingAux = false;
        _selectedSource = 0u;
        _selectedDestination = 0u;
        _page = kFeedbackPagePatch;
        _factoryPresetIndex = 0;
        _randomSeed = 0x53334642u;
        _engineBank = 1u;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            s3g::kFeedbackShiftPresetInfo[0u].name);
        _openMenu = kFeedbackMenuNone;
        _openMenuControl = 0u;
        _hoverMenuItem = -1;
        _menuOrigin = drawing::Point{};
        _menuWidth = 0.0;
}

void service() override {
    
    if (!isHidden() && _plugin
        && true) setNeedsDisplay(true);

}

uint32_t openMenuItemCount()  {
    switch (_openMenu) {
    case kFeedbackMenuPreset: return s3g::kFeedbackShiftPresetCount;
    case kFeedbackMenuNodeMode: return 2u;
    case kFeedbackMenuInsertCategory:
        return s3g::kFeedbackInsertCategoryCount;
    case kFeedbackMenuInsertEffect: {
        const auto pedal = static_cast<s3g::FeedbackPedalType>(
            static_cast<uint32_t>(paramValue(
                *static_cast<Plugin*>(_plugin),
                nodeParamId(_selectedNode, kNodePedalOffset))));
        return s3g::feedbackInsertEffectCount(
            s3g::feedbackInsertCategory(pedal));
    }
    case kFeedbackMenuExciterSource:
        return s3g::kFeedbackExciterSourceCount;
    case kFeedbackMenuMorphSource:
        return s3g::kFeedbackMorphSourceCount;
    case kFeedbackMenuOutputMode: return s3g::kFeedbackShiftOutputModeCount;
    case kFeedbackMenuRun:
    case kFeedbackMenuMorphHold:
    case kFeedbackMenuMorphSync: return 2u;
    case kFeedbackMenuMorphDivision:
        return static_cast<uint32_t>(s3g::kFeedbackPulseDivisionBeats.size());
    case kFeedbackMenuMorphShape: return s3g::kFeedbackPulseShapeCount;
    default:
        if (_openMenu >= kFeedbackMenuEffectBase) {
            const auto pedal = static_cast<s3g::FeedbackPedalType>(
                static_cast<uint32_t>(paramValue(
                    *static_cast<Plugin*>(_plugin),
                    nodeParamId(_selectedNode, kNodePedalOffset))));
            return (_editingAux
                    ? s3g::feedbackAuxControlInfo(_openMenuControl)
                    : s3g::feedbackPedalControlInfo(
                        pedal, _openMenuControl)).menuCount;
        }
        return 0u;
    }

}

drawing::String  menuItemText(uint32_t item)  {
    switch (_openMenu) {
    case kFeedbackMenuPreset:
        return drawing::stringFromUtf8(s3g::kFeedbackShiftPresetInfo[
                std::min(item, s3g::kFeedbackShiftPresetCount - 1u)].name);
    case kFeedbackMenuNodeMode: return item == 0u ? "SHIFT" : "RING";
    case kFeedbackMenuInsertCategory:
        return drawing::stringFromUtf8(s3g::feedbackInsertCategoryName(
                static_cast<s3g::FeedbackInsertCategory>(std::min(item,
                    s3g::kFeedbackInsertCategoryCount - 1u))));
    case kFeedbackMenuInsertEffect: {
        const auto pedal = static_cast<s3g::FeedbackPedalType>(
            static_cast<uint32_t>(paramValue(
                *static_cast<Plugin*>(_plugin),
                nodeParamId(_selectedNode, kNodePedalOffset))));
        return drawing::stringFromUtf8(s3g::feedbackInsertEffectName(
            s3g::feedbackInsertCategory(pedal), item));
    }
    case kFeedbackMenuExciterSource:
        return drawing::stringFromUtf8(s3g::feedbackExciterSourceName(
            static_cast<s3g::FeedbackExciterSource>(std::min(item,
                s3g::kFeedbackExciterSourceCount - 1u))));
    case kFeedbackMenuMorphSource:
        return drawing::stringFromUtf8(s3g::feedbackMorphSourceName(
            static_cast<s3g::FeedbackMorphSource>(std::min(item,
                s3g::kFeedbackMorphSourceCount - 1u))));
    case kFeedbackMenuOutputMode:
        return drawing::stringFromUtf8(s3g::feedbackShiftOutputModeName(
            static_cast<s3g::FeedbackShiftOutputMode>(std::min(item,
                s3g::kFeedbackShiftOutputModeCount - 1u))));
    case kFeedbackMenuRun:
    case kFeedbackMenuMorphHold:
    case kFeedbackMenuMorphSync: return item == 0u ? "OFF" : "ON";
    case kFeedbackMenuMorphDivision:
        return drawing::stringFromUtf8(s3g::feedbackPulseDivisionName(item));
    case kFeedbackMenuMorphShape:
        return drawing::stringFromUtf8(s3g::feedbackPulseShapeName(
            static_cast<s3g::FeedbackPulseShape>(std::min(item,
                s3g::kFeedbackPulseShapeCount - 1u))));
    default:
        if (_openMenu >= kFeedbackMenuEffectBase) {
            if (_editingAux) {
                return drawing::stringFromUtf8(s3g::feedbackAuxMenuItem(_openMenuControl, item));
            }
            const auto pedal = static_cast<s3g::FeedbackPedalType>(
                static_cast<uint32_t>(paramValue(
                    *static_cast<Plugin*>(_plugin),
                    nodeParamId(_selectedNode, kNodePedalOffset))));
            return drawing::stringFromUtf8(s3g::feedbackPedalMenuItem(pedal, _openMenuControl, item));
        }
        return "";
    }

}

int selectedMenuItem()  {
    auto* plugin = static_cast<Plugin*>(_plugin);
    switch (_openMenu) {
    case kFeedbackMenuPreset: return _factoryPresetIndex;
    case kFeedbackMenuNodeMode: return static_cast<int>(paramValue(*plugin,
        nodeParamId(_selectedNode, kNodeModeOffset)));
    case kFeedbackMenuInsertCategory: {
        const auto pedal = static_cast<s3g::FeedbackPedalType>(
            static_cast<uint32_t>(paramValue(*plugin,
                nodeParamId(_selectedNode, kNodePedalOffset))));
        return static_cast<int>(s3g::feedbackInsertCategory(pedal));
    }
    case kFeedbackMenuInsertEffect: {
        const auto pedal = static_cast<s3g::FeedbackPedalType>(
            static_cast<uint32_t>(paramValue(*plugin,
                nodeParamId(_selectedNode, kNodePedalOffset))));
        const uint32_t fractureProcessor = static_cast<uint32_t>(std::lround(
            feedbackNormalizedParam(*plugin,
                feedbackPedalStorageParamId(_selectedNode, 6u))
            * static_cast<double>(s3g::kFractureProcessorCount - 1u)));
        return static_cast<int>(s3g::feedbackInsertEffectIndex(
            pedal, fractureProcessor));
    }
    case kFeedbackMenuExciterSource:
        return static_cast<int>(paramValue(*plugin,
            nodeParamId(_selectedNode, kNodeExciterSourceOffset)));
    case kFeedbackMenuMorphSource:
        return static_cast<int>(paramValue(*plugin, kMorphSourceParamId));
    case kFeedbackMenuOutputMode:
        return static_cast<int>(paramValue(*plugin, kOutputModeParamId));
    case kFeedbackMenuRun:
        return static_cast<int>(paramValue(*plugin, kRunParamId));
    case kFeedbackMenuMorphHold:
        return static_cast<int>(paramValue(*plugin, kMorphHoldParamId));
    case kFeedbackMenuMorphSync:
        return static_cast<int>(paramValue(*plugin, kMorphSyncParamId));
    case kFeedbackMenuMorphDivision:
        return static_cast<int>(paramValue(*plugin, kMorphDivisionParamId));
    case kFeedbackMenuMorphShape:
        return static_cast<int>(paramValue(*plugin, kMorphShapeParamId));
    default:
        if (_openMenu >= kFeedbackMenuEffectBase) {
            const auto pedal = static_cast<s3g::FeedbackPedalType>(
                static_cast<uint32_t>(paramValue(*plugin,
                    nodeParamId(_selectedNode, kNodePedalOffset))));
            const auto info = _editingAux
                ? s3g::feedbackAuxControlInfo(_openMenuControl)
                : s3g::feedbackPedalControlInfo(
                    pedal, _openMenuControl);
            const clap_id id = _editingAux
                ? auxParamId(info.storageSlot)
                : feedbackPedalStorageParamId(
                    _selectedNode, info.storageSlot);
            const double normalized = feedbackNormalizedParam(*plugin, id);
            return info.menuCount > 1u ? static_cast<int>(std::lround(
                normalized * static_cast<double>(info.menuCount - 1u))) : 0;
        }
        return -1;
    }

}

drawing::Rect openMenuRect()  {
    return drawing::makeRect(_menuOrigin.x, _menuOrigin.y, _menuWidth,
        kFeedbackMenuItemHeight
            * static_cast<double>(openMenuItemCount()));

}

void openMenu_control_rect(int menu, uint32_t control, drawing::Rect rect)  {
    _openMenu = menu;
    _openMenuControl = control;
    _hoverMenuItem = -1;
    _menuWidth = rect.size.width;
    const double height = kFeedbackMenuItemHeight
        * static_cast<double>(openMenuItemCount());
    const double below = drawing::maxY(rect) + 3.0;
    const double y = below + height <= kGuiHeight - 8.0
        ? below : rect.origin.y - height - 3.0;
    _menuOrigin = drawing::makePoint(rect.origin.x, std::max<double>(8.0, y));
    setNeedsDisplay(true);

}

void updateMenuHover(drawing::Point point)  {
    const uint32_t count = openMenuItemCount();
    if (count == 0u) return;
    const int hover = drawing::dropdownHitIndex(
        point, openMenuRect(), kFeedbackMenuItemHeight, count);
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        setNeedsDisplay(true);
    }

}

void drawPanel_panel_style_labels(drawing::String  title, const s3g::gui_layout::Panel& panel, const drawing::Style& style, drawing::Attrs  labels)  {
    drawing::drawPanelFrame(panel.frame.x, panel.frame.y,
        panel.frame.width, panel.frame.height, style);
    drawing::drawPanelHeader(title, true, panel.frame.x, panel.frame.y,
        panel.frame.width, s3g::gui_layout::kStandardMetrics.headerHeight,
        labels, style);

}

void drawSlider_id_row_panel_style_labels_values(drawing::String  label, clap_id id, uint32_t row, const s3g::gui_layout::Panel& panel, const drawing::Style& style, drawing::Attrs  labels, drawing::Attrs  values)  {
    drawing::drawProcessorSlider(label,
        feedbackParamText(*static_cast<Plugin*>(_plugin), id),
        feedbackNormalizedParam(*static_cast<Plugin*>(_plugin), id),
        s3g::gui_layout::rowY(panel, row), panel.frame.x, panel.frame.width,
        labels, values, style);

}

void drawMenu_id_row_panel_style_labels_values(drawing::String  label, clap_id id, uint32_t row, const s3g::gui_layout::Panel& panel, const drawing::Style& style, drawing::Attrs  labels, drawing::Attrs  values)  {
    drawing::drawProcessorMenu(label,
        feedbackParamText(*static_cast<Plugin*>(_plugin), id),
        s3g::gui_layout::rowY(panel, row), panel.frame.x, panel.frame.width,
        labels, values, style);

}

void drawOriginal(drawing::Rect dirty)  {
    (void)dirty;
    auto* plugin = static_cast<Plugin*>(_plugin);
    drawing::Style style;
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs  labels = drawing::softLabelAttrs();
    drawing::Attrs  values = drawing::softValueAttrs();
    float peak = 0.0f;
    for (const auto& meter : plugin->outputPeaks) {
        peak = std::max(peak, meter.load(std::memory_order_relaxed));
    }
    const auto outputMode = static_cast<s3g::FeedbackShiftOutputMode>(
        static_cast<uint32_t>(paramValue(*plugin, kOutputModeParamId)));
    const float liveMorph = plugin->morphValue.load(std::memory_order_relaxed);
    drawing::String  status = drawing::stringFormat("%s  //  A %.0f%%  B %.0f%%  //  %@",
        s3g::feedbackShiftOutputModeName(outputMode),
        (1.0f - liveMorph) * 100.0f, liveMorph * 100.0f,
        drawing::peakDbText(peak));
    const auto titleBand = s3g::gui_layout::matrixTitleBand(kFeedbackCanvas);
    drawing::drawProcessorTitleBand(
        "s3g PROCESSOR FEEDBACK SHIFT 8CH",
        drawing::stringFromUtf8(_titlePresetName), status,
        titleBand, drawing::softTitleAttrs(), labels, values, style);
    drawing::setFill(style.strip);
    drawing::rectFill(kFeedbackPageBand);
    drawing::setStroke(style.grid);
    drawing::frameRect(kFeedbackPageBand);
    static drawing::String  pageNames[kFeedbackPageCount] = {
        "PATCH", "INSERT", "ECOLOGY", "AUX",
    };
    for (uint32_t page = 0u; page < kFeedbackPageCount; ++page) {
        drawFeedbackActionButton(feedbackPageButtonRect(page),
            pageNames[page], _page == page, style);
    }
    drawing::drawAtPoint_withAttributes("EDIT SCENE", drawing::makePoint(632.0, 48.0), labels);
    drawFeedbackActionButton(feedbackSceneButtonRect(false), "A",
        !_editingSceneB, style);
    drawFeedbackActionButton(feedbackSceneButtonRect(true), "B",
        _editingSceneB, style);
    drawFeedbackActionButton(feedbackRandomButtonRect(false), "RANDOM A",
        false, style);
    drawFeedbackActionButton(feedbackRandomButtonRect(true), "RANDOM B",
        false, style);

    const auto pedal = static_cast<s3g::FeedbackPedalType>(
        static_cast<uint32_t>(paramValue(*plugin,
            nodeParamId(_selectedNode, kNodePedalOffset))));
    const auto fractureProcessor = static_cast<s3g::FractureProcessor>(
        std::min<uint32_t>(s3g::kFractureProcessorCount - 1u,
            static_cast<uint32_t>(std::lround(
                feedbackNormalizedParam(*plugin,
                    feedbackPedalStorageParamId(_selectedNode, 6u))
                * static_cast<double>(
                    s3g::kFractureProcessorCount - 1u)))));
    const auto shredCircuit = static_cast<s3g::MacroShredCircuit>(
        std::min<uint32_t>(s3g::kMacroShredCircuitCount - 1u,
            static_cast<uint32_t>(std::lround(
                feedbackNormalizedParam(*plugin,
                    feedbackPedalStorageParamId(_selectedNode, 11u))
                * static_cast<double>(
                    s3g::kMacroShredCircuitCount - 1u)))));
    const uint32_t effectRows = _editingAux
        ? s3g::kFeedbackAuxControlCount
        : feedbackVisiblePedalControlCount(pedal);
    const auto pedalPanel = feedbackEffectPanel(effectRows);
    const bool temporalPedal = !_editingAux
        && (pedal == s3g::FeedbackPedalType::Repeater
            || pedal == s3g::FeedbackPedalType::TimeMangler);
    const uint32_t temporalPhase = plugin->temporalPhases[_selectedNode].load(
        std::memory_order_relaxed);
    drawing::String  temporalState = "LISTEN";
    if (temporalPhase == 1u) temporalState = "CAPTURE";
    else if (temporalPhase == 2u) {
        if (pedal == s3g::FeedbackPedalType::Repeater) {
            temporalState = "REPEAT";
        } else {
            const auto modeInfo = s3g::feedbackPedalControlInfo(pedal, 3u);
            const double modeNorm = feedbackNormalizedParam(*plugin,
                feedbackPedalStorageParamId(
                    _selectedNode, modeInfo.storageSlot));
            const uint32_t mode = static_cast<uint32_t>(std::lround(
                modeNorm * static_cast<double>(modeInfo.menuCount - 1u)));
            temporalState = drawing::stringFromUtf8(s3g::feedbackPedalMenuItem(pedal, 3u, mode));
        }
    }
    const clap_id selectedRouteId = matrixParamId(_editingSceneB,
        _selectedDestination, _selectedSource);
    if (_page == kFeedbackPagePatch) {
        drawPanel_panel_style_labels(drawing::stringFormat("SCENE %c MATRIX // SOURCE > DESTINATION",
                _editingSceneB ? 'B' : 'A'), kMatrixPanel, style, labels);
        drawPanel_panel_style_labels("SCENE OPERATIONS", kPatchScenePanel, style, labels);
        drawPanel_panel_style_labels(drawing::stringFormat("CROSSPOINT // S%u > D%u", _selectedSource + 1u,
                _selectedDestination + 1u), kCrosspointPanel, style, labels);
    } else if (_page == kFeedbackPageInsert) {
        drawPanel_panel_style_labels("NODE INSERTS // SELECT + RANDOMIZE", kNodeStripPanel, style, labels);
        drawPanel_panel_style_labels(drawing::stringFormat("SCENE %c // NODE %u CORE", _editingSceneB ? 'B' : 'A',
                _selectedNode + 1u), kNodePanel, style, labels);
        drawPanel_panel_style_labels("INSERT SELECTION", kInsertSelectorPanel, style, labels);
        drawPanel_panel_style_labels((pedal == s3g::FeedbackPedalType::Fracture
                ? drawing::stringFormat("INSERT // FRACTURE // %s",
                    s3g::fractureProcessorName(fractureProcessor))
                : pedal == s3g::FeedbackPedalType::MacroShred
                ? drawing::stringFormat("INSERT // SHRED // %s",
                    s3g::macroShredCircuitName(shredCircuit))
                : temporalPedal
                ? drawing::stringFormat("INSERT // %s // %@",
                    s3g::feedbackPedalName(pedal), temporalState)
                : drawing::stringFormat("INSERT // %s",
                    s3g::feedbackPedalName(pedal))), pedalPanel, style, labels);
    } else if (_page == kFeedbackPageAux) {
        drawPanel_panel_style_labels(drawing::stringFormat("SCENE %c AUX SENDS // NODE > BUS",
                _editingSceneB ? 'B' : 'A'), kAuxSendPanel, style, labels);
        drawPanel_panel_style_labels(drawing::stringFormat("FEEDBACK AUX // %.1f dB GR",
                plugin->auxGainReductionDb.load(std::memory_order_relaxed)), kAuxFeedbackPanel, style, labels);
        drawPanel_panel_style_labels(drawing::stringFormat("POST GRANULATOR // %.1f GRAINS",
                plugin->auxGrainActivity.load(
                    std::memory_order_relaxed) * 10.0f), kPostGrainPanel, style, labels);
    }
    const double restActivity = plugin->restActivity.load(
        std::memory_order_relaxed);
    const uint32_t restingLanes = plugin->restingLaneCount.load(
        std::memory_order_relaxed);
    const double spliceEventsPerSecond =
        s3g::feedbackSpliceRateHz(static_cast<float>(
            paramValue(*plugin, kSpliceRateParamId)), static_cast<float>(
            paramValue(*plugin, kSpliceRateFineParamId)))
        * s3g::kFeedbackShiftChannels;
    drawing::String  spliceTitle = spliceEventsPerSecond < 1.0
        ? drawing::stringFormat("SPLICE // %.1f/MIN",
            spliceEventsPerSecond * 60.0)
        : spliceEventsPerSecond < 10.0
        ? drawing::stringFormat("SPLICE // %.1f/S",
            spliceEventsPerSecond)
        : drawing::stringFormat("SPLICE // %.0f/S",
            spliceEventsPerSecond);
    drawing::String  engineTitle = _engineBank == 2u
        ? drawing::stringFormat("SUB // %.1f HZ",
            s3g::feedbackSubBassFrequencyHz(static_cast<float>(
                paramValue(*plugin, kSubBassTuneParamId))))
        : _engineBank == 1u
        ? spliceTitle
        : restingLanes > 0u
        ? drawing::stringFormat("ENGINE // %u LANE%s REST",
            restingLanes, restingLanes == 1u ? "" : "S")
        : restActivity > 0.01f
            ? drawing::stringFormat("ENGINE // REST ENTER %.0f%%",
                restActivity * 100.0f)
            : "ENGINE";
    if (_page == kFeedbackPageEcology) {
        drawPanel_panel_style_labels(engineTitle, kExcitationPanel, style, labels);
        drawFeedbackActionButton(feedbackEngineBankButtonRect(0u), "SRC",
            _engineBank == 0u, style);
        drawFeedbackActionButton(feedbackEngineBankButtonRect(1u), "SPL",
            _engineBank == 1u, style);
        drawFeedbackActionButton(feedbackEngineBankButtonRect(2u), "SUB",
            _engineBank == 2u, style);
        drawPanel_panel_style_labels("ECOLOGY MORPH // A > B", kMorphPanel, style, labels);
        drawPanel_panel_style_labels("OUTPUT TOPOLOGY", kOutputPanel, style, labels);
        drawPanel_panel_style_labels("NODE ECOLOGY // SELECTED LANE ACTIVITY", kEcologyActivityPanel, style, labels);
    }
    if (_page == kFeedbackPageInsert
        && temporalPedal && temporalPhase != 0u) {
        drawing::Rect temporalBar = drawing::makeRect(pedalPanel.frame.x + 1.0,
            pedalPanel.frame.y + s3g::gui_layout::kStandardMetrics.headerHeight
                - 3.0,
            (pedalPanel.frame.width - 2.0)
                * plugin->temporalProgress[_selectedNode].load(
                    std::memory_order_relaxed), 2.0);
        drawing::setFill((temporalPhase == 1u ? drawing::color(0xb28a72)
                             : drawing::color(0x86a48c)));
        drawing::rectFill(temporalBar);
    }

    if (_page == kFeedbackPagePatch) {
    drawing::drawAtPoint_withAttributes("SOURCE", drawing::makePoint(304.0, 108.0), labels);
    drawing::drawAtPoint_withAttributes("DEST", drawing::makePoint(68.0, 322.0), labels);
    for (uint32_t index = 0u; index < s3g::kFeedbackShiftChannels; ++index) {
        drawing::String  number = drawing::stringFormat("%u", index + 1u);
        drawing::drawAtPoint_withAttributes(number, drawing::makePoint(kFeedbackMatrixX
            + static_cast<double>(index) * kFeedbackMatrixCell + 13.0,
            122.0), values);
        drawing::drawAtPoint_withAttributes(number, drawing::makePoint(116.0, kFeedbackMatrixY
            + static_cast<double>(index) * kFeedbackMatrixCell + 8.0), values);
    }
    drawing::setStroke(style.grid);
    const double matrixFirstCenter = kFeedbackMatrixX + 17.0;
    const double matrixLastCenter = matrixFirstCenter
        + kFeedbackMatrixCell * 7.0;
    for (uint32_t index = 0u; index < s3g::kFeedbackShiftChannels; ++index) {
        const double x = matrixFirstCenter
            + static_cast<double>(index) * kFeedbackMatrixCell;
        const double y = kFeedbackMatrixY + 17.0
            + static_cast<double>(index) * kFeedbackMatrixCell;
        drawing::Path::strokeLineFromPoint_toPoint(drawing::makePoint(x, kFeedbackMatrixY + 17.0), drawing::makePoint(x, matrixLastCenter));
        drawing::Path::strokeLineFromPoint_toPoint(drawing::makePoint(matrixFirstCenter, y), drawing::makePoint(matrixLastCenter, y));
    }
    for (uint32_t destination = 0u;
         destination < s3g::kFeedbackShiftChannels; ++destination) {
        for (uint32_t source = 0u;
             source < s3g::kFeedbackShiftChannels; ++source) {
            const uint32_t index = destination
                * s3g::kFeedbackShiftChannels + source;
            const double route = paramValue(*plugin,
                matrixParamId(_editingSceneB, destination, source));
            const float activity = plugin->routeActivity[index].load(
                std::memory_order_relaxed);
            const drawing::Rect cell = feedbackMatrixCellRect(destination, source);
            const bool active = std::abs(route) > 0.001;
            const bool selected = source == _selectedSource
                && destination == _selectedDestination;
            const uint32_t routeBase = route >= 0.0 ? 0xc95e3b : 0x5daeb6;
            const uint32_t routeLive = route >= 0.0 ? 0xff7047 : 0x69d2dc;
            const drawing::Rect node = drawing::makeRect(drawing::midX(cell) - 8.0,
                drawing::midY(cell) - 8.0, 16.0, 16.0);
            drawing::setFill(drawing::color(0x141414));
            drawing::rectFill(node);
            if (active) {
                drawing::setFill(drawing::withAlpha(drawing::color(routeBase), 0.58));
                drawing::rectFill(drawing::inset(node, 1.0, 1.0));
                const double effective = std::clamp<double>(
                    std::abs(route) * (0.72 + activity * 0.58), 0.0, 1.0);
                const double liveSize = 3.0 + effective * 11.0;
                drawing::setFill(drawing::withAlpha(drawing::color(routeLive), 0.56 + activity * 0.40));
                drawing::rectFill(drawing::makeRect(drawing::midX(node) - liveSize * 0.5,
                    drawing::midY(node) - liveSize * 0.5, liveSize, liveSize));
            }
            drawing::setStroke((selected ? drawing::color(0xc8c8c8)
                       : drawing::color(0x5a5a5a)));
            drawing::frameRect(node);
        }
    }
    drawing::drawAtPoint_withAttributes("CLICK: PATCH / SELECT · CLICK AGAIN: DISSOLVE · OPTION: NEGATIVE", drawing::makePoint(kMatrixPanel.frame.x + 18.0,
            kMatrixPanel.frame.y + kMatrixPanel.frame.height - 26.0), values);
    const double selectedRoute = paramValue(*plugin, selectedRouteId);
    drawing::drawProcessorSlider("GAIN",
        drawing::stringFormat("%+.3f", selectedRoute),
        (selectedRoute + 1.0) * 0.5,
        s3g::gui_layout::rowY(kCrosspointPanel, 0u),
        kCrosspointPanel.frame.x, kCrosspointPanel.frame.width,
        labels, values, style);
    drawFeedbackActionButton(feedbackCrosspointActionRect(0u),
        selectedRoute < 0.0 ? "NEGATIVE" : "POSITIVE",
        std::abs(selectedRoute) > 0.001, style);
    drawFeedbackActionButton(feedbackCrosspointActionRect(1u),
        "CLEAR", false, style);
    const double selectedActivity = plugin->routeActivity[
        _selectedDestination * s3g::kFeedbackShiftChannels
            + _selectedSource].load(std::memory_order_relaxed);
    drawing::drawAtPoint_withAttributes("POLARITY", drawing::makePoint(686.0, 420.0), labels);
    drawing::setFill(drawing::color(0xc95e3b));
    drawing::rectFill(drawing::makeRect(772.0, 421.0, 12.0, 12.0));
    drawing::drawAtPoint_withAttributes("POSITIVE", drawing::makePoint(792.0, 419.0), values);
    drawing::setFill(drawing::color(0x5daeb6));
    drawing::rectFill(drawing::makeRect(886.0, 421.0, 12.0, 12.0));
    drawing::drawAtPoint_withAttributes("NEGATIVE", drawing::makePoint(906.0, 419.0), values);
    drawing::drawAtPoint_withAttributes("LIVE ROUTE", drawing::makePoint(686.0, 462.0), labels);
    const drawing::Rect routeMeter = drawing::makeRect(772.0, 464.0, 276.0, 10.0);
    drawing::setFill(drawing::color(0x080808)); drawing::rectFill(routeMeter);
    drawing::setStroke(style.grid); drawing::frameRect(routeMeter);
    drawing::setFill((selectedRoute >= 0.0 ? drawing::color(0xff7047)
                           : drawing::color(0x69d2dc)));
    drawing::Rect routeFill = drawing::inset(routeMeter, 1.0, 1.0);
    routeFill.size.width *= std::clamp<double>(
        std::abs(selectedRoute) * (0.6 + selectedActivity * 0.6), 0.0, 1.0);
    drawing::rectFill(routeFill);
    drawing::drawAtPoint_withAttributes("RANDOM A/B mutates one scene only. COPY commits the authored scene.", drawing::makePoint(686.0, 108.0), values);
    drawFeedbackActionButton(feedbackSceneCopyButtonRect(true),
        "COPY A > B", false, style);
    drawFeedbackActionButton(feedbackSceneCopyButtonRect(false),
        "COPY B > A", false, style);
    drawing::drawAtPoint_withAttributes("Orange = positive gain · Cyan = inverted gain", drawing::makePoint(686.0, 214.0), values);
    }

    if (_page == kFeedbackPageInsert
        || _page == kFeedbackPageEcology) {
    for (uint32_t node = 0u; node < s3g::kFeedbackShiftChannels; ++node) {
        const bool selected = node == _selectedNode;
        drawFeedbackActionButton(feedbackNodeButtonRect(_page, node),
            drawing::stringFormat("N%u", node + 1u), selected, style);
        if (_page == kFeedbackPageInsert) {
            drawFeedbackActionButton(feedbackNodeRandomButtonRect(node),
                "RND", false, style);
        }
        const double laneRest = plugin->restLaneActivities[node].load(
            std::memory_order_relaxed);
        if (laneRest > 0.005f) {
            drawing::Rect restMeter = feedbackNodeButtonRect(_page, node);
            restMeter.origin.y += 1.0;
            restMeter.size.height = 3.0;
            restMeter.size.width = std::max<double>(1.0,
                restMeter.size.width * laneRest);
            drawing::setFill(drawing::color(0xb28a72));
            drawing::rectFill(restMeter);
        }
        const double laneEnvelope = plugin->laneEcologyEnvelopes[node].load(
            std::memory_order_relaxed);
        const double laneEdge = plugin->laneEcologyEdges[node].load(
            std::memory_order_relaxed);
        const double activity = std::max<double>(
            static_cast<double>(plugin->nodeActivity[node].load(std::memory_order_relaxed)),
            laneEnvelope);
        if (activity > 0.001) {
            drawing::Rect meter = feedbackNodeButtonRect(_page, node);
            meter.origin.y = drawing::maxY(meter) - 2.0;
            meter.size.height = 2.0;
            meter.size.width *= std::min<double>(1.0, activity);
            drawing::setFill(drawing::color(0x86a48c));
            drawing::rectFill(meter);
        }
        if (laneEdge > 0.015f) {
            const drawing::Rect nodeRect = feedbackNodeButtonRect(_page, node);
            const double diameter = 2.0f + laneEdge * 4.0f;
            drawing::setFill(drawing::color(0xc9b86f));
            drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
                drawing::maxX(nodeRect) - diameter - 3.0,
                drawing::midY(nodeRect) - diameter * 0.5,
                diameter, diameter)).fill();
        }
        const double splicePulse = plugin->spliceLaneActivities[node].load(
            std::memory_order_relaxed);
        if (splicePulse > 0.01f) {
            const drawing::Rect nodeRect = feedbackNodeButtonRect(_page, node);
            drawing::setFill(drawing::color(0x71a6a0));
            drawing::rectFill(drawing::makeRect(drawing::maxX(nodeRect) - 2.0,
                nodeRect.origin.y + 2.0, 2.0,
                (nodeRect.size.height - 4.0) * splicePulse));
        }
    }
    }
    const double auxActivity = plugin->auxActivity.load(
        std::memory_order_relaxed);
    if (_page == kFeedbackPageAux && auxActivity > 0.001f) {
        drawing::Rect meter = drawing::makeRect(kAuxFeedbackPanel.frame.x + 1.0,
            kAuxFeedbackPanel.frame.y
                + s3g::gui_layout::kStandardMetrics.headerHeight - 3.0,
            (kAuxFeedbackPanel.frame.width - 2.0)
                * std::min<double>(1.0, auxActivity * 2.0), 2.0);
        drawing::setFill(drawing::color(0xff7047));
        drawing::rectFill(meter);
    }
    if (_page == kFeedbackPageAux) {
        drawing::setFill(style.grid);
        drawing::rectFill(drawing::makeRect(kAuxSendRightPanel.frame.x,
            kAuxSendPanel.frame.y
                + s3g::gui_layout::kStandardMetrics.headerHeight,
            1.0, kAuxSendPanel.frame.height
                - s3g::gui_layout::kStandardMetrics.headerHeight));
        for (uint32_t node = 0u;
             node < s3g::kFeedbackShiftChannels; ++node) {
            const auto& panel = node < 4u
                ? kAuxSendLeftPanel : kAuxSendRightPanel;
            drawSlider_id_row_panel_style_labels_values(drawing::stringFormat("NODE %u", node + 1u), sceneParamId(_editingSceneB, node, kSceneAuxSendOffset), node % 4u, panel, style, labels, values);
        }
    } else if (_page == kFeedbackPageInsert) {
        drawMenu_id_row_panel_style_labels_values("MODE", nodeParamId(_selectedNode, kNodeModeOffset), 0u, kNodePanel, style, labels, values);
        const auto insertCategory = s3g::feedbackInsertCategory(pedal);
        const uint32_t insertEffect = s3g::feedbackInsertEffectIndex(pedal,
            static_cast<uint32_t>(fractureProcessor),
            static_cast<uint32_t>(shredCircuit));
        drawing::drawProcessorMenu("CATEGORY",
            drawing::stringFromUtf8(s3g::feedbackInsertCategoryName(insertCategory)),
            s3g::gui_layout::rowY(kInsertSelectorPanel, 0u),
            kInsertSelectorPanel.frame.x, kInsertSelectorPanel.frame.width,
            labels, values, style);
        drawing::drawProcessorMenu("EFFECT",
            drawing::stringFromUtf8(s3g::feedbackInsertEffectName(insertCategory, insertEffect)),
            s3g::gui_layout::rowY(kInsertSelectorPanel, 1u),
            kInsertSelectorPanel.frame.x, kInsertSelectorPanel.frame.width,
            labels, values, style);
        drawMenu_id_row_panel_style_labels_values("SOURCE", nodeParamId(_selectedNode,
            kNodeExciterSourceOffset), 1u, kNodePanel, style, labels, values);
        drawSlider_id_row_panel_style_labels_values("INPUT", nodeParamId(_selectedNode,
            kNodeExciterGainOffset), 2u, kNodePanel, style, labels, values);
        drawSlider_id_row_panel_style_labels_values("SHIFT", sceneParamId(_editingSceneB,
            _selectedNode, kSceneFrequencyOffset), 3u, kNodePanel, style, labels, values);
        drawSlider_id_row_panel_style_labels_values("REGEN", sceneParamId(_editingSceneB,
            _selectedNode, kSceneRegenerationOffset), 4u, kNodePanel, style, labels, values);
        drawSlider_id_row_panel_style_labels_values("COLOR", sceneParamId(_editingSceneB,
            _selectedNode, kSceneColorOffset), 5u, kNodePanel, style, labels, values);
        drawSlider_id_row_panel_style_labels_values("BODY", sceneParamId(_editingSceneB,
            _selectedNode, kSceneBodyOffset), 6u, kNodePanel, style, labels, values);
        drawSlider_id_row_panel_style_labels_values("LEVEL", sceneParamId(_editingSceneB,
            _selectedNode, kSceneLevelOffset), 7u, kNodePanel, style, labels, values);
        drawSlider_id_row_panel_style_labels_values("AUX SEND", sceneParamId(_editingSceneB,
            _selectedNode, kSceneAuxSendOffset), 8u, kNodePanel, style, labels, values);
    }

    if (_page == kFeedbackPageInsert || _page == kFeedbackPageAux) {
    if (effectRows == 0u) {
        drawing::drawAtPoint_withAttributes("NO INSERT // SHIFT/RING REMAINS ACTIVE", drawing::makePoint(pedalPanel.frame.x + 16.0,
                pedalPanel.frame.y + 37.0), values);
    }
    for (uint32_t row = 0u; row < effectRows; ++row) {
        const uint32_t control = _editingAux ? row
            : feedbackVisiblePedalControlIndex(pedal, row);
        auto info = _editingAux
            ? s3g::feedbackAuxControlInfo(row)
            : s3g::feedbackPedalControlInfo(pedal, control);
        if (!_editingAux && pedal == s3g::FeedbackPedalType::Fracture) {
            if (control == 1u) info.label = s3g::fractureAmountLabel(
                fractureProcessor);
            else if (control == 2u) info.label = s3g::fractureColorLabel(
                fractureProcessor);
            else if (control == 3u) info.label = s3g::fractureBiasLabel(
                fractureProcessor);
        }
        const clap_id id = _editingAux ? auxParamId(info.storageSlot)
            : feedbackPedalStorageParamId(_selectedNode, info.storageSlot);
        const double normalized = feedbackNormalizedParam(*plugin, id);
        drawing::String  value = _editingAux
            ? feedbackAuxControlText(info, normalized, row)
            : feedbackPedalControlText(info, normalized, pedal, control);
        const auto& controlPanel = _editingAux
            ? (row < 6u ? kAuxFeedbackPanel : kPostGrainPanel)
            : pedalPanel;
        const uint32_t controlRow = _editingAux && row >= 6u
            ? row - 6u : row;
        if (info.display == s3g::FeedbackPedalControlDisplay::Menu) {
            drawing::drawProcessorMenu(
                drawing::stringFromUtf8(info.label), value,
                s3g::gui_layout::rowY(controlPanel, controlRow),
                controlPanel.frame.x, controlPanel.frame.width,
                labels, values, style);
        } else {
            drawing::drawProcessorSlider(
                drawing::stringFromUtf8(info.label), value, normalized,
                s3g::gui_layout::rowY(controlPanel, controlRow),
                controlPanel.frame.x, controlPanel.frame.width,
                labels, values, style);
        }
    }
    }

    if (_page == kFeedbackPageEcology) {
    if (_engineBank == 0u) {
        drawSlider_id_row_panel_style_labels_values("INT NOISE", kExciteParamId, 0u, kExcitationPanel, style, labels, values);
        drawSlider_id_row_panel_style_labels_values("DRIFT", kDriftParamId, 1u, kExcitationPanel, style, labels, values);
        drawMenu_id_row_panel_style_labels_values("ENGINE RUN", kRunParamId, 2u, kExcitationPanel, style, labels, values);
        drawSlider_id_row_panel_style_labels_values("REST", kGovernorRestParamId, 3u, kExcitationPanel, style, labels, values);
        drawFeedbackActionButton(feedbackActionButtonRect(0u),
            drawing::stringFormat("HIT N%u", _selectedNode + 1u),
            false, style);
        drawFeedbackActionButton(feedbackActionButtonRect(1u), "PANIC",
            false, style);
    } else if (_engineBank == 1u) {
        drawSlider_id_row_panel_style_labels_values("SPLICE", kSpliceAmountParamId, 0u, kExcitationPanel, style, labels, values);
        drawSlider_id_row_panel_style_labels_values("RANGE", kSpliceRateParamId, 1u, kExcitationPanel, style, labels, values);
        drawSlider_id_row_panel_style_labels_values("FINE", kSpliceRateFineParamId, 2u, kExcitationPanel, style, labels, values);
        drawSlider_id_row_panel_style_labels_values("CONTRAST", kSpliceContrastParamId, 3u, kExcitationPanel, style, labels, values);
        drawSlider_id_row_panel_style_labels_values("SPACE", kSpliceSpaceParamId, 4u, kExcitationPanel, style, labels, values);
    } else {
        drawSlider_id_row_panel_style_labels_values("TUNE", kSubBassTuneParamId, 0u, kExcitationPanel, style, labels, values);
        drawSlider_id_row_panel_style_labels_values("SHAPE", kSubBassShapeParamId, 1u, kExcitationPanel, style, labels, values);
        drawSlider_id_row_panel_style_labels_values("DRIVE", kSubBassDriveParamId, 2u, kExcitationPanel, style, labels, values);
        drawSlider_id_row_panel_style_labels_values("DECAY", kSubBassDecayParamId, 3u, kExcitationPanel, style, labels, values);
        drawSlider_id_row_panel_style_labels_values("SUSTAIN", kSubBassSustainParamId, 4u, kExcitationPanel, style, labels, values);
    }

    drawing::setFill(style.grid);
    drawing::rectFill(drawing::makeRect(kMorphRightPanel.frame.x,
        kMorphPanel.frame.y + s3g::gui_layout::kStandardMetrics.headerHeight,
        1.0, kMorphPanel.frame.height
            - s3g::gui_layout::kStandardMetrics.headerHeight));
    drawMenu_id_row_panel_style_labels_values("DRIVER", kMorphSourceParamId, 0u, kMorphLeftPanel, style, labels, values);
    const double morphRowY = s3g::gui_layout::rowY(kMorphLeftPanel, 1u);
    drawing::drawProcessorSlider("MORPH",
        drawing::stringFormat("B %.0f%%", liveMorph * 100.0f),
        liveMorph, morphRowY, kMorphLeftPanel.frame.x,
        kMorphLeftPanel.frame.width, labels, values, style);
    // A full-track scene gradient shows the actual live fade. The fine dark
    // tick preserves the editable manual/base position while the larger gold
    // readhead follows automatic morphology.
    const double morphTrackX = s3g::gui_layout::processorControlX(
        kMorphLeftPanel.frame.x);
    const double morphTrackWidth = s3g::gui_layout::processorTrackWidth(
        kMorphLeftPanel.frame.width);
    const drawing::Rect morphTrack = drawing::makeRect(
        morphTrackX, morphRowY + 1.0, morphTrackWidth, 9.0);
    const drawing::Rect morphInner = drawing::inset(morphTrack, 1.0, 1.0);
    drawing::sceneGradient(morphInner, drawing::withAlpha(drawing::color(0x86a48c), 0.72), drawing::withAlpha(drawing::color(0xb28a72), 0.72));
    drawing::setStroke(style.grid);
    drawing::frameRect(morphTrack);
    const double morphBase = std::clamp<double>(
        paramValue(*plugin, kMorphParamId), 0.0, 1.0);
    drawing::setFill(style.bg);
    drawing::rectFill(drawing::makeRect(morphTrackX + morphTrackWidth * morphBase,
        morphTrack.origin.y, 1.0, morphTrack.size.height));
    drawing::setFill(drawing::color(0xc9b86f));
    drawing::rectFill(drawing::makeRect(std::clamp(
            morphTrackX + morphTrackWidth * liveMorph - 1.5,
            morphTrackX + 1.0, morphTrackX + morphTrackWidth - 4.0),
        morphTrack.origin.y - 2.0, 3.0, morphTrack.size.height + 4.0));
    drawSlider_id_row_panel_style_labels_values("DEPTH", kMorphDepthParamId, 2u, kMorphLeftPanel, style, labels, values);
    drawSlider_id_row_panel_style_labels_values("INERTIA", kMorphInertiaParamId, 3u, kMorphLeftPanel, style, labels, values);
    const auto morphSource = static_cast<s3g::FeedbackMorphSource>(
        static_cast<uint32_t>(paramValue(*plugin, kMorphSourceParamId)));
    const bool pulseMorph = morphSource == s3g::FeedbackMorphSource::Pulse;
    const bool edgeMorph = morphSource == s3g::FeedbackMorphSource::Edge;
    const double ecologyEnvelope = std::clamp<double>(
        plugin->inputEnvelope.load(std::memory_order_relaxed), 0.0, 1.0);
    const double ecologyEdge = std::clamp<double>(
        plugin->ecologyEdge.load(std::memory_order_relaxed), 0.0, 1.0);
    const double ecologyMeter = edgeMorph ? ecologyEdge : ecologyEnvelope;
    drawing::drawProcessorSlider(
        edgeMorph ? "ECOLOGY EDGE" : "ECOLOGY ENV",
        drawing::stringFormat("%3.0f%%", ecologyMeter * 100.0f),
        ecologyMeter, s3g::gui_layout::rowY(kMorphLeftPanel, 4u),
        kMorphLeftPanel.frame.x, kMorphLeftPanel.frame.width,
        labels, values, style);
    if (pulseMorph) {
        drawSlider_id_row_panel_style_labels_values("RATE", kMorphRateParamId, 0u, kMorphRightPanel, style, labels, values);
        drawMenu_id_row_panel_style_labels_values("HOLD", kMorphHoldParamId, 1u, kMorphRightPanel, style, labels, values);
        drawMenu_id_row_panel_style_labels_values("SYNC", kMorphSyncParamId, 2u, kMorphRightPanel, style, labels, values);
        drawMenu_id_row_panel_style_labels_values("DIV", kMorphDivisionParamId, 3u, kMorphRightPanel, style, labels, values);
        drawMenu_id_row_panel_style_labels_values("SHAPE", kMorphShapeParamId, 4u, kMorphRightPanel, style, labels, values);
    } else {
        drawSlider_id_row_panel_style_labels_values("REFLEX", kGovernorReflexParamId, 0u, kMorphRightPanel, style, labels, values);
        drawSlider_id_row_panel_style_labels_values("SENSE", kGovernorSensitivityParamId, 1u, kMorphRightPanel, style, labels, values);
        drawSlider_id_row_panel_style_labels_values("RECOVER", kGovernorRecoveryParamId, 2u, kMorphRightPanel, style, labels, values);
        drawSlider_id_row_panel_style_labels_values("RATE", kMorphRateParamId, 3u, kMorphRightPanel, style, labels, values);
        drawMenu_id_row_panel_style_labels_values("HOLD", kMorphHoldParamId, 4u, kMorphRightPanel, style, labels, values);
    }
    const double phase = plugin->morphPhase.load(std::memory_order_relaxed);
    const double driver = plugin->morphDriver.load(std::memory_order_relaxed);
    drawing::setFill(drawing::color(0xc9b86f));
    drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
        kMorphPanel.frame.x + 164.0 + phase * 78.0,
        kMorphPanel.frame.y + 7.0, 5.0, 5.0)).fill();
    drawing::setFill(drawing::color(0x86a48c));
    drawing::rectFill(drawing::makeRect(kMorphPanel.frame.x + 258.0,
        kMorphPanel.frame.y + 20.0, driver * 258.0, 2.0));

    drawSlider_id_row_panel_style_labels_values("OUT", kOutputParamId, 0u, kOutputPanel, style, labels, values);
    drawMenu_id_row_panel_style_labels_values("MODE", kOutputModeParamId, 1u, kOutputPanel, style, labels, values);
    drawSlider_id_row_panel_style_labels_values("ROTATE", kOutputRotationParamId, 2u, kOutputPanel, style, labels, values);
    const double containment = 1.0f - plugin->minimumGovernor.load(
        std::memory_order_relaxed);
    drawing::drawProcessorSlider("GOV",
        drawing::stringFormat("%3.0f%%", containment * 100.0),
        containment, s3g::gui_layout::rowY(kOutputPanel, 3u),
        kOutputPanel.frame.x, kOutputPanel.frame.width,
        labels, values, style);
    for (uint32_t node = 0u; node < s3g::kFeedbackShiftChannels; ++node) {
        const uint32_t column = node / 4u;
        const uint32_t row = node % 4u;
        const double x = 386.0 + static_cast<double>(column) * 340.0;
        const double y = 570.0 + static_cast<double>(row) * 38.0;
        drawing::drawAtPoint_withAttributes(drawing::stringFormat("N%u", node + 1u), drawing::makePoint(x, y - 3.0), values);
        const drawing::Rect activityTrack = drawing::makeRect(x + 28.0, y, 290.0, 8.0);
        drawing::setFill(drawing::color(0x080808)); drawing::rectFill(activityTrack);
        drawing::setStroke(style.grid); drawing::frameRect(activityTrack);
        drawing::Rect activityFill = drawing::inset(activityTrack, 1.0, 1.0);
        activityFill.size.width *= std::clamp<double>(
            plugin->laneEcologyEnvelopes[node].load(
                std::memory_order_relaxed), 0.0, 1.0);
        drawing::setFill(drawing::color(0x69d2dc)); drawing::rectFill(activityFill);
        drawing::Rect restFill = drawing::makeRect(x + 28.0, y + 13.0, 290.0, 3.0);
        restFill.size.width *= std::clamp<double>(
            plugin->restLaneActivities[node].load(
                std::memory_order_relaxed), 0.0, 1.0);
        drawing::setFill(drawing::color(0xff7047)); drawing::rectFill(restFill);
    }
    }

    if (openMenuItemCount() > 0u) {
        drawing::String  items[32u] {};
        const uint32_t count = openMenuItemCount();
        for (uint32_t item = 0u; item < count; ++item) {
            items[item] = menuItemText(item);
        }
        drawing::drawDropdownMenu(openMenuRect(),
            kFeedbackMenuItemHeight, items, count, selectedMenuItem(),
            _hoverMenuItem, values, style);
    }

}

void updateSlider(drawing::Point point)  {
    if (_dragParam < 0 || _dragPanelWidth <= 0.0) return;
    auto* plugin = static_cast<Plugin*>(_plugin);
    const clap_id id = static_cast<clap_id>(_dragParam);
    const double trackX = s3g::gui_layout::processorControlX(_dragPanelX);
    const double trackWidth = s3g::gui_layout::processorTrackWidth(
        _dragPanelWidth);
    const double normalized = std::clamp(
        (point.x - trackX) / std::max(1.0, trackWidth), 0.0, 1.0);
    uint32_t node = 0u;
    clap_id offset = 0u;
    bool sceneB = false;
    double value = 0.0;
    if (decodeSceneParam(id, sceneB, node, offset)
        && offset == kSceneFrequencyOffset) {
        value = s3g::feedbackShiftFrequencyFromControlNorm(
            static_cast<float>(normalized));
    } else {
        ParamRange range;
        if (!paramRange(id, range)) return;
        value = range.minimum + normalized * (range.maximum - range.minimum);
        if (range.stepped) value = std::round(value);
    }
    queueGuiParamValue(*plugin, id, value);
    _factoryPresetIndex = -1;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CUSTOM");
    setNeedsDisplay(true);

}

void selectMenuItem(uint32_t item)  {
    auto* plugin = static_cast<Plugin*>(_plugin);
    clap_id id = CLAP_INVALID_ID;
    double value = static_cast<double>(item);
    switch (_openMenu) {
    case kFeedbackMenuPreset:
        _factoryPresetIndex = static_cast<int>(std::min(item,
            s3g::kFeedbackShiftPresetCount - 1u));
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            s3g::kFeedbackShiftPresetInfo[
                static_cast<uint32_t>(_factoryPresetIndex)].name);
        queueGuiPatch(*plugin, s3g::feedbackShiftPreset(
            static_cast<uint32_t>(_factoryPresetIndex)));
        return;
    case kFeedbackMenuNodeMode:
        id = nodeParamId(_selectedNode, kNodeModeOffset); break;
    case kFeedbackMenuInsertCategory: {
        const auto category = static_cast<s3g::FeedbackInsertCategory>(
            std::min(item, s3g::kFeedbackInsertCategoryCount - 1u));
        const auto pedal = s3g::feedbackInsertEffectPedal(category, 0u);
        queueGuiParamGesture(*plugin,
            nodeParamId(_selectedNode, kNodePedalOffset),
            static_cast<double>(pedal));
        if (category == s3g::FeedbackInsertCategory::Fracture) {
            queueGuiParamGesture(*plugin,
                feedbackPedalStorageParamId(_selectedNode, 6u), 0.0);
        } else if (category == s3g::FeedbackInsertCategory::Shred) {
            queueGuiParamGesture(*plugin,
                feedbackPedalStorageParamId(_selectedNode, 11u), 0.0);
        }
        _factoryPresetIndex = -1;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "CUSTOM");
        return;
    }
    case kFeedbackMenuInsertEffect: {
        const auto currentPedal = static_cast<s3g::FeedbackPedalType>(
            static_cast<uint32_t>(paramValue(*plugin,
                nodeParamId(_selectedNode, kNodePedalOffset))));
        const auto category = s3g::feedbackInsertCategory(currentPedal);
        const uint32_t count = s3g::feedbackInsertEffectCount(category);
        const uint32_t effect = std::min(item, count - 1u);
        queueGuiParamGesture(*plugin,
            nodeParamId(_selectedNode, kNodePedalOffset),
            static_cast<double>(s3g::feedbackInsertEffectPedal(
                category, effect)));
        if (category == s3g::FeedbackInsertCategory::Fracture) {
            const double normalized = count > 1u
                ? static_cast<double>(effect)
                    / static_cast<double>(count - 1u) : 0.0;
            queueGuiParamGesture(*plugin,
                feedbackPedalStorageParamId(_selectedNode, 6u), normalized);
        } else if (category == s3g::FeedbackInsertCategory::Shred) {
            const double normalized = count > 1u
                ? static_cast<double>(effect)
                    / static_cast<double>(count - 1u) : 0.0;
            queueGuiParamGesture(*plugin,
                feedbackPedalStorageParamId(_selectedNode, 11u), normalized);
        }
        _factoryPresetIndex = -1;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "CUSTOM");
        return;
    }
    case kFeedbackMenuExciterSource:
        id = nodeParamId(_selectedNode, kNodeExciterSourceOffset); break;
    case kFeedbackMenuMorphSource: id = kMorphSourceParamId; break;
    case kFeedbackMenuOutputMode: id = kOutputModeParamId; break;
    case kFeedbackMenuRun: id = kRunParamId; break;
    case kFeedbackMenuMorphHold: id = kMorphHoldParamId; break;
    case kFeedbackMenuMorphSync: id = kMorphSyncParamId; break;
    case kFeedbackMenuMorphDivision: id = kMorphDivisionParamId; break;
    case kFeedbackMenuMorphShape: id = kMorphShapeParamId; break;
    default:
        if (_openMenu >= kFeedbackMenuEffectBase) {
            const auto pedal = static_cast<s3g::FeedbackPedalType>(
                static_cast<uint32_t>(paramValue(*plugin,
                    nodeParamId(_selectedNode, kNodePedalOffset))));
            const auto info = _editingAux
                ? s3g::feedbackAuxControlInfo(_openMenuControl)
                : s3g::feedbackPedalControlInfo(
                    pedal, _openMenuControl);
            id = _editingAux ? auxParamId(info.storageSlot)
                : feedbackPedalStorageParamId(
                    _selectedNode, info.storageSlot);
            const double normalized = info.menuCount > 1u
                ? static_cast<double>(item)
                    / static_cast<double>(info.menuCount - 1u) : 0.0;
            ParamRange range;
            if (paramRange(id, range)) {
                value = range.minimum
                    + normalized * (range.maximum - range.minimum);
            }
        }
        break;
    }
    if (id != CLAP_INVALID_ID) {
        queueGuiParamGesture(*plugin, id, value);
        _factoryPresetIndex = -1;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "CUSTOM");
    }

}

void mouseDown(drawing::Event*  event)  {
    const drawing::Point point = convertPoint_fromView(event->locationInWindow(), nullptr);
    auto* plugin = static_cast<Plugin*>(_plugin);
    if (openMenuItemCount() > 0u) {
        const int hit = drawing::dropdownHitIndex(point,
            openMenuRect(), kFeedbackMenuItemHeight,
            openMenuItemCount());
        if (hit >= 0) selectMenuItem(static_cast<uint32_t>(hit));
        _openMenu = kFeedbackMenuNone;
        _hoverMenuItem = -1;
        setNeedsDisplay(true);
        return;
    }

    const auto titleBand = s3g::gui_layout::matrixTitleBand(kFeedbackCanvas);
    if (drawing::contains(point,
            drawing::cocoaRect(titleBand.presetMenu))) {
        openMenu_control_rect(kFeedbackMenuPreset, 0u, drawing::cocoaRect(titleBand.presetMenu));
        return;
    }
    if (handleProcessorTitleClick(point,
            &plugin->plugin, "Feedback Shift", titleBand,
            _titlePresetName, sizeof(_titlePresetName))) {
        _factoryPresetIndex = -1;
        setNeedsDisplay(true);
        return;
    }
    for (uint32_t page = 0u; page < kFeedbackPageCount; ++page) {
        if (!drawing::contains(point, feedbackPageButtonRect(page))) continue;
        _page = page;
        _editingAux = page == kFeedbackPageAux;
        _openMenu = kFeedbackMenuNone;
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point, feedbackRandomButtonRect(false))
        || drawing::contains(point, feedbackRandomButtonRect(true))) {
        const bool randomizeSceneB = drawing::contains(
            point, feedbackRandomButtonRect(true));
        _randomSeed = _randomSeed * 1664525u + 1013904223u;
        const auto current = paramsSnapshot(*plugin);
        queueGuiPatch(*plugin, s3g::randomFeedbackShiftScene(
            _randomSeed, current, randomizeSceneB));
        _editingSceneB = randomizeSceneB;
        _factoryPresetIndex = -1;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            randomizeSceneB ? "RANDOM B" : "RANDOM A");
        setNeedsDisplay(true);
        return;
    }

    if (_page == kFeedbackPageInsert) {
        for (uint32_t node = 0u;
             node < s3g::kFeedbackShiftChannels; ++node) {
            if (!drawing::contains(point,
                    feedbackNodeRandomButtonRect(node))) continue;
            _selectedNode = node;
            _randomSeed = _randomSeed * 1664525u + 1013904223u;
            const auto current = paramsSnapshot(*plugin);
            const auto randomized = s3g::randomFeedbackShiftNodeInsert(
                _randomSeed, current.nodes[node]);
            queueGuiParamGesture(*plugin,
                nodeParamId(node, kNodePedalOffset),
                static_cast<double>(randomized.pedal));
            for (uint32_t slot = 0u;
                 slot < 4u + s3g::kFeedbackPedalExtraParameterCount; ++slot) {
                queueGuiParamGesture(*plugin,
                    feedbackPedalStorageParamId(node, slot),
                    s3g::feedbackPedalStorageValue(randomized, slot));
            }
            _factoryPresetIndex = -1;
            std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                "CUSTOM");
            setNeedsDisplay(true);
            return;
        }
    }
    if (_page == kFeedbackPageInsert
        || _page == kFeedbackPageEcology) {
    for (uint32_t node = 0u; node < s3g::kFeedbackShiftChannels; ++node) {
        if (drawing::contains(point, feedbackNodeButtonRect(_page, node))) {
            _selectedNode = node;
            _openMenu = kFeedbackMenuNone;
            setNeedsDisplay(true);
            return;
        }
    }
    }
    if (drawing::contains(point, feedbackSceneButtonRect(false))
        || drawing::contains(point, feedbackSceneButtonRect(true))) {
        _editingSceneB = drawing::contains(point, feedbackSceneButtonRect(true));
        _openMenu = kFeedbackMenuNone;
        setNeedsDisplay(true);
        return;
    }
    const clap_id selectedRouteId = matrixParamId(_editingSceneB,
        _selectedDestination, _selectedSource);
    if (_page == kFeedbackPagePatch) {
    for (uint32_t destination = 0u;
         destination < s3g::kFeedbackShiftChannels; ++destination) {
        for (uint32_t source = 0u;
             source < s3g::kFeedbackShiftChannels; ++source) {
            if (!drawing::contains(point,
                    feedbackMatrixCellRect(destination, source))) continue;
            const clap_id id = matrixParamId(
                _editingSceneB, destination, source);
            const double current = paramValue(*plugin, id);
            const bool alreadySelected = source == _selectedSource
                && destination == _selectedDestination;
            _selectedSource = source;
            _selectedDestination = destination;
            const bool negative = (pointerModifiers
                & 2u) != 0u;
            bool changed = false;
            double next = current;
            if (std::abs(current) <= 0.001) {
                const double created = source == destination ? 0.94 : 0.25;
                next = negative ? -created : created;
                changed = true;
            } else if (alreadySelected) {
                next = 0.0;
                changed = true;
            }
            if (changed) {
                queueGuiParamGesture(*plugin, id, next);
                _factoryPresetIndex = -1;
                std::snprintf(_titlePresetName,
                    sizeof(_titlePresetName), "%s", "CUSTOM");
            }
            setNeedsDisplay(true);
            return;
        }
    }

    if (drawing::contains(point, feedbackCrosspointActionRect(0u))) {
        const double current = paramValue(*plugin, selectedRouteId);
        queueGuiParamGesture(*plugin, selectedRouteId,
            std::abs(current) < 0.001 ? 0.25 : -current);
        _factoryPresetIndex = -1;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "CUSTOM");
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point, feedbackCrosspointActionRect(1u))) {
        queueGuiParamGesture(*plugin, selectedRouteId, 0.0);
        _factoryPresetIndex = -1;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "CUSTOM");
        setNeedsDisplay(true);
        return;
    }
    }

    if (_page == kFeedbackPagePatch
        && (drawing::contains(point, feedbackSceneCopyButtonRect(true))
            || drawing::contains(point, feedbackSceneCopyButtonRect(false)))) {
        const bool toSceneB = drawing::contains(
            point, feedbackSceneCopyButtonRect(true));
        auto patch = paramsSnapshot(*plugin);
        if (toSceneB) {
            patch.sceneBNodes = patch.nodes;
            patch.sceneBMatrix = patch.matrix;
            patch.sceneBAuxSend = patch.auxSend;
        } else {
            for (uint32_t node = 0u;
                 node < s3g::kFeedbackShiftChannels; ++node) {
                patch.nodes[node].frequencyHz
                    = patch.sceneBNodes[node].frequencyHz;
                patch.nodes[node].regeneration
                    = patch.sceneBNodes[node].regeneration;
                patch.nodes[node].color = patch.sceneBNodes[node].color;
                patch.nodes[node].body = patch.sceneBNodes[node].body;
                patch.nodes[node].levelDb = patch.sceneBNodes[node].levelDb;
            }
            patch.matrix = patch.sceneBMatrix;
            patch.auxSend = patch.sceneBAuxSend;
        }
        queueGuiPatch(*plugin, patch);
        _factoryPresetIndex = -1;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "CUSTOM");
        setNeedsDisplay(true);
        return;
    }

    if (_page == kFeedbackPageInsert
        && drawing::contains(point,
            feedbackProcessorMenuRect(kNodePanel, 0u))) {
        openMenu_control_rect(kFeedbackMenuNodeMode, 0u, feedbackProcessorMenuRect(kNodePanel, 0u)); return;
    }
    if (_page == kFeedbackPageInsert
        && drawing::contains(point,
            feedbackProcessorMenuRect(kInsertSelectorPanel, 0u))) {
        openMenu_control_rect(kFeedbackMenuInsertCategory, 0u, feedbackProcessorMenuRect(kInsertSelectorPanel, 0u)); return;
    }
    if (_page == kFeedbackPageInsert
        && drawing::contains(point,
            feedbackProcessorMenuRect(kInsertSelectorPanel, 1u))) {
        openMenu_control_rect(kFeedbackMenuInsertEffect, 0u, feedbackProcessorMenuRect(kInsertSelectorPanel, 1u)); return;
    }
    if (_page == kFeedbackPageInsert && drawing::contains(point,
            feedbackProcessorMenuRect(kNodePanel, 1u))) {
        openMenu_control_rect(kFeedbackMenuExciterSource, 0u, feedbackProcessorMenuRect(kNodePanel, 1u)); return;
    }
    const auto morphSource = static_cast<s3g::FeedbackMorphSource>(
        static_cast<uint32_t>(paramValue(*plugin, kMorphSourceParamId)));
    const bool pulseMorph = morphSource == s3g::FeedbackMorphSource::Pulse;
    if (_page == kFeedbackPageEcology) {
    for (uint32_t bank = 0u; bank < 3u; ++bank) {
        if (!drawing::contains(point, feedbackEngineBankButtonRect(bank))) continue;
        _engineBank = bank;
        setNeedsDisplay(true);
        return;
    }
    if (_engineBank == 0u && drawing::contains(point,
            feedbackProcessorMenuRect(kExcitationPanel, 2u))) {
        openMenu_control_rect(kFeedbackMenuRun, 0u, feedbackProcessorMenuRect(kExcitationPanel, 2u)); return;
    }
    if (drawing::contains(point, feedbackProcessorMenuRect(kMorphLeftPanel, 0u))) {
        openMenu_control_rect(kFeedbackMenuMorphSource, 0u, feedbackProcessorMenuRect(kMorphLeftPanel, 0u)); return;
    }
    const uint32_t holdRow = pulseMorph ? 1u : 4u;
    if (drawing::contains(point,
            feedbackProcessorMenuRect(kMorphRightPanel, holdRow))) {
        openMenu_control_rect(kFeedbackMenuMorphHold, 0u, feedbackProcessorMenuRect(kMorphRightPanel, holdRow)); return;
    }
    if (pulseMorph && drawing::contains(point,
            feedbackProcessorMenuRect(kMorphRightPanel, 2u))) {
        openMenu_control_rect(kFeedbackMenuMorphSync, 0u, feedbackProcessorMenuRect(kMorphRightPanel, 2u)); return;
    }
    if (pulseMorph && drawing::contains(point,
            feedbackProcessorMenuRect(kMorphRightPanel, 3u))) {
        openMenu_control_rect(kFeedbackMenuMorphDivision, 0u, feedbackProcessorMenuRect(kMorphRightPanel, 3u)); return;
    }
    if (pulseMorph && drawing::contains(point,
            feedbackProcessorMenuRect(kMorphRightPanel, 4u))) {
        openMenu_control_rect(kFeedbackMenuMorphShape, 0u, feedbackProcessorMenuRect(kMorphRightPanel, 4u)); return;
    }
    if (drawing::contains(point, feedbackProcessorMenuRect(kOutputPanel, 1u))) {
        openMenu_control_rect(kFeedbackMenuOutputMode, 0u, feedbackProcessorMenuRect(kOutputPanel, 1u)); return;
    }
    }

    const auto pedal = static_cast<s3g::FeedbackPedalType>(
        static_cast<uint32_t>(paramValue(*plugin,
            nodeParamId(_selectedNode, kNodePedalOffset))));
    const uint32_t effectRows = _editingAux
        ? s3g::kFeedbackAuxControlCount
        : feedbackVisiblePedalControlCount(pedal);
    const auto pedalPanel = feedbackEffectPanel(effectRows);
    if (_page == kFeedbackPageInsert || _page == kFeedbackPageAux) {
    for (uint32_t row = 0u; row < effectRows; ++row) {
        const uint32_t control = _editingAux ? row
            : feedbackVisiblePedalControlIndex(pedal, row);
        const auto info = _editingAux
            ? s3g::feedbackAuxControlInfo(row)
            : s3g::feedbackPedalControlInfo(pedal, control);
        const auto& controlPanel = _editingAux
            ? (row < 6u ? kAuxFeedbackPanel : kPostGrainPanel)
            : pedalPanel;
        const uint32_t controlRow = _editingAux && row >= 6u
            ? row - 6u : row;
        if (info.display == s3g::FeedbackPedalControlDisplay::Menu
            && drawing::contains(point,
                feedbackProcessorMenuRect(controlPanel, controlRow))) {
            openMenu_control_rect(kFeedbackMenuEffectBase + static_cast<int>(row), control, feedbackProcessorMenuRect(
                    controlPanel, controlRow));
            return;
        }
    }
    }

    if (_page == kFeedbackPageEcology && _engineBank == 0u
        && drawing::contains(point, feedbackActionButtonRect(0u))) {
        plugin->strikeMask.fetch_or(1u << _selectedNode,
            std::memory_order_relaxed);
        if (plugin->host && plugin->host->request_process)
            plugin->host->request_process(plugin->host);
        return;
    }
    if (_page == kFeedbackPageEcology && _engineBank == 0u
        && drawing::contains(point, feedbackActionButtonRect(1u))) {
        plugin->panicRequested.store(true, std::memory_order_release);
        if (plugin->host && plugin->host->request_process)
            plugin->host->request_process(plugin->host);
        return;
    }

    const auto beginSlider = [&](clap_id id,
                                 const s3g::gui_layout::Panel& panel) {
        double defaultValue = 0.0;
        if (drawing::sliderDoubleClickDefault(
                event, &plugin->plugin, id, &defaultValue)) {
            queueGuiParamGesture(*plugin, id, defaultValue);
            _dragParam = -1;
        } else if (queueGuiParamGestureBegin(*plugin, id)) {
            _dragParam = static_cast<int>(id);
            _dragPanelX = panel.frame.x;
            _dragPanelWidth = panel.frame.width;
            updateSlider(point);
        }
        setNeedsDisplay(true);
    };
    const auto hitSlider = [&](const s3g::gui_layout::Panel& panel,
                               uint32_t row, clap_id id) {
        if (!drawing::contains(point, drawing::cocoaRect(
                s3g::gui_layout::sliderHitRect(panel, row)))) return false;
        beginSlider(id, panel);
        return true;
    };
    if (((_page == kFeedbackPageAux) && (hitSlider(kAuxSendLeftPanel, 0u,
                sceneParamId(_editingSceneB, 0u, kSceneAuxSendOffset))
            || hitSlider(kAuxSendLeftPanel, 1u,
                sceneParamId(_editingSceneB, 1u, kSceneAuxSendOffset))
            || hitSlider(kAuxSendLeftPanel, 2u,
                sceneParamId(_editingSceneB, 2u, kSceneAuxSendOffset))
            || hitSlider(kAuxSendLeftPanel, 3u,
                sceneParamId(_editingSceneB, 3u, kSceneAuxSendOffset))
            || hitSlider(kAuxSendRightPanel, 0u,
                sceneParamId(_editingSceneB, 4u, kSceneAuxSendOffset))
            || hitSlider(kAuxSendRightPanel, 1u,
                sceneParamId(_editingSceneB, 5u, kSceneAuxSendOffset))
            || hitSlider(kAuxSendRightPanel, 2u,
                sceneParamId(_editingSceneB, 6u, kSceneAuxSendOffset))
            || hitSlider(kAuxSendRightPanel, 3u,
                sceneParamId(_editingSceneB, 7u, kSceneAuxSendOffset))))
        || ((_page == kFeedbackPageInsert) && (hitSlider(kNodePanel, 3u,
                sceneParamId(_editingSceneB, _selectedNode,
                    kSceneFrequencyOffset))
            || hitSlider(kNodePanel, 4u,
                sceneParamId(_editingSceneB, _selectedNode,
                    kSceneRegenerationOffset))
            || hitSlider(kNodePanel, 5u,
                sceneParamId(_editingSceneB, _selectedNode,
                    kSceneColorOffset))
            || hitSlider(kNodePanel, 6u,
                sceneParamId(_editingSceneB, _selectedNode,
                    kSceneBodyOffset))
            || hitSlider(kNodePanel, 2u,
                nodeParamId(_selectedNode, kNodeExciterGainOffset))
            || hitSlider(kNodePanel, 7u,
                sceneParamId(_editingSceneB, _selectedNode,
                    kSceneLevelOffset))
            || hitSlider(kNodePanel, 8u,
                sceneParamId(_editingSceneB, _selectedNode,
                    kSceneAuxSendOffset))))
        || ((_page == kFeedbackPageEcology) && (_engineBank == 0u
            ? (hitSlider(kExcitationPanel, 0u, kExciteParamId)
                || hitSlider(kExcitationPanel, 1u, kDriftParamId)
                || hitSlider(kExcitationPanel, 3u, kGovernorRestParamId))
            : _engineBank == 1u
            ? (hitSlider(kExcitationPanel, 0u, kSpliceAmountParamId)
                || hitSlider(kExcitationPanel, 1u, kSpliceRateParamId)
                || hitSlider(kExcitationPanel, 2u,
                    kSpliceRateFineParamId)
                || hitSlider(kExcitationPanel, 3u,
                    kSpliceContrastParamId)
                || hitSlider(kExcitationPanel, 4u, kSpliceSpaceParamId))
            : (hitSlider(kExcitationPanel, 0u, kSubBassTuneParamId)
                || hitSlider(kExcitationPanel, 1u, kSubBassShapeParamId)
                || hitSlider(kExcitationPanel, 2u, kSubBassDriveParamId)
                || hitSlider(kExcitationPanel, 3u, kSubBassDecayParamId)
                || hitSlider(kExcitationPanel, 4u, kSubBassSustainParamId))))
        || ((_page == kFeedbackPageEcology) && (
            hitSlider(kMorphLeftPanel, 1u, kMorphParamId)
        || hitSlider(kMorphLeftPanel, 2u, kMorphDepthParamId)
        || hitSlider(kMorphLeftPanel, 3u, kMorphInertiaParamId)
        || (pulseMorph
            ? hitSlider(kMorphRightPanel, 0u, kMorphRateParamId)
            : (hitSlider(kMorphRightPanel, 0u, kGovernorReflexParamId)
                || hitSlider(kMorphRightPanel, 1u,
                    kGovernorSensitivityParamId)
                || hitSlider(kMorphRightPanel, 2u,
                    kGovernorRecoveryParamId)
                || hitSlider(kMorphRightPanel, 3u, kMorphRateParamId)))
        || hitSlider(kOutputPanel, 0u, kOutputParamId)
        || hitSlider(kOutputPanel, 2u, kOutputRotationParamId)))
        || ((_page == kFeedbackPagePatch)
            && hitSlider(kCrosspointPanel, 0u, selectedRouteId))) return;
    if (_page == kFeedbackPageInsert || _page == kFeedbackPageAux) {
    for (uint32_t row = 0u; row < effectRows; ++row) {
        const uint32_t control = _editingAux ? row
            : feedbackVisiblePedalControlIndex(pedal, row);
        const auto info = _editingAux
            ? s3g::feedbackAuxControlInfo(row)
            : s3g::feedbackPedalControlInfo(pedal, control);
        const auto& controlPanel = _editingAux
            ? (row < 6u ? kAuxFeedbackPanel : kPostGrainPanel)
            : pedalPanel;
        const uint32_t controlRow = _editingAux && row >= 6u
            ? row - 6u : row;
        if (info.display != s3g::FeedbackPedalControlDisplay::Menu
            && hitSlider(controlPanel, controlRow,
                _editingAux ? auxParamId(info.storageSlot)
                : feedbackPedalStorageParamId(
                    _selectedNode, info.storageSlot))) return;
    }
    }

}

void mouseMoved(drawing::Event*  event)  {
    if (_openMenu == kFeedbackMenuNone) return;
    updateMenuHover(convertPoint_fromView(event->locationInWindow(), nullptr));

}

void mouseDragged(drawing::Event*  event)  {
    updateSlider(convertPoint_fromView(event->locationInWindow(), nullptr));

}

void mouseUp(drawing::Event*  event)  {
    (void)event;
    if (_dragParam >= 0) {
        queueGuiParamGestureEnd(*static_cast<Plugin*>(_plugin),
            static_cast<clap_id>(_dragParam));
    }
    _dragParam = -1;

}

};
}
s3g::portable_gui::foundation::ContentView* makePortableCanvas(Plugin& p) { return new feedback_processor_canvas::Editor(p); }
#include "s3g_clap_canvas_gui.inc"
