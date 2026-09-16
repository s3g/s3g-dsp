#include "s3g_sample_family_vstgui.h"
#include "s3g_sample_cursor_presenter.h"
#if defined(_WIN32)
#include "s3g_windows_sample_rings_cache.h"
#endif

#include "s3g_gui_layout.h"
#include "s3g_vstgui_foundation.h"
#include "s3g_crcltr.h"
#include "s3g_sample_lanes.h"
#include "s3g_sample_motion.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/dragging.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/idatapackage.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace s3g::portable_gui {
namespace {

using namespace VSTGUI;
using foundation::color;

constexpr double kOuterInset = 18.0;
constexpr double kPanelTop = 42.0;
constexpr double kSamplePanelHeight = 276.0;
constexpr double kPanelGap = 12.0;
constexpr double kParameterRowHeight = 22.0;

CRect rect(double x, double y, double width, double height)
{
    return foundation::rect(x, y, width, height);
}

CRect rect(const gui_layout::Rect& value)
{
    return rect(value.x, value.y, value.width, value.height);
}

CColor alphaColor(uint32_t rgb, uint8_t alpha)
{
    CColor result = color(rgb);
    result.alpha = alpha;
    return result;
}

bool contains(const CRect& bounds, const CPoint& point)
{
    return foundation::contains(bounds, point);
}

std::string uppercaseAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
    return value;
}

std::vector<std::string> droppedFiles(IDataPackage* package)
{
    if (!package) return {};
    std::vector<std::string> paths;
    for (uint32_t index = 0u; index < package->getCount(); ++index) {
        const void* data = nullptr;
        IDataPackage::Type type = IDataPackage::kError;
        const uint32_t size = package->getData(index, data, type);
        if (type != IDataPackage::kFilePath || !data || size == 0u)
            continue;
        const char* path = static_cast<const char*>(data);
        const void* terminator = std::memchr(path, '\0', size);
        const std::size_t length = terminator
            ? static_cast<std::size_t>(
                static_cast<const char*>(terminator) - path)
            : static_cast<std::size_t>(size);
        if (length) paths.emplace_back(path, length);
    }
    return paths;
}

struct ParameterCell {
    std::size_t parameterIndex = 0u;
    CRect row;
    CRect control;
    CRect track;
    std::string label;
    bool menu = false;
};

struct ParameterPanel {
    CRect bounds;
    std::string title;
};

struct ParameterLayout {
    std::vector<ParameterCell> cells;
    std::vector<ParameterPanel> panels;
};

enum class CanvasMenuKind : uint8_t {
    None = 0u,
    Parameter,
    Preset,
};

struct CanvasMenuEntry {
    std::string label;
    double value = 0.0;
    uint32_t itemIndex = 0u;
};

class SampleFamilyView final : public foundation::ContentView,
                               public IDropTarget {
public:
    explicit SampleFamilyView(const SampleFamilyEditorConfig& editorConfig)
        : ContentView(rect(0.0, 0.0, editorConfig.nativeWidth,
            editorConfig.nativeHeight))
        , config(editorConfig)
    {
        const auto& metrics = foundation::fontMetrics();
        font = foundation::makeUiFont(metrics.body);
        titleFont = foundation::makeUiFont(metrics.title);
        tinyFont = foundation::makeUiFont(metrics.channel);
        const foundation::ParameterEditCallbacks editCallbacks {
            config.callbacks.context,
            config.callbacks.beginParamEdit,
            config.callbacks.setParam,
            config.callbacks.endParamEdit,
        };
        parameterEdit.setCallbacks(editCallbacks);
        markerEdit.setCallbacks(editCallbacks);
        readParameters();
        if (config.callbacks.loadSample) {
            const uint32_t slots = sampleSlotCount();
            for (uint32_t slot = 0u; slot < slots; ++slot) {
                char variable[64] {};
                std::snprintf(variable, sizeof(variable), slot == 0u
                    ? "S3G_GUI_DOCUMENTATION_SAMPLE_PATH"
                    : "S3G_GUI_DOCUMENTATION_SAMPLE_PATH_%u", slot + 1u);
                const char* path = std::getenv(variable);
                if (path && path[0])
                    config.callbacks.loadSample(config.callbacks.context,
                        slot, path);
            }
        }
    }

    void startRefresh() override
    {
        if (refreshTimer) return;
        refreshTimer = makeOwned<CVSTGUITimer>(
            [this](CVSTGUITimer*) {
                if (config.callbacks.service)
                    config.callbacks.service(config.callbacks.context);
                clampSelectedSlot();
                if (hasScrollableWaveform()) {
                    const auto* current = asset(0u);
                    if (current != displayedWaveformAsset) {
                        displayedWaveformAsset = current;
                        resetWavesetsView();
                    }
                }
                updateCursorPresentation();
                if (isVisible()) invalid();
            }, 33u);
    }

    void stopRefresh() override
    {
        refreshTimer = nullptr;
        cursorPresenter.reset();
        cursorPresentationActive = false;
        cursorPresenterUnavailable = false;
    }

    void updateCursorPresentation()
    {
        if (!config.callbacks.getCursorTrajectories || !getFrame() || !isVisible()
            || cursorPresenterUnavailable) return;
        if (!cursorPresenter) cursorPresenter = SampleCursorPresenter::create(getFrame());
        if (!cursorPresenter) {
            cursorPresenterUnavailable = true;
            return; // Software fallback remains visible; retry only on reopen.
        }
        std::array<SampleCursorTrajectory, 64> trajectories {};
        const uint32_t count = std::min(64u, config.callbacks.getCursorTrajectories(
            config.callbacks.context, trajectories.data(), 64u));
        std::array<SampleFamilyCursorState, 64> states {};
        if (config.callbacks.getCursorStates)
            config.callbacks.getCursorStates(config.callbacks.context, 0, states.data(), 64u);
        std::array<SampleCursorVisual, 64> visuals {};
        const CRect wave = waveformRect();
        const bool doubles = config.visualization == SampleFamilyVisualization::Doubles;
        for (uint32_t i = 0; i < count; ++i) {
            auto& v = visuals[i];
            v.trajectory = trajectories[i];
            v.color = doubles ? color(i == 0 ? 0x69d2dc : 0xff7047)
                : i % 3 == 0 ? style.fill : i % 3 == 1 ? style.accent : style.dim;
            v.top = doubles ? i * wave.getHeight() * 0.5 : 0;
            v.height = doubles ? wave.getHeight() * 0.5 : wave.getHeight();
            v.flagTop = 18 + (i % 10) * 15;
            if (!doubles) {
                const auto& state = states[i];
                char label[32] {};
                const bool motion = config.visualization == SampleFamilyVisualization::Motion;
                if (state.hasOutputRouting && state.outputWidth > 1)
                    std::snprintf(label, sizeof(label), motion ? "N%u>%02u/%02u" : "N%u > %02u/%02u",
                        state.key, state.outputFirst + 1, state.outputSecond + 1);
                else if (state.hasOutputRouting)
                    std::snprintf(label, sizeof(label), motion ? "N%u>%02u" : "N%u > %02u",
                        state.key, state.outputFirst + 1);
                else std::snprintf(label, sizeof(label), "N%u", state.key);
                v.label = label;
            }
        }
        CRect occlusion {};
        if (canvasMenuKind != CanvasMenuKind::None) {
            occlusion = canvasMenuBounds;
            occlusion.inset(-2, -2);
        }
        cursorPresentationActive = cursorPresenter->update(wave, occlusion,
            hasScrollableWaveform() ? wavesetsViewStart : 0,
            hasScrollableWaveform() ? wavesetsVisibleSpan() : 1, visuals, count);
        if (!cursorPresentationActive) {
            cursorPresenter.reset();
            cursorPresenterUnavailable = true;
        }
    }

    void draw(CDrawContext* context) override
    {
        if (!context) return;
        if (sampleCursorScreenDraw()) updateCursorPresentation();
        context->setDrawMode(kAntiAliasing);
        context->setFillColor(style.background);
        context->drawRect(getViewSize(), kDrawFilled);
        drawTitleBand(*context);
        drawSamplePanel(*context);
        drawParameterPanels(*context);
        drawOpenMenu(*context);
        setDirty(false);
    }

    void onMouseDownEvent(MouseDownEvent& event) override
    {
        clampSelectedSlot();
        if (canvasMenuKind != CanvasMenuKind::None) {
            if (event.buttonState.isLeft()) {
                const int32_t selected = menuItemAt(event.mousePosition);
                const CanvasMenuKind kind = canvasMenuKind;
                const uint32_t parameterId = canvasMenuParameterId;
                closeMenu();
                if (selected >= 0
                    && static_cast<std::size_t>(selected)
                        < canvasMenuEntries.size()) {
                    const auto entry = canvasMenuEntries[
                        static_cast<std::size_t>(selected)];
                    if (kind == CanvasMenuKind::Preset
                        && config.callbacks.applyFactoryPreset
                        && config.callbacks.applyFactoryPreset(
                            config.callbacks.context,
                            static_cast<uint32_t>(std::lround(entry.value)))) {
                        presetName = entry.label;
                        selectedPreset = selected;
                        if (selected == 0) resetWavesetsWaveView();
                    } else if (kind == CanvasMenuKind::Parameter) {
                        const bool customApplied
                            = config.callbacks.applyParameterMenuItem
                            && config.callbacks.applyParameterMenuItem(
                                config.callbacks.context, parameterId,
                                entry.itemIndex);
                        if (!customApplied)
                            parameterEdit.perform(parameterId, entry.value);
                        markPresetEdited();
                    }
                }
                invalid();
                event.consumed = true;
            } else if (event.buttonState.isRight()) {
                closeMenu();
                invalid();
                event.consumed = true;
            }
            return;
        }
        if (event.buttonState.isRight()
            && contains(visualizationGraphRect(), event.mousePosition)
            && config.callbacks.removeVisualizationPoint) {
            const int32_t point = visualizationPointAt(event.mousePosition);
            if (point >= 0) {
                config.callbacks.removeVisualizationPoint(
                    config.callbacks.context, static_cast<uint32_t>(point));
                invalid();
                event.consumed = true;
            }
            return;
        }
        if (!event.buttonState.isLeft()) return;
        const CPoint point = event.mousePosition;
        if (config.visualization == SampleFamilyVisualization::Circulator
            && contains(visualizationRect(), point)
            && paramNamed("Crossfade Motion") < 0.5) {
            const auto index = parameterIndex("Position / Rate");
            if (index < parameters.size()) {
                circulatorMixDrag = true;
                parameterEdit.begin(parameters[index].id);
                updateCirculatorMix(point);
            }
            event.consumed = true;
            return;
        }
        if (config.visualization == SampleFamilyVisualization::Doubles) {
            if (contains(rect(554.0, 558.0, 80.0, 28.0), point)) {
                setNamedParameter("Link Decks", paramNamed("Link Decks") >= 0.5
                    ? 0.0 : 1.0);
                invalid();
                event.consumed = true;
                return;
            }
            const CRect tempoButtons[] {rect(568.0,263.0,44.0,17.0),
                rect(616.0,263.0,52.0,17.0), rect(672.0,263.0,44.0,17.0)};
            for (uint32_t index = 0u; index < 3u; ++index) {
                if (!contains(tempoButtons[index], point)) continue;
                if (config.callbacks.applyTempoMultiplier)
                    config.callbacks.applyTempoMultiplier(config.callbacks.context,
                        index == 0u ? 0.5 : index == 2u ? 2.0 : 1.0,
                        index == 1u);
                invalid();
                event.consumed = true;
                return;
            }
        }
        const auto band = gui_layout::encoderTitleBand({
            static_cast<double>(config.nativeWidth),
            static_cast<double>(config.nativeHeight),
        });
        if (contains(rect(band.presetMenu), point)) {
            if (!openPresetMenu()) {
                if (config.callbacks.resetToDefaults)
                    config.callbacks.resetToDefaults(config.callbacks.context);
                presetName = "INIT";
                selectedPreset = 0;
                resetWavesetsWaveView();
            }
            invalid();
            event.consumed = true;
            return;
        }
        if (contains(rect(band.loadButton), point)) {
            selectPreset(false);
            event.consumed = true;
            return;
        }
        if (contains(rect(band.saveButton), point)) {
            selectPreset(true);
            event.consumed = true;
            return;
        }

        if (config.visualization == SampleFamilyVisualization::Rings) {
            for (uint32_t head = 0u; head < 8u; ++head) {
                if (!contains(rect(860.0 + head * 58.0, 484.0,
                        52.0, 21.0), point)) continue;
                selectedHead = head;
                if (event.modifiers.has(ModifierKey::Alt)) {
                    const auto mask = static_cast<uint32_t>(std::lround(paramNamed("Head Mask", 255.0)));
                    setNamedParameter("Head Mask", mask ^ (1u << head));
                    soloHead = -1;
                }
                invalid();
                event.consumed = true;
                return;
            }
            if (contains(rect(1198.0, 454.0, 62.0, 19.0), point)) {
                const uint32_t mask = static_cast<uint32_t>(std::lround(
                    paramNamed("Head Mask", 255.0)));
                setNamedParameter("Head Mask",
                    static_cast<double>(mask ^ (1u << selectedHead)));
                soloHead = -1;
                invalid();
                event.consumed = true;
                return;
            }
            if (contains(rect(1266.0, 454.0, 60.0, 19.0), point)) {
                const uint32_t bit = 1u << selectedHead;
                const uint32_t mask = static_cast<uint32_t>(std::lround(
                    paramNamed("Head Mask", 255.0)));
                uint32_t next = bit;
                if (mask == bit) {
                    next = soloHead == static_cast<int32_t>(selectedHead) ? soloRestoreMask : 255u;
                    soloHead = -1;
                } else {
                    if (soloHead < 0 || mask != (1u << static_cast<uint32_t>(soloHead))) soloRestoreMask = mask;
                    soloHead = static_cast<int32_t>(selectedHead);
                }
                setNamedParameter("Head Mask", next);
                invalid();
                event.consumed = true;
                return;
            }
        }

        const uint32_t slots = sampleSlotCount();
        for (uint32_t slot = 0u; slot < slots; ++slot) {
            if (contains(slotButtonRect(slot, slots), point)) {
                selectedSlot = slot;
                if (config.visualization
                        == SampleFamilyVisualization::Rings)
                    setNamedParameter("Selected Slot", slot);
                invalid();
                event.consumed = true;
                return;
            }
        }
        for (uint32_t slot = 0u; slot < slots; ++slot) {
            if (config.visualization == SampleFamilyVisualization::Rings && slot != selectedSlot) continue;
            if (contains(sampleLoadButtonRect(slot), point)) {
                selectedSlot = slot;
                selectSample();
                event.consumed = true;
                return;
            }
            if (contains(sampleClearButtonRect(slot), point)) {
                selectedSlot = slot;
                if (config.callbacks.clearSample)
                    config.callbacks.clearSample(config.callbacks.context,
                        selectedSlot);
                invalid();
                event.consumed = true;
                return;
            }
        }
        if (contains(storageButtonRect(), point)) {
            if (config.callbacks.cycleStorageMode)
                config.callbacks.cycleStorageMode(config.callbacks.context,
                    selectedSlot);
            invalid();
            event.consumed = true;
            return;
        }
        if (contains(outputPageButtonRect(), point)) {
            outputRoutingPage = !outputRoutingPage;
            invalid();
            event.consumed = true;
            return;
        }
        for (uint32_t action = 0u; action < config.actionCount; ++action) {
            if (!contains(actionButtonRect(action), point)) continue;
            const auto& definition = config.actions[action];
            if (config.visualization == SampleFamilyVisualization::Cutups
                && config.callbacks.recalculateSample && action == 2u) {
                config.callbacks.recalculateSample(config.callbacks.context, selectedSlot);
                event.consumed = true;
                return;
            }
            if (config.visualization == SampleFamilyVisualization::Rings
                && ((action == 5u && (paramNamed("Radial Path") == 0.0 || paramNamed("Radial Path") == 7.0))
                    || (action == 7u && !asset(selectedSlot)))) {
                event.consumed = true;
                return;
            }
            if (config.callbacks.performAction)
                config.callbacks.performAction(config.callbacks.context,
                    definition.actionId, true);
            if (definition.hold) heldAction = action + 1u;
            invalid();
            event.consumed = true;
            return;
        }

        if (config.visualization == SampleFamilyVisualization::Rings && beginRingDrag(point)) {
            event.consumed = true;
            return;
        }
        const CRect wave = waveformRect();
        const bool inWave = isLaneFamily()
            ? point.x >= 118.0 && point.x <= 782.0
                && point.y >= 89.0 && point.y <= 325.0
            : contains(wave, point);
        if (inWave) {
            selectedSlot = waveformSlotAt(point);
            if (config.visualization == SampleFamilyVisualization::Doubles
                && config.callbacks.placeDoublesCue) {
                SampleFamilyDoublesState state {};
                if (config.callbacks.getDoublesState) config.callbacks.getDoublesState(config.callbacks.context, &state);
                const uint32_t deck = point.y < wave.getCenter().y ? 0u : 1u;
                if ((state.cueMask & (1u << deck)) && std::abs(point.x - wave.left
                        - state.cues[deck] * wave.getWidth()) <= 10.0) {
                    cueDragDeck = static_cast<int32_t>(deck);
                    updateDoublesCue(point);
                    event.consumed = true;
                    return;
                }
            }
            const uint32_t marker = markerAt(point);
            if (marker != 0u) {
                markerDragParameter = marker;
                markerEdit.begin(marker);
                updateMarker(point);
                markPresetEdited();
            } else if (hasScrollableWaveform()
                && event.clickCount >= 2u) {
                resetWavesetsWaveView();
            }
            invalid();
            event.consumed = true;
            return;
        }

        if (config.visualization == SampleFamilyVisualization::Wavesets
            && contains(visualizationRect(), point)) {
            if (event.clickCount >= 2u) wavesetsScopeScale = 1.0;
            invalid();
            event.consumed = true;
            return;
        }

        if (contains(visualizationGraphRect(), point)
            && config.callbacks.getVisualizationPoints) {
            visualizationAlternateDrag = event.modifiers.has(
                ModifierKey::Alt);
            if ((config.visualization == SampleFamilyVisualization::Lanes
                    || config.visualization
                        == SampleFamilyVisualization::Grains)
                && config.callbacks.addVisualizationPoint) {
                visualizationDragPoint = visualizationPointAt(point);
                const auto normalized = normalizedVisualizationPoint(point);
                if (visualizationDragPoint < 0
                    && config.callbacks.addVisualizationPoint(
                        config.callbacks.context,
                        static_cast<float>(normalized.x),
                        static_cast<float>(normalized.y)))
                    visualizationDragPoint = visualizationPointAt(point);
                if (visualizationDragPoint >= 0)
                    updateVisualizationPoint(point);
            } else if (config.callbacks.setVisualizationPoint) {
                visualizationDragPoint = visualizationPointAt(point);
                if (visualizationDragPoint >= 0)
                    updateVisualizationPoint(point);
            }
            invalid();
            event.consumed = true;
            return;
        }

        const ParameterLayout layout = parameterLayout();
        for (const auto& cell : layout.cells) {
            if (!contains(cell.row, point)) continue;
            const auto& parameter = parameters[cell.parameterIndex];
            if (parameter.readOnly || !controlInactiveReason(parameter).empty()) return;
            const bool circulatorToggle
                = config.visualization
                        == SampleFamilyVisualization::Circulator
                && (std::strcmp(parameter.name, "Loop 1 Reverse") == 0
                    || std::strcmp(parameter.name, "Loop 2 Reverse") == 0);
            if (circulatorToggle) {
                parameterEdit.perform(parameter.id,
                    param(parameter.id) >= 0.5 ? 0.0 : 1.0);
                markPresetEdited();
            } else if (event.clickCount >= 2u) {
                parameterEdit.perform(parameter.id, parameter.defaultValue);
                markPresetEdited();
            } else if (cell.menu && openParameterMenu(parameter, cell)) {
                // The in-canvas popup owns selection until the next click.
            } else if (cell.menu && parameter.stepped) {
                const double direction = event.modifiers.has(
                    ModifierKey::Shift) ? -1.0 : 1.0;
                double value = std::round(param(parameter.id)) + direction;
                if (value > parameter.maximum) value = parameter.minimum;
                if (value < parameter.minimum) value = parameter.maximum;
                parameterEdit.perform(parameter.id, value);
                markPresetEdited();
            } else {
                dragParameter = parameter.id;
                parameterEdit.begin(parameter.id);
                updateContinuousParameter(parameter, cell, point);
                markPresetEdited();
            }
            invalid();
            event.consumed = true;
            return;
        }
    }

    void onMouseMoveEvent(MouseMoveEvent& event) override
    {
        if (canvasMenuKind != CanvasMenuKind::None
            && !event.buttonState.has(MouseButton::Left)) {
            const int32_t hover = menuItemAt(event.mousePosition);
            if (hover != canvasMenuHover) {
                canvasMenuHover = hover;
                invalid();
            }
            event.consumed = true;
            return;
        }
        if (!event.buttonState.has(MouseButton::Left)) return;
        if (ringDrag) {
            updateRingDrag(event.mousePosition);
            event.consumed = true;
            return;
        }
        if (cueDragDeck >= 0) {
            updateDoublesCue(event.mousePosition);
            event.consumed = true;
            return;
        }
        if (circulatorMixDrag) {
            updateCirculatorMix(event.mousePosition);
            event.consumed = true;
            return;
        }
        if (markerDragParameter != 0u) {
            updateMarker(event.mousePosition);
            event.consumed = true;
            return;
        }
        if (visualizationDragPoint >= 0) {
            visualizationAlternateDrag = event.modifiers.has(
                ModifierKey::Alt);
            updateVisualizationPoint(event.mousePosition);
            event.consumed = true;
            return;
        }
        if (dragParameter == 0u) return;
        const ParameterLayout layout = parameterLayout();
        for (const auto& cell : layout.cells) {
            const auto& parameter = parameters[cell.parameterIndex];
            if (parameter.id != dragParameter) continue;
            updateContinuousParameter(parameter, cell, event.mousePosition);
            event.consumed = true;
            return;
        }
    }

    void onMouseUpEvent(MouseUpEvent& event) override
    {
        finishEdits();
        releaseHeldAction();
        event.consumed = true;
    }

    void onMouseCancelEvent(MouseCancelEvent& event) override
    {
        finishEdits();
        releaseHeldAction();
        event.consumed = true;
    }

    void onMouseWheelEvent(MouseWheelEvent& event) override
    {
        if (canvasMenuKind != CanvasMenuKind::None) {
            event.consumed = true;
            return;
        }
        if (config.visualization == SampleFamilyVisualization::Wavesets) {
            if (contains(visualizationRect(), event.mousePosition)
                && asset(0u)) {
                wavesetsScopeScale = std::clamp(wavesetsScopeScale
                    * std::exp(-event.deltaY * 0.08), 1.0, 16.0);
                invalid();
                event.consumed = true;
                return;
            }
        }
        if (hasScrollableWaveform()
            && contains(waveformRect(), event.mousePosition) && asset(0u)) {
                const double oldSpan = wavesetsVisibleSpan();
                const bool pan = event.modifiers.has(ModifierKey::Shift)
                    || std::abs(event.deltaX) > std::abs(event.deltaY);
                if (pan && wavesetsWaveZoom > 1.0) {
                    const double delta = std::abs(event.deltaX)
                            > std::abs(event.deltaY)
                        ? event.deltaX : event.deltaY;
                    wavesetsViewStart = std::clamp(wavesetsViewStart
                        + delta * oldSpan * 0.0125,
                        0.0, 1.0 - oldSpan);
                } else {
                    const CRect wave = waveformRect();
                    const double anchor = std::clamp(
                        (event.mousePosition.x - wave.left)
                            / wave.getWidth(), 0.0, 1.0);
                    const double sourceAnchor = wavesetsViewStart
                        + anchor * oldSpan;
                    wavesetsWaveZoom = std::clamp(wavesetsWaveZoom
                        * std::exp(event.deltaY * 0.08), 1.0, 128.0);
                    const double newSpan = wavesetsVisibleSpan();
                    wavesetsViewStart = std::clamp(sourceAnchor
                        - anchor * newSpan, 0.0, 1.0 - newSpan);
                }
                invalid();
                event.consumed = true;
                return;
        }
        const ParameterLayout layout = parameterLayout();
        for (const auto& cell : layout.cells) {
            if (!contains(cell.row, event.mousePosition)) continue;
            const auto& parameter = parameters[cell.parameterIndex];
            if (parameter.readOnly || !controlInactiveReason(parameter).empty()) return;
            const double span = parameter.maximum - parameter.minimum;
            const double increment = parameter.stepped ? 1.0
                : span * (event.modifiers.has(ModifierKey::Shift)
                    ? 0.001 : 0.01);
            const double value = std::clamp(param(parameter.id)
                + (event.deltaY >= 0.0 ? increment : -increment),
                parameter.minimum, parameter.maximum);
            parameterEdit.perform(parameter.id,
                parameter.stepped ? std::round(value) : value);
            markPresetEdited();
            invalid();
            event.consumed = true;
            return;
        }
    }

    SharedPointer<IDropTarget> getDropTarget() override { return this; }

    DragOperation onDragEnter(DragEventData data) override
    {
        return droppedFiles(data.drag).empty()
            ? DragOperation::None : DragOperation::Copy;
    }

    DragOperation onDragMove(DragEventData data) override
    {
        selectedSlot = waveformSlotAt(data.pos);
        invalid();
        return onDragEnter(data);
    }

    void onDragLeave(DragEventData) override {}

    bool onDrop(DragEventData data) override
    {
        clampSelectedSlot();
        const auto paths = droppedFiles(data.drag);
        if (paths.empty() || !config.callbacks.loadSample) return false;
        uint32_t slot = waveformSlotAt(data.pos);
        if (config.visualization == SampleFamilyVisualization::Circulator && paths.size() > 1u) slot = 0u;
        bool loaded = false;
        for (const auto& path : paths) {
            if (slot >= sampleSlotCount()) break;
            loaded = config.callbacks.loadSample(config.callbacks.context, slot++, path.c_str()) || loaded;
        }
        return loaded;
    }

private:
    void readParameters()
    {
        if (!config.callbacks.getParameterCount
            || !config.callbacks.getParameterInfo) return;
        const uint32_t count = config.callbacks.getParameterCount(
            config.callbacks.context);
        parameters.reserve(count);
        for (uint32_t index = 0u; index < count; ++index) {
            SampleFamilyParameterInfo info {};
            if (!config.callbacks.getParameterInfo(
                    config.callbacks.context, index, &info)) continue;
            if (!(info.maximum > info.minimum)) info.readOnly = true;
            parameters.push_back(info);
        }
    }

    uint32_t sampleSlotCount() const
    {
        const uint32_t count = config.callbacks.getSampleSlotCount
            ? config.callbacks.getSampleSlotCount(config.callbacks.context)
            : 1u;
        return std::clamp(count, 1u, 16u);
    }

    void clampSelectedSlot()
    {
        if (config.visualization == SampleFamilyVisualization::Rings)
            selectedSlot = static_cast<uint32_t>(std::clamp(paramNamed("Selected Slot"), 0.0, 3.0));
        selectedSlot = std::min(selectedSlot, sampleSlotCount() - 1u);
    }

    uint32_t ringCount() const
    {
        uint32_t count = 0u;
        for (uint32_t slot = 0u; slot < sampleSlotCount(); ++slot)
            if (const auto* source = asset(slot)) count += std::min<uint32_t>(8u, source->channelCount);
        return count;
    }

    double ringRadius(uint32_t ring) const
    {
        const uint32_t count = ringCount();
        return count <= 1u ? 163.0 : 34.0 + 258.0 * ring / (count - 1u);
    }

    CPoint ringPoint(uint32_t ring, double phase) const
    {
        const double angle = phase * 6.28318530717958647692;
        const double radius = ringRadius(ring);
        return CPoint(428.0 + std::sin(angle) * radius, 378.0 - std::cos(angle) * radius);
    }

    bool beginRingDrag(const CPoint& point)
    {
        const double radius = std::hypot(point.x - 428.0, point.y - 378.0);
        if (!ringCount() || radius < 19.0 || radius > 307.0 || !config.callbacks.getRingsHeadState) return false;
        const uint32_t mask = static_cast<uint32_t>(std::lround(paramNamed("Head Mask", 255.0)));
        double nearest = 15.0;
        for (uint32_t head = 0u; head < 8u; ++head) {
            if (!(mask & (1u << head))) continue;
            SampleFamilyRingsHeadState state {};
            if (!config.callbacks.getRingsHeadState(config.callbacks.context, head, &state)) continue;
            const CPoint p = ringPoint(state.ringA, state.phaseA);
            const double distance = std::hypot(point.x - p.x, point.y - p.y);
            if (distance < nearest) { nearest = distance; selectedHead = head; }
        }
        setNamedParameter("Radial Path", 7.0);
        setNamedParameter("Head Relationship", 7.0);
        SampleFamilyRingsHeadState state {};
        if (!config.callbacks.getRingsHeadState(config.callbacks.context, selectedHead, &state)) return false;
        parameterEdit.begin(state.manualRingParameterId);
        markerEdit.begin(state.manualPhaseParameterId);
        ringDrag = true;
        updateRingDrag(point);
        return true;
    }

    void updateRingDrag(const CPoint& point)
    {
        const double radius = std::clamp(std::hypot(point.x - 428.0, point.y - 378.0), 34.0, 292.0);
        double radial = (radius - 34.0) / 258.0;
        if (paramNamed("Head Formation") != 0.0) {
            std::array<double, 4u> radii {};
            uint32_t count = 0u, base = 0u;
            for (uint32_t slot = 0u; slot < sampleSlotCount(); ++slot) {
                const auto* source = asset(slot);
                if (!source || !source->channelCount) continue;
                const uint32_t width = std::min<uint32_t>(source->channelCount, 8u);
                radii[count++] = ringRadius(base + selectedHead % width);
                base += width;
            }
            radial = count <= 1u || radius <= radii[0u] ? 0.0 : 1.0;
            for (uint32_t i = 0u; i + 1u < count; ++i)
                if (radius >= radii[i] && radius <= radii[i + 1u])
                    radial = (i + (radius - radii[i]) / std::max(1.0, radii[i + 1u] - radii[i])) / (count - 1u);
        }
        double phase = std::atan2(point.x - 428.0, 378.0 - point.y) / 6.28318530717958647692;
        phase -= std::floor(phase);
        parameterEdit.set(radial);
        markerEdit.set(phase);
        if (config.callbacks.previewRingsHead)
            config.callbacks.previewRingsHead(config.callbacks.context,
                selectedHead, static_cast<float>(radial), static_cast<float>(phase));
        markPresetEdited();
        invalid();
    }

    void updateDoublesCue(const CPoint& point)
    {
        if (cueDragDeck < 0 || !config.callbacks.placeDoublesCue) return;
        const CRect wave = waveformRect();
        config.callbacks.placeDoublesCue(config.callbacks.context, static_cast<uint32_t>(cueDragDeck),
            static_cast<float>(std::clamp((point.x - wave.left) / wave.getWidth(), 0.0, 1.0)));
        invalid();
    }

    // These are editor affordances, not host read-only flags: retain the stored
    // values and automation surface while reproducing Cocoa's mode conditions.
    std::string controlInactiveReason(const SampleFamilyParameterInfo& info) const
    {
        const std::string name(info.name);
        if (config.visualization == SampleFamilyVisualization::Motion) {
            const int path = static_cast<int>(std::lround(paramNamed("Motion")));
            const int model = static_cast<int>(std::lround(paramNamed("Segment Model")));
            const int trigger = static_cast<int>(std::lround(paramNamed("Segment Trigger")));
            if (name == "Travel" && path != 2 && path != 3) return "DRUNK / ZIGZAG";
            if (name == "Step" && path != 6 && model != 2 && model != 4 && model != 6) return "NO SOURCE STEP";
            if (model != 0 && trigger != 0 && ((name == "Event Rate" && model != 3)
                    || (name == "Interval Curve" && model == 5))) return "CLOCK";
        }
        if (config.visualization != SampleFamilyVisualization::Rings) return {};
        const int path = static_cast<int>(std::lround(paramNamed("Radial Path")));
        const int angular = static_cast<int>(std::lround(paramNamed("Head Relationship")));
        const int formation = static_cast<int>(std::lround(paramNamed("Head Formation")));
        if (name.rfind("Slot ", 0u) == 0u && name.size() > 5u && !asset(static_cast<uint32_t>(name[5] - 'A'))) return "LOAD SLOT";
        if (name == "Overdub Feedback" && paramNamed("Capture Mode") != 1.0) return "OVERDUB";
        if (name == "Ring Position" && path == 7) return "RAD MANUAL";
        if ((name == "Radial Ratio" || name == "Path Depth" || name == "Path Offset" || name == "Reverse Radial Path")
            && (path == 0 || path == 7)) return path == 7 ? "RAD MANUAL" : "FIXED";
        if (name == "Formation Spread") {
            if (path == 7) return "RAD MANUAL";
            if (formation == 3) return "FIELD 8";
        }
        if (name == "Relationship Amount" && (angular == 0 || angular == 7)) return angular == 7 ? "ANG MANUAL" : "UNISON";
        if (name == "Head Pivot" && angular != 2 && angular != 4) return "FAN / SHEAR";
        if (name == "Relationship Glide" && (angular < 2 || angular > 6)) return "NO RATE MOD";
        if (name == "Motion Seed" && path != 6 && angular != 6 && paramNamed("Head Drift") <= 0.0) return "RANDOM / DRIFT";
        if (name == "Ring Blend" && ringCount() <= 1u) return "1 RING";
        if ((name == "Loop Join" || name == "Seam Duck") && !ringCount()) return "NO SOURCE";
        if (name == "Seam Duck" && paramNamed("Loop Join") <= 0.0) return "JOIN 0";
        if (name == "Output Rotation" && paramNamed("Output Format") == 0.0) return "8 DIRECT";
        if (name == "MIDI Root" && paramNamed("MIDI Mode") == 0.0) return "MIDI OFF";
        return {};
    }

    const sample::SampleAsset* asset(uint32_t slot) const
    {
#if defined(_WIN32)
        if (config.visualization == SampleFamilyVisualization::Rings
            && config.callbacks.getWindowsOwnedAsset && slot < windowsRingsAssets.size()) {
            auto next = config.callbacks.getWindowsOwnedAsset(config.callbacks.context, slot);
            auto& cached = windowsRingsAssets[slot];
            if (cached.owner != next) {
                // Retain ownership so allocator address reuse cannot hit a stale
                // validation/path entry. Source replacement/clear invalidates all paths.
                for (auto& geometry : windowsRingsPaths) geometry = {};
                cached.owner = std::move(next);
                cached.valid = cached.owner && cached.owner->valid();
            }
            return cached.owner.get();
        }
#endif
        return config.callbacks.getAsset
            ? config.callbacks.getAsset(config.callbacks.context, slot)
            : nullptr;
    }

#if defined(_WIN32)
    bool windowsAssetValid(const sample::SampleAsset* current) const
    {
        if (!current) return false;
        if (config.visualization == SampleFamilyVisualization::Rings && config.callbacks.getWindowsOwnedAsset)
            for (const auto& cached : windowsRingsAssets)
                if (cached.owner.get() == current) return cached.valid;
        return current->valid();
    }
    mutable std::array<WindowsRingsAssetCache, 4u> windowsRingsAssets;
    mutable std::array<WindowsRingsPathCache, 32u> windowsRingsPaths;
#endif
    double param(uint32_t id) const
    {
        return config.callbacks.getParam
            ? config.callbacks.getParam(config.callbacks.context, id) : 0.0;
    }

    double paramNamed(const char* name, double fallback = 0.0) const
    {
        const std::size_t index = parameterIndex(name);
        return index < parameters.size() ? param(parameters[index].id)
                                         : fallback;
    }

    std::string paramTextNamed(const char* name,
        const char* fallback = "") const
    {
        const std::size_t index = parameterIndex(name);
        return index < parameters.size() ? uppercaseAscii(
            paramText(parameters[index])) : fallback;
    }

    void setNamedParameter(const char* name, double value)
    {
        const std::size_t index = parameterIndex(name);
        if (index < parameters.size()) {
            parameterEdit.perform(parameters[index].id, value);
            if (std::strcmp(name, "Selected Slot") != 0) markPresetEdited();
        }
    }

    const SampleFamilyParameterInfo* parameterInfo(uint32_t id) const
    {
        const auto found = std::find_if(parameters.begin(), parameters.end(),
            [id](const SampleFamilyParameterInfo& value) {
                return value.id == id;
            });
        return found == parameters.end() ? nullptr : &*found;
    }

    std::string paramText(const SampleFamilyParameterInfo& info) const
    {
        char buffer[64] {};
        const double value = param(info.id);
        if (config.callbacks.getParamText
            && config.callbacks.getParamText(config.callbacks.context,
                info.id, value, buffer,
                static_cast<uint32_t>(sizeof(buffer))))
            return parameterTextAlias(info, value, uppercaseAscii(buffer));
        std::snprintf(buffer, sizeof(buffer), "%.4g", value);
        return parameterTextAlias(info, value, uppercaseAscii(buffer));
    }

    std::string paramTextAt(
        const SampleFamilyParameterInfo& info, double value) const
    {
        char buffer[64] {};
        if (config.callbacks.getParamText
            && config.callbacks.getParamText(config.callbacks.context,
                info.id, value, buffer,
                static_cast<uint32_t>(sizeof(buffer))))
            return parameterTextAlias(info, value, uppercaseAscii(buffer));
        std::snprintf(buffer, sizeof(buffer), "%.4g", value);
        return parameterTextAlias(info, value, uppercaseAscii(buffer));
    }

    std::string parameterTextAlias(const SampleFamilyParameterInfo& info,
        double value, std::string fallback) const
    {
        if (config.visualization != SampleFamilyVisualization::Circulator)
            return fallback;
        if (std::strcmp(info.name, "Playing") == 0
            || std::strcmp(info.name, "Record") == 0
            || std::strcmp(info.name, "Loop 1 Reverse") == 0
            || std::strcmp(info.name, "Loop 2 Reverse") == 0)
            return value >= 0.5 ? "ON" : "OFF";
        if (std::strcmp(info.name, "Loop Model") == 0)
            return value >= 0.5 ? "DUAL SLOTS" : "CRCLTR CLASSIC";
        return fallback;
    }

    bool openParameterMenu(const SampleFamilyParameterInfo& info,
        const ParameterCell& cell)
    {
        if (!info.stepped) return false;
        if (config.callbacks.getParameterMenuItemCount
            && config.callbacks.getParameterMenuItem) {
            const uint32_t customCount = std::min<uint32_t>(256u,
                config.callbacks.getParameterMenuItemCount(
                    config.callbacks.context, info.id));
            if (customCount > 0u) {
                canvasMenuEntries.clear();
                canvasMenuEntries.reserve(customCount);
                for (uint32_t index = 0u; index < customCount; ++index) {
                    char label[64] {};
                    double value = 0.0;
                    if (!config.callbacks.getParameterMenuItem(
                            config.callbacks.context, info.id, index,
                            &value, label,
                            static_cast<uint32_t>(sizeof(label))))
                        continue;
                    canvasMenuEntries.push_back({
                        uppercaseAscii(label), value, index,
                    });
                }
                if (canvasMenuEntries.empty()) return false;
                canvasMenuSelected
                    = config.callbacks.getParameterMenuSelectedIndex
                    ? config.callbacks.getParameterMenuSelectedIndex(
                        config.callbacks.context, info.id) : -1;
                canvasMenuKind = CanvasMenuKind::Parameter;
                canvasMenuParameterId = info.id;
                canvasMenuHover = -1;
                layoutMenu(cell.control, false);
                invalid();
                return true;
            }
        }
        const int64_t first = static_cast<int64_t>(std::llround(info.minimum));
        const int64_t last = static_cast<int64_t>(std::llround(info.maximum));
        if (last < first || last - first > 255) return false;

        canvasMenuEntries.clear();
        canvasMenuEntries.reserve(static_cast<std::size_t>(
            last - first + 1));
        for (int64_t value = first; value <= last; ++value) {
            canvasMenuEntries.push_back({
                paramTextAt(info, static_cast<double>(value)),
                static_cast<double>(value),
                static_cast<uint32_t>(value - first),
            });
        }
        canvasMenuSelected = static_cast<int32_t>(std::clamp<int64_t>(
            static_cast<int64_t>(std::llround(param(info.id))) - first,
            0, last - first));
        canvasMenuKind = CanvasMenuKind::Parameter;
        canvasMenuParameterId = info.id;
        canvasMenuHover = -1;
        layoutMenu(cell.control, false);
        invalid();
        return true;
    }

    bool openPresetMenu()
    {
        if (!config.callbacks.getFactoryPresetCount
            || !config.callbacks.getFactoryPresetName
            || !config.callbacks.applyFactoryPreset) return false;
        const uint32_t count = std::min<uint32_t>(
            config.callbacks.getFactoryPresetCount(config.callbacks.context),
            128u);
        if (count == 0u) return false;
        canvasMenuEntries.clear();
        canvasMenuEntries.reserve(count);
        for (uint32_t index = 0u; index < count; ++index) {
            const char* name = config.callbacks.getFactoryPresetName(
                config.callbacks.context, index);
            canvasMenuEntries.push_back({
                uppercaseAscii(name && name[0] ? name : "PRESET"),
                static_cast<double>(index),
                index,
            });
        }
        canvasMenuKind = CanvasMenuKind::Preset;
        canvasMenuParameterId = 0u;
        canvasMenuSelected = selectedPreset;
        canvasMenuHover = -1;
        const auto band = gui_layout::encoderTitleBand({
            static_cast<double>(config.nativeWidth),
            static_cast<double>(config.nativeHeight),
        });
        layoutMenu(rect(band.presetMenu), true);
        invalid();
        return true;
    }

    void layoutMenu(const CRect& anchor, bool preset)
    {
        constexpr double rowHeight = 20.0;
        const uint32_t count = static_cast<uint32_t>(
            canvasMenuEntries.size());
        canvasMenuColumns = preset ? 1u
            : count > 32u ? 4u : count > 16u ? 2u : 1u;
        canvasMenuColumns = std::max(1u,
            std::min(canvasMenuColumns, count));
        const double roomBelow = std::max(0.0,
            static_cast<double>(config.nativeHeight) - 4.0 - anchor.bottom);
        const double roomAbove = std::max(0.0, anchor.top - 4.0);
        const double room = std::max(roomBelow, roomAbove);
        while (canvasMenuColumns < count
            && std::ceil(static_cast<double>(count) / canvasMenuColumns)
                    * rowHeight > room)
            ++canvasMenuColumns;
        canvasMenuRows = (count + canvasMenuColumns - 1u)
            / canvasMenuColumns;
        double columnWidth = std::max(88.0, anchor.getWidth());
        double width = columnWidth * canvasMenuColumns;
        const double maximumWidth = std::max(1.0,
            static_cast<double>(config.nativeWidth) - 8.0);
        if (width > maximumWidth) {
            width = maximumWidth;
            columnWidth = width / canvasMenuColumns;
        }
        const double height = rowHeight * canvasMenuRows;
        const double left = std::clamp(anchor.left, 4.0,
            std::max(4.0, static_cast<double>(config.nativeWidth)
                - 4.0 - width));
        const double top = roomBelow >= height
            ? anchor.bottom : std::max(4.0, anchor.top - height);
        canvasMenuBounds = rect(left, top, width, height);
        canvasMenuColumnWidth = columnWidth;
    }

    CRect menuItemRect(uint32_t index) const
    {
        constexpr double rowHeight = 20.0;
        const uint32_t column = canvasMenuRows > 0u
            ? index / canvasMenuRows : 0u;
        const uint32_t row = canvasMenuRows > 0u
            ? index % canvasMenuRows : 0u;
        return rect(canvasMenuBounds.left
                + column * canvasMenuColumnWidth,
            canvasMenuBounds.top + row * rowHeight,
            canvasMenuColumnWidth, rowHeight);
    }

    int32_t menuItemAt(const CPoint& point) const
    {
        if (canvasMenuKind == CanvasMenuKind::None
            || !contains(canvasMenuBounds, point)
            || canvasMenuRows == 0u || canvasMenuColumns == 0u)
            return -1;
        const uint32_t column = std::min(canvasMenuColumns - 1u,
            static_cast<uint32_t>((point.x - canvasMenuBounds.left)
                / canvasMenuColumnWidth));
        const uint32_t row = std::min(canvasMenuRows - 1u,
            static_cast<uint32_t>((point.y - canvasMenuBounds.top) / 20.0));
        const uint32_t index = column * canvasMenuRows + row;
        return index < canvasMenuEntries.size()
            ? static_cast<int32_t>(index) : -1;
    }

    void drawOpenMenu(CDrawContext& context)
    {
        if (canvasMenuKind == CanvasMenuKind::None
            || canvasMenuEntries.empty()) return;
        context.setFillColor(color(0x080808));
        context.drawRect(rect(canvasMenuBounds.left - 2.0,
            canvasMenuBounds.top - 2.0, canvasMenuBounds.getWidth() + 4.0,
            canvasMenuBounds.getHeight() + 4.0), kDrawFilled);
        context.setFillColor(color(0x151515));
        context.drawRect(canvasMenuBounds, kDrawFilled);
        context.setFrameColor(color(0x6c6c6c));
        context.drawRect(canvasMenuBounds, kDrawStroked);
        for (uint32_t index = 0u;
             index < canvasMenuEntries.size(); ++index) {
            const CRect row = menuItemRect(index);
            if (static_cast<int32_t>(index) == canvasMenuHover) {
                context.setFillColor(color(0x343434));
                context.drawRect(rect(row.left + 1.0, row.top + 1.0,
                    row.getWidth() - 2.0, row.getHeight() - 2.0),
                    kDrawFilled);
            } else if (static_cast<int32_t>(index)
                    == canvasMenuSelected) {
                context.setFillColor(color(0x292929));
                context.drawRect(rect(row.left + 1.0, row.top + 1.0,
                    row.getWidth() - 2.0, row.getHeight() - 2.0),
                    kDrawFilled);
            } else if ((index % canvasMenuRows) % 2u == 1u) {
                context.setFillColor(style.strip);
                context.drawRect(rect(row.left + 1.0, row.top + 1.0,
                    row.getWidth() - 2.0, row.getHeight() - 2.0),
                    kDrawFilled);
            }
            if (static_cast<int32_t>(index) == canvasMenuSelected
                || static_cast<int32_t>(index) == canvasMenuHover) {
                context.setFillColor(style.fill);
                context.drawRect(rect(row.left + 2.0, row.top + 2.0,
                    3.0, row.getHeight() - 4.0), kDrawFilled);
            }
            if (index % canvasMenuRows != 0u) {
                context.setFrameColor(color(0x3a3a3a));
                context.drawLine(CPoint(row.left, row.top),
                    CPoint(row.right, row.top));
            }
            text(context, canvasMenuEntries[index].label,
                row.left + 9.0, row.top + 4.0,
                row.getWidth() - 18.0, style.value);
        }
    }

    void closeMenu()
    {
        canvasMenuKind = CanvasMenuKind::None;
        canvasMenuHover = -1;
    }

    void markPresetEdited()
    {
        if (config.visualization == SampleFamilyVisualization::Rings) {
            if (presetName.empty()) presetName = "CUSTOM";
            if (presetName.back() != '*') presetName += "*";
        } else if (presetName != "CUSTOM") presetName = "CUSTOM";
        selectedPreset = -1;
    }

    void text(CDrawContext& context, const std::string& value,
        double x, double y, double width, CColor textColor,
        CHoriTxtAlign alignment = kLeftText, CFontRef overrideFont = nullptr)
    {
        foundation::drawTextLine(context, value, x, y, width, textColor,
            overrideFont ? overrideFont : font.get(), alignment);
    }

    double samplePanelHeight() const
    {
        switch (config.visualization) {
        case SampleFamilyVisualization::Rings: return 515.0;
        case SampleFamilyVisualization::Grains: return 430.0;
        case SampleFamilyVisualization::Wavesets:
        case SampleFamilyVisualization::Lanes:
        case SampleFamilyVisualization::Cutups: return 390.0;
        case SampleFamilyVisualization::Motion: return 370.0;
        case SampleFamilyVisualization::Doubles:
            return 310.0;
        case SampleFamilyVisualization::Circulator: return 350.0;
        default: return kSamplePanelHeight;
        }
    }

    bool isLaneFamily() const
    {
        return config.visualization == SampleFamilyVisualization::Lanes
            || config.visualization == SampleFamilyVisualization::Grains
            || config.visualization == SampleFamilyVisualization::Cutups;
    }

    double wavesetsVisibleSpan() const
    {
        return 1.0 / std::clamp(wavesetsWaveZoom, 1.0, 128.0);
    }

    bool hasScrollableWaveform() const
    {
        return config.visualization == SampleFamilyVisualization::Wavesets
            || config.visualization == SampleFamilyVisualization::Motion;
    }

    void resetWavesetsWaveView()
    {
        wavesetsWaveZoom = 1.0;
        wavesetsViewStart = 0.0;
    }

    void resetWavesetsView()
    {
        resetWavesetsWaveView();
        wavesetsScopeScale = 2.0;
    }

    bool waveformValueVisible(double normalized) const
    {
        if (!hasScrollableWaveform())
            return true;
        return normalized >= wavesetsViewStart
            && normalized <= wavesetsViewStart + wavesetsVisibleSpan();
    }

    double waveformXForNormalized(
        const CRect& lane, double normalized) const
    {
        if (!hasScrollableWaveform())
            return lane.left + normalized * lane.getWidth();
        return lane.left + (normalized - wavesetsViewStart)
            / wavesetsVisibleSpan() * lane.getWidth();
    }

    double waveformNormalizedAtX(const CRect& lane, double x) const
    {
        const double local = std::clamp(
            (x - lane.left) / lane.getWidth(), 0.0, 1.0);
        if (!hasScrollableWaveform())
            return local;
        return std::clamp(wavesetsViewStart
            + local * wavesetsVisibleSpan(), 0.0, 1.0);
    }

    bool hasAuxiliaryVisualization() const
    {
        return config.visualization != SampleFamilyVisualization::Waveform
            && config.visualization != SampleFamilyVisualization::Doubles
            && config.visualization != SampleFamilyVisualization::Circulator
            && config.visualization != SampleFamilyVisualization::Rings;
    }

    bool hasPathVisualization() const
    {
        return config.visualization == SampleFamilyVisualization::Lanes
            || config.visualization == SampleFamilyVisualization::Grains
            || config.visualization == SampleFamilyVisualization::Cutups;
    }

    CRect samplePanelRect() const
    {
        switch (config.visualization) {
        case SampleFamilyVisualization::Doubles:
            return rect(18.0, 42.0, 1004.0, 252.0);
        case SampleFamilyVisualization::Wavesets:
        case SampleFamilyVisualization::Motion:
            return rect(18.0, 54.0, 944.0, 272.0);
        case SampleFamilyVisualization::Lanes:
        case SampleFamilyVisualization::Grains:
        case SampleFamilyVisualization::Cutups:
            return rect(18.0, 54.0, 850.0, 296.0);
        case SampleFamilyVisualization::Rings:
            return rect(18.0, 42.0, 820.0, 680.0);
        case SampleFamilyVisualization::Circulator:
            return rect(18.0, 42.0, 724.0, 142.0);
        default:
            return rect(kOuterInset, kPanelTop,
                config.nativeWidth - 2.0 * kOuterInset,
                samplePanelHeight());
        }
    }

    CRect waveformRect() const
    {
        switch (config.visualization) {
        case SampleFamilyVisualization::Doubles:
            return rect(30.0, 70.0, 980.0, 184.0);
        case SampleFamilyVisualization::Wavesets:
        case SampleFamilyVisualization::Motion:
            return rect(30.0, 82.0, 920.0, 200.0);
        case SampleFamilyVisualization::Lanes:
        case SampleFamilyVisualization::Grains:
        case SampleFamilyVisualization::Cutups:
            return rect(118.0, 89.0, 664.0, 47.0);
        case SampleFamilyVisualization::Rings:
            return rect(18.0, 42.0, 820.0, 680.0);
        case SampleFamilyVisualization::Circulator:
            return rect(120.0, 66.0, 528.0, 102.0);
        default:
            return rect(kOuterInset + 12.0, kPanelTop + 31.0,
                config.nativeWidth - 2.0 * (kOuterInset + 12.0), 178.0);
        }
    }

    CRect visualizationRect() const
    {
        switch (config.visualization) {
        case SampleFamilyVisualization::Wavesets:
            return rect(18.0, 326.0, 944.0, 100.0);
        case SampleFamilyVisualization::Motion:
            return rect(18.0, 326.0, 944.0, 100.0);
        case SampleFamilyVisualization::Lanes:
        case SampleFamilyVisualization::Grains:
        case SampleFamilyVisualization::Cutups:
            return rect(18.0, 362.0, 850.0, 180.0);
        case SampleFamilyVisualization::Rings:
            return waveformRect();
        case SampleFamilyVisualization::Circulator:
            return rect(48.0, 70.0, 48.0, 94.0);
        default: {
            const CRect wave = waveformRect();
            return rect(wave.left, wave.bottom + 8.0, wave.getWidth(),
                std::max(0.0, samplePanelHeight() - 279.0));
        }
        }
    }

    CRect visualizationGraphRect() const
    {
        switch (config.visualization) {
        case SampleFamilyVisualization::Wavesets:
            return rect(170.0, 336.0, 780.0, 80.0);
        case SampleFamilyVisualization::Motion:
            return rect(228.0, 336.0, 722.0, 80.0);
        case SampleFamilyVisualization::Lanes:
        case SampleFamilyVisualization::Grains:
        case SampleFamilyVisualization::Cutups:
            return rect(218.0, 390.0, 638.0, 138.0);
        default:
            return visualizationRect();
        }
    }

    CRect loadButtonRect() const
    {
        switch (config.visualization) {
        case SampleFamilyVisualization::Doubles:
            return rect(818.0, 45.0, 86.0, 15.0);
        case SampleFamilyVisualization::Wavesets:
            return rect(662.0, 59.0, 112.0, 15.0);
        case SampleFamilyVisualization::Motion:
            return rect(674.0, 59.0, 108.0, 15.0);
        case SampleFamilyVisualization::Rings:
            return rect(1106.0, 630.0, 66.0, 19.0);
        case SampleFamilyVisualization::Circulator:
            return rect(602.0, 45.0, 62.0, 15.0);
        default:
            return rect(790.0, 91.0, 52.0, 16.0);
        }
    }

    CRect clearButtonRect() const
    {
        switch (config.visualization) {
        case SampleFamilyVisualization::Doubles:
            return rect(0.0, 0.0, 0.0, 0.0);
        case SampleFamilyVisualization::Wavesets:
            return rect(782.0, 59.0, 56.0, 15.0);
        case SampleFamilyVisualization::Motion:
            return rect(790.0, 59.0, 52.0, 15.0);
        case SampleFamilyVisualization::Rings:
            return rect(1178.0, 630.0, 66.0, 19.0);
        default:
            return rect(790.0, 118.0, 52.0, 16.0);
        }
    }

    CRect storageButtonRect() const
    {
        switch (config.visualization) {
        case SampleFamilyVisualization::Doubles:
            return rect(912.0, 45.0, 98.0, 15.0);
        case SampleFamilyVisualization::Wavesets:
            return rect(846.0, 59.0, 104.0, 15.0);
        case SampleFamilyVisualization::Motion:
            return rect(850.0, 59.0, 100.0, 15.0);
        case SampleFamilyVisualization::Lanes:
        case SampleFamilyVisualization::Grains:
        case SampleFamilyVisualization::Cutups:
            return rect(744.0, 60.0, 112.0, 16.0);
        case SampleFamilyVisualization::Rings:
            return rect(470.0, 46.0, 130.0, 19.0);
        default:
            return rect(config.nativeWidth - 194.0, kPanelTop + 3.0,
                164.0, 16.0);
        }
    }

    CRect slotButtonRect(uint32_t slot, uint32_t count) const
    {
        if (isLaneFamily())
            return rect(28.0, 84.0 + slot * 63.0, 84.0, 57.0);
        if (config.visualization == SampleFamilyVisualization::Rings)
            return rect(42.0 + slot * 196.0, 691.0, 185.0, 22.0);
        if (config.visualization == SampleFamilyVisualization::Circulator)
            return rect(28.0, 66.0 + slot * 54.0, 16.0, 48.0);
        if (count == 1u) return rect(0.0, 0.0, 0.0, 0.0);
        const double available = std::max(80.0,
            loadButtonRect().left - (kOuterInset + 118.0));
        const double width = std::min(54.0,
            (available - std::max(0u, count - 1u) * 4.0)
                / static_cast<double>(count));
        return rect(kOuterInset + 104.0 + slot * (width + 4.0),
            kPanelTop + 3.0, width, 16.0);
    }

    CRect sampleLoadButtonRect(uint32_t slot) const
    {
        if (isLaneFamily())
            return rect(790.0, 91.0 + slot * 63.0, 52.0, 16.0);
        if (config.visualization == SampleFamilyVisualization::Circulator)
            return rect(602.0 + slot * 68.0, 45.0, 62.0, 15.0);
        if (config.visualization == SampleFamilyVisualization::Rings)
            return loadButtonRect();
        return slot == 0u ? loadButtonRect()
                          : rect(0.0, 0.0, 0.0, 0.0);
    }

    CRect sampleClearButtonRect(uint32_t slot) const
    {
        if (isLaneFamily())
            return rect(790.0, 118.0 + slot * 63.0, 52.0, 16.0);
        if (config.visualization == SampleFamilyVisualization::Rings)
            return clearButtonRect();
        if (config.visualization == SampleFamilyVisualization::Circulator)
            return rect(0.0, 0.0, 0.0, 0.0);
        return slot == 0u ? clearButtonRect()
                          : rect(0.0, 0.0, 0.0, 0.0);
    }

    CRect actionButtonRect(uint32_t index) const
    {
        if (config.visualization == SampleFamilyVisualization::Doubles
            && index < config.actionCount) {
            const char* label = config.actions[index].label
                ? config.actions[index].label : "";
            if (std::strcmp(label, "RESTART") == 0)
                return rect(30.0, 558.0, 104.0, 28.0);
            if (std::strcmp(label, "PLAY") == 0)
                return rect(142.0, 558.0, 86.0, 28.0);
            if (std::strcmp(label, "STOP") == 0)
                return rect(236.0, 558.0, 86.0, 28.0);
            if (std::strcmp(label, "DECK A") == 0)
                return rect(338.0, 558.0, 100.0, 28.0);
            if (std::strcmp(label, "DECK B") == 0)
                return rect(446.0, 558.0, 100.0, 28.0);
            if (std::strcmp(label, "SYNC") == 0)
                return rect(812.0, 558.0, 86.0, 28.0);
            if (std::strcmp(label, "STEP -") == 0)
                return rect(718.0, 558.0, 86.0, 28.0);
            if (std::strcmp(label, "STEP +") == 0)
                return rect(906.0, 558.0, 104.0, 28.0);
            if (std::strcmp(label, "PUNCH A") == 0)
                return rect(30.0, 602.0, 110.0, 42.0);
            if (std::strcmp(label, "DRAG A") == 0)
                return rect(148.0, 602.0, 96.0, 42.0);
            if (std::strcmp(label, "SET CUE A") == 0)
                return rect(252.0, 602.0, 96.0, 42.0);
            if (std::strcmp(label, "TRIGGER A") == 0)
                return rect(356.0, 602.0, 108.0, 42.0);
            if (std::strcmp(label, "TRIGGER B") == 0)
                return rect(576.0, 602.0, 108.0, 42.0);
            if (std::strcmp(label, "SET CUE B") == 0)
                return rect(692.0, 602.0, 96.0, 42.0);
            if (std::strcmp(label, "DRAG B") == 0)
                return rect(796.0, 602.0, 90.0, 42.0);
            if (std::strcmp(label, "PUNCH B") == 0)
                return rect(894.0, 602.0, 116.0, 42.0);
        }
        if (config.visualization == SampleFamilyVisualization::Wavesets
            || config.visualization == SampleFamilyVisualization::Motion) {
            return index == 0u ? rect(662.0, 744.0, 126.0, 30.0)
                               : rect(798.0, 744.0, 144.0, 30.0);
        }
        if (config.visualization == SampleFamilyVisualization::Rings) {
            switch (index) {
            case 0u: return rect(606.0, 46.0, 52.0, 19.0);
            case 1u: return rect(664.0, 46.0, 58.0, 19.0);
            case 2u: return rect(728.0, 46.0, 48.0, 19.0);
            case 3u: return rect(782.0, 46.0, 44.0, 19.0);
            case 4u: return rect(1238.0, 46.0, 88.0, 19.0);
            case 5u: return rect(1158.0, 190.0, 80.0, 19.0);
            case 6u: return rect(1246.0, 190.0, 80.0, 19.0);
            default: return rect(1250.0, 630.0, 76.0, 19.0);
            }
        }
        if (isLaneFamily()) {
            if (config.visualization == SampleFamilyVisualization::Cutups
                && index == 2u)
                return rect(1088.0, 60.0, 162.0, 16.0);
            const double y = config.visualization
                    == SampleFamilyVisualization::Grains
                ? 744.0 : config.visualization
                    == SampleFamilyVisualization::Cutups
                ? 688.0 : 560.0;
            if (index == 0u)
                return rect(config.visualization
                        == SampleFamilyVisualization::Lanes
                    ? 590.0 : 974.0, y, 126.0, 16.0);
            return rect(config.visualization
                    == SampleFamilyVisualization::Lanes
                ? 724.0 : 1108.0, y,
                config.visualization == SampleFamilyVisualization::Lanes
                    ? 132.0 : 142.0, 16.0);
        }
        if (config.visualization == SampleFamilyVisualization::Circulator) {
            switch (index) {
            case 0u: return rect(118.0, 224.0, 240.0, 24.0);
            case 1u: return rect(118.0, 368.0, 240.0, 24.0);
            case 2u: return rect(126.0, 505.0, 111.0, 15.0);
            default: return rect(243.0, 505.0, 111.0, 15.0);
            }
        }
        const double gap = 7.0;
        const double available = config.nativeWidth
            - 2.0 * (kOuterInset + 12.0);
        const double width = config.actionCount == 0u ? 0.0
            : (available - gap * (config.actionCount - 1u))
                / static_cast<double>(config.actionCount);
        return rect(kOuterInset + 12.0 + index * (width + gap),
            kPanelTop + samplePanelHeight() - 26.0, width, 18.0);
    }

    CRect waveformSlotRect(uint32_t slot) const
    {
        if (isLaneFamily()) {
            return rect(118.0, 89.0 + slot * 63.0, 664.0, 47.0);
        }
        if (config.visualization == SampleFamilyVisualization::Circulator) {
            return rect(120.0, 66.0 + slot * 54.0, 528.0, 48.0);
        }
        if (config.visualization == SampleFamilyVisualization::Rings)
            return waveformRect();
        const CRect wave = waveformRect();
        const uint32_t slots = sampleSlotCount();
        const double height = wave.getHeight() / static_cast<double>(slots);
        return rect(wave.left, wave.top + slot * height,
            wave.getWidth(), height);
    }

    uint32_t waveformSlotAt(const CPoint& point) const
    {
        if (config.visualization == SampleFamilyVisualization::Rings) {
            for (uint32_t slot = 0; slot < sampleSlotCount(); ++slot)
                if (contains(slotButtonRect(slot, sampleSlotCount()), point)) return slot;
            return selectedSlot;
        }
        if (isLaneFamily()) {
            for (uint32_t slot = 0u;
                 slot < std::min(4u, sampleSlotCount()); ++slot)
                if (contains(rect(28.0, 84.0 + slot * 63.0, 826.0, 57.0), point)) return slot;
            return selectedSlot;
        }
        const CRect wave = waveformRect();
        const uint32_t slots = sampleSlotCount();
        if (!contains(wave, point)) return selectedSlot;
        const double relative = std::clamp(
            (point.y - wave.top) / wave.getHeight(), 0.0, 0.999999);
        return std::min(slots - 1u,
            static_cast<uint32_t>(relative * slots));
    }

    bool hasOutputRoutingControls() const
    {
        if (!isLaneFamily()
            && config.visualization != SampleFamilyVisualization::Motion)
            return false;
        const uint32_t channels = config.callbacks.getOutputChannelCount
            ? config.callbacks.getOutputChannelCount(config.callbacks.context)
            : 2u;
        if (channels <= 2u) return false;
        return parameterIndex(config.visualization
                    == SampleFamilyVisualization::Motion
                ? "Output Order" : "Output Mode") < parameters.size();
    }

    CRect outputPageButtonRect() const
    {
        if (!hasOutputRoutingControls()) return rect(0.0, 0.0, 0.0, 0.0);
        if (config.visualization == SampleFamilyVisualization::Motion)
            return rect(834.0, 448.0, 116.0, 16.0);
        if (config.visualization == SampleFamilyVisualization::Grains)
            return rect(850.0, 744.0, 116.0, 16.0);
        if (config.visualization == SampleFamilyVisualization::Cutups)
            return rect(850.0, 688.0, 116.0, 16.0);
        return rect(466.0, 560.0, 116.0, 16.0);
    }

    void drawTitleBand(CDrawContext& context)
    {
        const auto band = gui_layout::encoderTitleBand({
            static_cast<double>(config.nativeWidth),
            static_cast<double>(config.nativeHeight),
        });
        foundation::drawPluginTitle(context,
            config.pluginName ? config.pluginName : "s3g Sample",
            rect(band.titleX, band.titleY - 2.0,
                band.presetLabelX - band.titleX - 8.0, 15.0), titleFont);
        text(context, "PRESET", band.presetLabelX, band.controlY + 1.0,
            band.presetMenu.x - band.presetLabelX - 4.0, style.label);
        foundation::drawMenuBox(context, rect(band.presetMenu),
            presetName, font);
        foundation::drawButton(context, rect(band.loadButton),
            "LOAD", font);
        foundation::drawButton(context, rect(band.saveButton),
            "SAVE", font);
        const float peak = config.callbacks.getOutputPeak
            ? config.callbacks.getOutputPeak(config.callbacks.context) : 0.0f;
        char peakText[32] {};
        std::snprintf(peakText, sizeof(peakText), "PK %+.1f",
            20.0 * std::log10(std::max(0.000001f, peak)));
        context.setFont(font);
        const double width = context.getStringWidth(peakText);
        text(context, peakText, config.nativeWidth - width
            - band.statusRightInset, band.titleY, width + 1.0, style.value);
    }

    void drawSamplePanel(CDrawContext& context)
    {
        if (config.visualization == SampleFamilyVisualization::Doubles) {
            drawDoublesSource(context);
            return;
        }
        if (config.visualization == SampleFamilyVisualization::Wavesets
            || config.visualization == SampleFamilyVisualization::Motion) {
            drawSingleSource(context);
            drawAuxiliaryVisualization(context);
            return;
        }
        if (isLaneFamily()) {
            drawLaneSources(context);
            drawAuxiliaryVisualization(context);
            return;
        }
        if (config.visualization == SampleFamilyVisualization::Rings) {
            drawRingsSource(context);
            return;
        }
        if (config.visualization == SampleFamilyVisualization::Circulator) {
            drawCirculatorSource(context);
            return;
        }
        const CRect panel = samplePanelRect();
        foundation::drawPanel(context, panel,
            uppercaseAscii(config.samplePanelName
                ? config.samplePanelName : "SAMPLE"), font);
        const uint32_t slots = sampleSlotCount();
        for (uint32_t slot = 0u; slot < slots; ++slot) {
            char label[24] {};
            std::snprintf(label, sizeof(label), slots == 1u
                ? "SOURCE" : "SLOT %c", static_cast<int>('A' + slot));
            foundation::drawButton(context, slotButtonRect(slot, slots),
                label, font, slot == selectedSlot);
        }
        foundation::drawButton(context, loadButtonRect(), "LOAD SAMPLE",
            font);
        foundation::drawButton(context, clearButtonRect(), "CLEAR", font);
        std::string storage = "STORE ";
        if (config.callbacks.getStorageModeName) {
            const char* mode = config.callbacks.getStorageModeName(
                config.callbacks.context, selectedSlot);
            if (mode) storage += mode;
        } else {
            storage += "PROJECT";
        }
        foundation::drawButton(context, storageButtonRect(), storage, font);
        if (config.visualization == SampleFamilyVisualization::Rings)
            drawRingField(context);
        else {
            drawWaveforms(context);
            drawAuxiliaryVisualization(context);
            if (config.visualization
                == SampleFamilyVisualization::Circulator)
                drawCirculatorCrossfade(context);
        }

        const char* status = config.callbacks.getSampleStatus
            ? config.callbacks.getSampleStatus(config.callbacks.context,
                selectedSlot) : "";
        const char* path = config.callbacks.getSamplePath
            ? config.callbacks.getSamplePath(config.callbacks.context,
                selectedSlot) : "";
        std::string detail = status ? status : "";
        if (detail.empty() && path) detail = path;
        text(context, detail, kOuterInset + 12.0,
            kPanelTop + samplePanelHeight() - 51.0,
            config.nativeWidth - 2.0 * (kOuterInset + 12.0), style.value);

        for (uint32_t action = 0u; action < config.actionCount; ++action) {
            const bool active = heldAction == action + 1u;
            foundation::drawButton(context, actionButtonRect(action),
                uppercaseAscii(config.actions[action].label
                    ? config.actions[action].label : ""),
                font, active);
        }
    }

    std::string storageLabel(uint32_t slot) const
    {
        std::string result = "STORE ";
        if (config.callbacks.getStorageModeName) {
            const char* mode = config.callbacks.getStorageModeName(
                config.callbacks.context, slot);
            result += mode ? uppercaseAscii(mode) : "PROJECT";
        } else {
            result += "PROJECT";
        }
        return result;
    }

    std::string sampleDetail(uint32_t slot) const
    {
        const char* status = config.callbacks.getSampleStatus
            ? config.callbacks.getSampleStatus(config.callbacks.context, slot)
            : "";
        const char* path = config.callbacks.getSamplePath
            ? config.callbacks.getSamplePath(config.callbacks.context, slot)
            : "";
        std::string result = status ? status : "";
        if (result.empty() && path) result = path;
        return uppercaseAscii(result);
    }

    void drawSingleSource(CDrawContext& context)
    {
        const CRect panel = samplePanelRect();
        foundation::drawPanel(context, panel, "SAMPLE", font);
        foundation::drawButton(context, loadButtonRect(), "LOAD SAMPLE", font);
        foundation::drawButton(context, clearButtonRect(), "CLEAR", font);
        foundation::drawButton(context, storageButtonRect(), storageLabel(0u),
            font);
        text(context, sampleDetail(0u), 90.0, 59.0,
            config.visualization == SampleFamilyVisualization::Motion
                ? 576.0 : 558.0,
            style.value);
        const CRect wave = waveformRect();
        context.setFillColor(style.strip);
        context.drawRect(wave, kDrawFilled);
        context.setFrameColor(style.grid);
        context.drawRect(wave, kDrawStroked);
        if (config.visualization == SampleFamilyVisualization::Motion) {
            const double start = paramNamed("Start");
            const double end = paramNamed("End", 1.0);
            const double locus = std::clamp(paramNamed("Locus", 0.5),
                start, end);
            const double half = paramNamed("Field", 0.25) * 0.5;
            const double left = std::max(start, locus - half);
            const double right = std::min(end, locus + half);
            const double x1 = std::clamp(waveformXForNormalized(wave, left),
                wave.left, wave.right);
            const double x2 = std::clamp(waveformXForNormalized(wave, right),
                wave.left, wave.right);
            if (x2 > x1) {
                context.setFillColor(alphaColor(0xd79a55, 23));
                context.drawRect(rect(x1, wave.top, x2 - x1,
                    wave.getHeight()), kDrawFilled);
            }
        }
        drawWaveform(context, wave, 0u, -1);
        drawMarkers(context, wave);
        if (config.visualization == SampleFamilyVisualization::Motion) {
            const double start = paramNamed("Start");
            const double end = paramNamed("End", 1.0);
            const double locus = std::clamp(paramNamed("Locus", 0.5),
                start, end);
            const double half = paramNamed("Field", 0.25) * 0.5;
            context.setFrameColor(style.dim);
            context.setLineWidth(0.75);
            for (const double edge : {
                    std::max(start, locus - half),
                    std::min(end, locus + half) }) {
                if (!waveformValueVisible(edge)) continue;
                const double x = waveformXForNormalized(wave, edge);
                context.drawLine(CPoint(x, wave.top),
                    CPoint(x, wave.bottom));
            }
        }
#if defined(_WIN32)
        if (const auto* current = asset(0u); current && windowsAssetValid(current)) {
#else
        if (const auto* current = asset(0u); current && current->valid()) {
#endif
            uint32_t activeVoices = 0u;
            if (config.callbacks.getMotionScopeState) {
                SampleFamilyMotionScopeState state {};
                config.callbacks.getMotionScopeState(config.callbacks.context, &state);
                activeVoices = state.activeVoices;
            } else if (config.callbacks.getCursorStates) {
                std::array<SampleFamilyCursorState, 64u> states {};
                activeVoices = config.callbacks.getCursorStates(config.callbacks.context,
                    0u, states.data(), 64u);
            }
            char metadata[192] {};
            std::snprintf(metadata, sizeof(metadata),
                "%u VOICES  /  %u CH  /  %u FRAMES  /  %.0f HZ  /  %s", activeVoices,
                static_cast<unsigned>(current->channelCount),
                current->frameCount(), current->sampleRate,
                sampleDetail(0u).c_str());
            text(context, metadata, 30.0, 289.0, 920.0, style.value,
                kLeftText, tinyFont);
        }
        char markerHelp[160] {};
        std::snprintf(markerHelp, sizeof(markerHelp),
            config.visualization == SampleFamilyVisualization::Motion
                ? "DRAG S / E / L   SCROLL ZOOM   SHIFT-SCROLL PAN   DOUBLE-CLICK FIT   ZOOM %.1fX"
                : "DRAG S / E / LS / LE   SCROLL ZOOM   SHIFT-SCROLL PAN   DOUBLE-CLICK FIT   ZOOM %.1fX",
            wavesetsWaveZoom);
        text(context, markerHelp,
            30.0, 307.0, 920.0, style.dim, kLeftText, tinyFont);
    }

    void drawDoublesSource(CDrawContext& context)
    {
        SampleFamilyDoublesState state {};
        if (config.callbacks.getDoublesState)
            config.callbacks.getDoublesState(config.callbacks.context, &state);
        const CRect panel = samplePanelRect();
        foundation::drawPanel(context, panel,
            "SHARED SAMPLE / TWO READ HEADS", font);
        foundation::drawButton(context, loadButtonRect(), "LOAD SAMPLE", font);
        foundation::drawButton(context, storageButtonRect(), storageLabel(0u),
            font);
        const CRect wave = waveformRect();
        context.setFillColor(style.strip);
        context.drawRect(wave, kDrawFilled);
        context.setFrameColor(style.grid);
        context.drawRect(wave, kDrawStroked);
        const double deckHeight = wave.getHeight() * 0.5;
        const CRect deckA = rect(wave.left, wave.top, wave.getWidth(),
            deckHeight);
        const CRect deckB = rect(wave.left, deckA.bottom, wave.getWidth(),
            deckHeight);
        drawWaveform(context, deckA, 0u, 0);
        drawWaveform(context, deckB, 0u, 1);
        context.setFrameColor(style.grid);
        context.drawLine(CPoint(wave.left, deckB.top),
            CPoint(wave.right, deckB.top));
        text(context, "DECK A", deckA.left + 6.0, deckA.top + 4.0,
            70.0, color(0x69d2dc), kLeftText, tinyFont);
        text(context, "DECK B", deckB.left + 6.0, deckB.top + 4.0,
            70.0, color(0xff7047), kLeftText, tinyFont);
        drawMarkers(context, deckA);
        for (uint32_t deck = 0u; deck < 2u; ++deck) {
            if ((state.cueMask & (1u << deck)) == 0u || state.cues[deck] < 0.0f) continue;
            const CRect lane = deck == 0u ? deckA : deckB;
            const double x = lane.left + state.cues[deck] * lane.getWidth();
            const CColor cueColor = color(deck == 0u ? 0x69d2dc : 0xff7047);
            context.setFrameColor(cueColor);
            context.setLineWidth(1.0);
            for (double y = lane.top; y < lane.bottom; y += 6.0)
                context.drawLine(CPoint(x, y), CPoint(x, std::min(y + 3.0, lane.bottom)));
            auto flag = owned(context.createGraphicsPath());
            if (flag) {
                flag->beginSubpath(x, lane.top);
                flag->addLine(x + 7.0, lane.top);
                flag->addLine(x, lane.top + 7.0);
                flag->closeSubpath();
                context.setFillColor(cueColor);
                context.drawGraphicsPath(flag, CDrawContext::kPathFilled);
            }
        }
        const char* status = config.callbacks.getSampleStatus
            ? config.callbacks.getSampleStatus(config.callbacks.context, 0u) : "";
        text(context, status ? status : "", 30.0, 264.0, 320.0, style.dim, kLeftText, tinyFont);
        text(context, state.tempoText, 354.0, 264.0, 206.0, style.text, kRightText, tinyFont);
        text(context, state.storageText, 728.0, 264.0, 282.0, style.dim, kRightText, tinyFont);
        text(context, state.playing ? "PLAYING" : "STOPPED", 694.0, 46.0,
            108.0, style.dim, kRightText, tinyFont);
    }

    void drawLaneSources(CDrawContext& context)
    {
        const char* title = config.visualization
                == SampleFamilyVisualization::Grains
            ? "FOUR SAMPLE LANES / SHARED SOURCE WINDOW"
            : config.visualization == SampleFamilyVisualization::Cutups
            ? "FOUR CUT SOURCES / TRANSIENTS + FILE TEMPO ANALYSED ON LOAD"
            : "FOUR LOOP LANES / SHARED START + END";
        foundation::drawPanel(context, samplePanelRect(), title, font);
        foundation::drawButton(context, storageButtonRect(), storageLabel(0u),
            font);
        const uint32_t slots = std::min(4u, sampleSlotCount());
        for (uint32_t slot = 0u; slot < slots; ++slot) {
            const CRect row = rect(28.0, 84.0 + slot * 63.0, 828.0, 57.0);
            context.setFillColor(slot == selectedSlot
                    ? color(0x202020) : color(0x191919));
            context.drawRect(row, kDrawFilled);
            context.setFrameColor(style.grid);
            context.drawRect(row, kDrawStroked);
            char laneLabel[24] {};
            std::snprintf(laneLabel, sizeof(laneLabel), "LANE %u", slot + 1u);
            text(context, laneLabel, row.left + 7.0, row.top + 6.0,
                78.0, style.label, kLeftText, tinyFont);
            text(context, sampleDetail(slot), row.left + 7.0, row.top + 24.0,
                78.0, style.dim, kLeftText, tinyFont);
            const CRect wave = waveformSlotRect(slot);
            context.setFillColor(style.strip);
            context.drawRect(wave, kDrawFilled);
            context.setFrameColor(style.grid);
            context.drawRect(wave, kDrawStroked);
            context.setFrameColor(color(0x343434));
            context.drawLine(CPoint(wave.left, wave.getCenter().y),
                CPoint(wave.right, wave.getCenter().y));
            drawWaveform(context, wave, slot, -1);
            drawLaneWaveOverlay(context, wave, slot);
            foundation::drawButton(context, sampleLoadButtonRect(slot),
                "LOAD", font);
            foundation::drawButton(context, sampleClearButtonRect(slot),
                "CLEAR", font);
        }
    }

    float grainWindow(float phase, float skew, int envelope) const
    {
        constexpr float pi = 3.14159265358979323846f;
        phase = std::clamp(phase, 0.0f, 1.0f);
        const float peak = 0.5f + 0.4f * std::clamp(skew, -1.0f, 1.0f);
        phase = phase <= peak ? 0.5f * phase / peak
            : 0.5f + 0.5f * (phase - peak) / (1.0f - peak);
        switch (envelope) {
        case 1: return std::sin(pi * phase);
        case 2: return 0.5f - 0.5f * std::cos(2.0f * pi * phase);
        case 3: return 1.0f - std::abs(2.0f * phase - 1.0f);
        case 4: {
            const float value = (phase - 0.5f) / 0.18f;
            return std::exp(-0.5f * value * value);
        }
        default: {
            const float triangle = 1.0f
                - std::abs(2.0f * phase - 1.0f);
            return triangle * triangle * (3.0f - 2.0f * triangle);
        }
        }
    }

    void drawLaneWaveOverlay(CDrawContext& context, const CRect& wave,
        uint32_t slot)
    {
        const double start = paramNamed("Start");
        const double end = paramNamed("End", 1.0);
        const double span = std::max(0.0, end - start);
        if (config.visualization == SampleFamilyVisualization::Cutups) {
            context.setFrameColor(alphaColor(0x7fa397, 108));
            context.setLineWidth(0.7);
            if (paramNamed("Regions") < 0.5 && asset(slot) && span > 0.0) {
                const uint32_t count = std::max(1u,
                    static_cast<uint32_t>(std::lround(
                        paramNamed("Steps / Regions", 16.0))));
                for (uint32_t index = 1u; index < count; ++index) {
                    const double x = wave.left + (start + span * index
                        / static_cast<double>(count)) * wave.getWidth();
                    context.drawLine(CPoint(x, wave.top + 1.0),
                        CPoint(x, wave.bottom - 1.0));
                }
            } else {
                std::array<SampleFamilyVisualPoint, 256u> transients {};
                const uint32_t count = visualPoints(20u + slot, transients);
                const uint32_t maximum = std::min(count, std::max(1u,
                    static_cast<uint32_t>(std::lround(
                        paramNamed("Steps / Regions", 16.0)))));
                for (uint32_t index = 0u; index < maximum; ++index) {
                    const double position = transients[index].x;
                    if (position <= start || position >= end) continue;
                    const double x = wave.left + position * wave.getWidth();
                    context.drawLine(CPoint(x, wave.top + 1.0),
                        CPoint(x, wave.bottom - 1.0));
                }
            }
        } else if (config.visualization == SampleFamilyVisualization::Grains
            && span > 0.0) {
            std::array<SampleFamilyVisualPoint, 256u> grains {};
            const uint32_t count = visualPoints(10u + slot, grains);
            const int envelope = static_cast<int>(std::lround(
                paramNamed("Grain Envelope")));
            const float skew = static_cast<float>(paramNamed(
                "Envelope Skew"));
            for (uint32_t grain = 0u; grain < count; ++grain) {
                const auto& event = grains[grain];
                const float currentPhase = std::clamp(event.y, 0.0f, 1.0f);
                const double weight = std::clamp(event.kind / 65535.0,
                    0.0, 1.0);
                const double lineWidth = 0.75 + 1.65 * std::sqrt(weight);
                const auto contour = [&](float first, float last,
                                           CColor lineColor) {
                    auto upper = owned(context.createGraphicsPath());
                    auto lower = owned(context.createGraphicsPath());
                    if (!upper || !lower || !(last > first)) return;
                    double previousX = 0.0;
                    for (uint32_t point = 0u; point <= 28u; ++point) {
                        const float phase = first + (last - first)
                            * point / 28.0f;
                        double source = event.x + event.width * phase;
                        source -= std::floor(source);
                        const double x = wave.left + source * wave.getWidth();
                        const double amplitude = (4.0 + 8.0 * weight)
                            * grainWindow(phase, skew, envelope);
                        const bool begin = point == 0u
                            || std::abs(x - previousX) > wave.getWidth() * 0.5;
                        if (begin) {
                            upper->beginSubpath(x, wave.getCenter().y
                                - amplitude);
                            lower->beginSubpath(x, wave.getCenter().y
                                + amplitude);
                        } else {
                            upper->addLine(x, wave.getCenter().y - amplitude);
                            lower->addLine(x, wave.getCenter().y + amplitude);
                        }
                        previousX = x;
                    }
                    context.setFrameColor(lineColor);
                    context.setLineWidth(lineWidth);
                    context.drawGraphicsPath(upper,
                        CDrawContext::kPathStroked);
                    context.drawGraphicsPath(lower,
                        CDrawContext::kPathStroked);
                };
                contour(0.0f, currentPhase, alphaColor(0x8a8a8a, 132));
                contour(currentPhase, 1.0f, alphaColor(0xe0e0e0, 206));
                double source = event.x + event.width * currentPhase;
                source -= std::floor(source);
                const double x = wave.left + source * wave.getWidth();
                const double radius = 2.0 + 2.0
                    * grainWindow(currentPhase, skew, envelope);
                context.setFillColor(color(0x080808));
                context.drawEllipse(rect(x - radius - 1.5,
                    wave.getCenter().y - radius - 1.5,
                    radius * 2.0 + 3.0, radius * 2.0 + 3.0), kDrawFilled);
                context.setFillColor(alphaColor(0xf5f5f5, 220));
                context.drawEllipse(rect(x - radius,
                    wave.getCenter().y - radius, radius * 2.0,
                    radius * 2.0), kDrawFilled);
            }
        } else if (span > 0.0) {
            char nudgeName[32] {};
            std::snprintf(nudgeName, sizeof(nudgeName), "Lane %u Nudge",
                slot + 1u);
            const double nudge = paramNamed(nudgeName);
            if (std::abs(nudge) > 1.0e-6) {
                double seam = -nudge;
                seam -= std::floor(seam);
                const double x = wave.left + (start + seam * span)
                    * wave.getWidth();
                context.setFrameColor(style.accent);
                for (double y = wave.top; y < wave.bottom; y += 6.0)
                    context.drawLine(CPoint(x, y),
                        CPoint(x, std::min(y + 3.0, wave.bottom)));
                auto flag = owned(context.createGraphicsPath());
                if (flag) {
                    flag->beginSubpath(x - 4.0, wave.bottom - 1.0);
                    flag->addLine(x + 4.0, wave.bottom - 1.0);
                    flag->addLine(x, wave.bottom - 7.0);
                    flag->closeSubpath();
                    context.setFillColor(style.accent);
                    context.drawGraphicsPath(flag,
                        CDrawContext::kPathFilled);
                }
            }
        }
        if (config.visualization != SampleFamilyVisualization::Grains) {
            std::array<SampleFamilyVisualPoint, 64u> cursors {};
            const uint32_t count = visualPoints(30u + slot, cursors);
            for (uint32_t cursor = 0u; cursor < count; ++cursor) {
                const auto& point = cursors[cursor];
                if (!(point.intensity > 0.01f) || point.x < 0.0f
                    || point.x > 1.0f)
                    continue;
                double timelinePosition = point.x;
                if (config.visualization == SampleFamilyVisualization::Lanes
                    && span > 0.0) {
                    char nudgeName[32] {};
                    std::snprintf(nudgeName, sizeof(nudgeName),
                        "Lane %u Nudge", slot + 1u);
                    const double nudge = paramNamed(nudgeName);
                    if (std::abs(nudge) > 1.0e-9
                        && point.x >= start && point.x <= end) {
                        timelinePosition = start + s3g::sample::laneTimelinePhase(
                            (point.x - start) / span, nudge) * span;
                    }
                }
                const double x = wave.left
                    + timelinePosition * wave.getWidth();
                const double activity = std::sqrt(std::clamp(
                    static_cast<double>(point.intensity), 0.0, 1.0));
                context.setFillColor(color(0x080808));
                context.drawEllipse(rect(x - 4.5,
                    wave.getCenter().y - 4.5, 9.0, 9.0), kDrawFilled);
                context.setFillColor(alphaColor(0xf5f5f5,
                    static_cast<uint8_t>(std::lround(194.0
                        + 61.0 * activity))));
                context.drawEllipse(rect(x - 3.0,
                    wave.getCenter().y - 3.0, 6.0, 6.0), kDrawFilled);
            }
        }
        const CColor boundaryColors[] {style.accent, style.text};
        const double boundaries[] {start, end};
        for (uint32_t index = 0u; index < 2u; ++index) {
            const double x = wave.left + boundaries[index] * wave.getWidth();
            context.setFrameColor(boundaryColors[index]);
            context.setLineWidth(1.0);
            context.drawLine(CPoint(x, wave.top), CPoint(x, wave.bottom));
        }
    }

    void drawRingsSource(CDrawContext& context)
    {
        foundation::drawPanel(context, samplePanelRect(),
            "CONCENTRIC SOURCE FIELD / 32 RINGS MAX", font);
        drawRingField(context);
        for (uint32_t slot = 0u; slot < std::min(4u, sampleSlotCount());
             ++slot) {
            const CRect badge = slotButtonRect(slot, 4u);
            context.setFillColor(slot == selectedSlot
                    ? color(0x2b2b2b) : color(0x171717));
            context.drawRect(badge, kDrawFilled);
            context.setFrameColor(slot == selectedSlot
                    ? style.accent : style.grid);
            context.drawRect(badge, kDrawStroked);
            const auto* current = asset(slot);
            char label[64] {};
#if defined(_WIN32)
            if (current && windowsAssetValid(current)) {
#else
            if (current && current->valid()) {
#endif
                std::snprintf(label, sizeof(label), "%c  %uCH  %.2fS",
                    static_cast<int>('A' + slot),
                    static_cast<unsigned>(current->channelCount),
                    static_cast<double>(current->frameCount())
                        / current->sampleRate);
            } else {
                std::snprintf(label, sizeof(label), "%c  EMPTY",
                    static_cast<int>('A' + slot));
            }
            text(context, label, badge.left + 8.0, badge.top + 5.0,
                badge.getWidth() - 16.0, style.value, kLeftText, tinyFont);
        }
    }

    void drawCirculatorSource(CDrawContext& context)
    {
        foundation::drawPanel(context, samplePanelRect(),
            "STEREO LOOP SOURCES", font);
        for (uint32_t slot = 0u; slot < 2u; ++slot) {
            context.setFillColor(style.button);
            context.drawRect(sampleLoadButtonRect(slot), kDrawFilled);
            foundation::drawTextInRect(context, slot == 0u ? "LOAD A" : "LOAD B",
                sampleLoadButtonRect(slot), color(slot == 0u ? 0x5f91a8 : 0xb1845f), font);
        }
        const CRect wave = waveformRect();
        context.setFillColor(style.strip);
        context.drawRect(wave, kDrawFilled);
        context.setFrameColor(style.grid);
        context.drawRect(wave, kDrawStroked);
        drawCirculatorWaveforms(context, wave);
        drawCirculatorCrossfade(context);
    }

    void drawWaveforms(CDrawContext& context)
    {
        const CRect wave = waveformRect();
        context.setFillColor(style.strip);
        context.drawRect(wave, kDrawFilled);
        context.setFrameColor(style.grid);
        context.setLineWidth(1.0);
        context.drawRect(wave, kDrawStroked);
        if (config.visualization == SampleFamilyVisualization::Circulator) {
            drawCirculatorWaveforms(context, wave);
            return;
        }
        if (config.visualization == SampleFamilyVisualization::Doubles
            && sampleSlotCount() == 1u) {
            const double height = wave.getHeight() * 0.5;
            const CRect deckA = rect(wave.left, wave.top,
                wave.getWidth(), height);
            const CRect deckB = rect(wave.left, wave.top + height,
                wave.getWidth(), height);
            drawWaveform(context, deckA, 0u, 0);
            drawWaveform(context, deckB, 0u, 1);
            context.setFrameColor(style.grid);
            context.drawLine(CPoint(wave.left, deckB.top),
                CPoint(wave.right, deckB.top));
            text(context, "DECK A", deckA.left + 6.0, deckA.top + 3.0,
                72.0, color(0x65c9da), kLeftText, tinyFont);
            text(context, "DECK B", deckB.left + 6.0, deckB.top + 3.0,
                72.0, color(0xe39062), kLeftText, tinyFont);
            drawMarkers(context, deckA);
            return;
        }
        const uint32_t slots = sampleSlotCount();
        for (uint32_t slot = 0u; slot < slots; ++slot) {
            const CRect lane = waveformSlotRect(slot);
            if (slot == selectedSlot) {
                context.setFillColor(color(0x202020));
                context.drawRect(rect(lane.left + 1.0, lane.top + 1.0,
                    lane.getWidth() - 2.0, lane.getHeight() - 2.0),
                    kDrawFilled);
            }
            if (slot != 0u) {
                context.setFrameColor(color(0x4b4b4b));
                context.drawLine(CPoint(lane.left, lane.top),
                    CPoint(lane.right, lane.top));
            }
            drawWaveform(context, lane, slot, -1);
        }
        drawMarkers(context, waveformSlotRect(selectedSlot));
    }

    void drawWaveform(CDrawContext& context, const CRect& lane,
        uint32_t slot, int cursorFilter)
    {
        const auto* current = asset(slot);
#if defined(_WIN32)
        if (!current || !windowsAssetValid(current)) {
#else
        if (!current || !current->valid()) {
#endif
            char empty[32] {};
            std::snprintf(empty, sizeof(empty), sampleSlotCount() == 1u
                ? "DROP AUDIO HERE" : "SLOT %c / DROP AUDIO",
                static_cast<int>('A' + slot));
            foundation::drawTextInRect(context, empty, lane, style.dim,
                tinyFont);
            return;
        }
        const std::size_t pixels = static_cast<std::size_t>(
            std::max(1.0, std::floor(lane.getWidth())));
        const bool wavesets = config.visualization
            == SampleFamilyVisualization::Wavesets;
        const bool separateChannels = wavesets
            || config.visualization == SampleFamilyVisualization::Motion;
        const double visibleStart = hasScrollableWaveform()
            ? wavesetsViewStart : 0.0;
        const double visibleSpan = hasScrollableWaveform()
            ? wavesetsVisibleSpan() : 1.0;
        const uint8_t displayChannels = separateChannels
            ? current->channelCount : 1u;
        const double start = paramNamed("Start");
        const double end = paramNamed("End", 1.0);
        const double span = end - start;
        char nudgeName[32] {};
        std::snprintf(nudgeName, sizeof(nudgeName), "Lane %u Nudge", slot + 1u);
        const double nudge = config.visualization == SampleFamilyVisualization::Lanes
            ? paramNamed(nudgeName) : 0.0;
        for (uint8_t displayChannel = 0u;
             displayChannel < displayChannels; ++displayChannel) {
            const double channelHeight = lane.getHeight()
                / displayChannels;
            const CRect channelLane = separateChannels
                ? rect(lane.left, lane.top + channelHeight * displayChannel,
                    lane.getWidth(), channelHeight)
                : lane;
            const double center = channelLane.getCenter().y;
            const double amplitude = std::max(2.0,
                channelLane.getHeight() * 0.42);
            auto trace = owned(context.createGraphicsPath());
            if (!trace) continue;
            for (std::size_t pixel = 0u; pixel < pixels; ++pixel) {
                const double sourceBegin = visibleStart
                    + static_cast<double>(pixel) / pixels * visibleSpan;
                const double sourceEnd = visibleStart
                    + static_cast<double>(pixel + 1u) / pixels * visibleSpan;
                float minimum = 1.0f;
                float maximum = -1.0f;
                const uint8_t firstChannel = separateChannels
                    ? displayChannel : 0u;
                const uint8_t lastChannel = separateChannels
                    ? static_cast<uint8_t>(displayChannel + 1u)
                    : isLaneFamily() ? 1u : current->channelCount;
                const auto accumulate = [&](double begin, double end) {
                    const double maxFrame = current->frameCount() - 1u;
                    const std::size_t first = static_cast<std::size_t>(
                        std::floor(std::clamp(begin, 0.0, 1.0) * maxFrame));
                    const std::size_t last = std::min<std::size_t>(current->frameCount(),
                        static_cast<std::size_t>(std::ceil(std::clamp(end, 0.0, 1.0) * maxFrame)) + 1u);
                    const std::size_t stride = isLaneFamily() ? 1u
                        : std::max<std::size_t>(1u, (last - first + 23u) / 24u);
                    for (uint8_t channel = firstChannel; channel < lastChannel; ++channel) {
                        const auto& samples = current->channels[channel];
                        for (std::size_t frame = first; frame < last; frame += stride) {
                            minimum = std::min(minimum, samples[frame]);
                            maximum = std::max(maximum, samples[frame]);
                        }
                        minimum = std::min(minimum, samples[last - 1u]);
                        maximum = std::max(maximum, samples[last - 1u]);
                    }
                };
                const double centerPosition = (sourceBegin + sourceEnd) * 0.5;
                if (std::abs(nudge) > 1.0e-9 && span > 0.0
                    && centerPosition >= start && centerPosition <= end) {
                    const double firstPhase = std::clamp((sourceBegin - start) / span, 0.0, 1.0);
                    const double width = std::clamp((sourceEnd - start) / span, 0.0, 1.0) - firstPhase;
                    const double sourceFirst = s3g::sample::laneSourcePhase(firstPhase, nudge);
                    const double sourceLast = sourceFirst + width;
                    if (width >= 1.0) accumulate(start, end);
                    else if (sourceLast <= 1.0)
                        accumulate(start + sourceFirst * span, start + sourceLast * span);
                    else {
                        accumulate(start + sourceFirst * span, end);
                        accumulate(start, start + (sourceLast - 1.0) * span);
                    }
                } else {
                    accumulate(sourceBegin, sourceEnd);
                }
                const double x = lane.left + pixel;
                trace->beginSubpath(x, center - maximum * amplitude);
                trace->addLine(x, center - minimum * amplitude);
            }
            context.setFrameColor(separateChannels
                    ? color(displayChannel % 2u == 0u
                        ? 0x747d78 : 0x68706c)
                    : isLaneFamily() ? color(0x555555)
                    : slot == selectedSlot ? style.accent
                                           : color(0x747c78));
            context.setLineWidth(1.0);
            context.drawGraphicsPath(trace, CDrawContext::kPathStroked);
            if (separateChannels) {
                if (displayChannel != 0u) {
                    context.setFrameColor(color(0x333333));
                    context.drawLine(CPoint(channelLane.left + 1.0,
                        channelLane.top), CPoint(channelLane.right - 1.0,
                        channelLane.top));
                }
                char channelLabel[8] {};
                std::snprintf(channelLabel, sizeof(channelLabel), "%02u",
                    static_cast<unsigned>(displayChannel + 1u));
                text(context, channelLabel, channelLane.left + 5.0,
                    channelLane.getCenter().y - 6.0, 28.0,
                    color(0x676767), kLeftText, tinyFont);
            }
        }
        if (config.visualization == SampleFamilyVisualization::Waveform) {
            char details[96] {};
            std::snprintf(details, sizeof(details), "%c  %uCH  %u FR  %.0fHZ",
                static_cast<int>('A' + slot),
                static_cast<unsigned>(current->channelCount),
                current->frameCount(), current->sampleRate);
            text(context, details, lane.left + 6.0, lane.top + 3.0,
                lane.getWidth() - 12.0, color(0x858585), kLeftText,
                tinyFont);
        }

        if (!isLaneFamily()
            && !(cursorPresentationActive && sampleCursorScreenDraw())
            && (config.callbacks.getCursorStates
                || config.callbacks.getCursors)) {
            std::array<SampleFamilyCursorState, 64u> states {};
            uint32_t count = 0u;
            if (config.callbacks.getCursorStates) {
                count = std::min<uint32_t>(64u,
                    config.callbacks.getCursorStates(
                        config.callbacks.context, slot, states.data(), 64u));
            } else {
                std::array<float, 64u> positions {};
                std::array<uint8_t, 64u> keys {};
                count = std::min<uint32_t>(64u,
                    config.callbacks.getCursors(config.callbacks.context,
                        slot, positions.data(), keys.data(), 64u));
                for (uint32_t cursor = 0u; cursor < count; ++cursor) {
                    states[cursor].position = positions[cursor];
                    states[cursor].key = keys[cursor];
                }
            }
            for (uint32_t cursor = 0u; cursor < count; ++cursor) {
                if (cursorFilter >= 0
                    && static_cast<int>(states[cursor].key & 1u)
                        != cursorFilter)
                    continue;
                if (!(states[cursor].position >= 0.0f
                        && states[cursor].position <= 1.0f)) continue;
                if (!waveformValueVisible(states[cursor].position)) continue;
                const double x = waveformXForNormalized(
                    lane, states[cursor].position);
                const CColor cursorColor = config.visualization == SampleFamilyVisualization::Doubles
                    ? color((states[cursor].key & 1u) == 0u ? 0x69d2dc : 0xff7047)
                    : cursor % 3u == 0u ? style.fill
                    : cursor % 3u == 1u ? style.accent : style.dim;
                context.setFrameColor(cursorColor);
                context.setLineWidth(1.25);
                context.drawLine(CPoint(x, lane.top),
                    CPoint(x, lane.bottom));
                if (hasScrollableWaveform()) {
                    char label[32] {};
                    const bool motion = config.visualization
                        == SampleFamilyVisualization::Motion;
                    if (states[cursor].hasOutputRouting) {
                        if (states[cursor].outputWidth > 1u)
                            std::snprintf(label, sizeof(label),
                                motion ? "N%u>%02u/%02u" : "N%u > %02u/%02u",
                                states[cursor].key,
                                states[cursor].outputFirst + 1u,
                                states[cursor].outputSecond + 1u);
                        else
                            std::snprintf(label, sizeof(label),
                                motion ? "N%u>%02u" : "N%u > %02u",
                                states[cursor].key,
                                states[cursor].outputFirst + 1u);
                    } else {
                        std::snprintf(label, sizeof(label), "N%u",
                            states[cursor].key);
                    }
                    const double flagWidth = states[cursor].hasOutputRouting
                        ? (states[cursor].outputWidth > 1u ? 91.0 : 72.0)
                        : 34.0;
                    double flagX = x + 4.0;
                    if (flagX + flagWidth > lane.right - 3.0)
                        flagX = x - flagWidth - 4.0;
                    const double flagY = lane.top + 18.0
                        + static_cast<double>(cursor % 10u) * 15.0;
                    const CRect flag = rect(flagX - 2.0, flagY - 1.0,
                        flagWidth + 4.0, 14.0);
                    context.setFillColor(style.background);
                    context.drawRect(flag, kDrawFilled);
                    text(context, label, flagX, flagY, flagWidth,
                        cursorColor, kLeftText, tinyFont);
                }
            }
        }
    }

    template <std::size_t Capacity>
    uint32_t visualPoints(uint32_t series,
        std::array<SampleFamilyVisualPoint, Capacity>& points) const
    {
        if (!config.callbacks.getVisualizationPoints) return 0u;
        return std::min<uint32_t>(static_cast<uint32_t>(Capacity),
            config.callbacks.getVisualizationPoints(config.callbacks.context,
                series, points.data(),
                static_cast<uint32_t>(Capacity)));
    }

    void drawGraphFrame(CDrawContext& context, const CRect& graph)
    {
        context.setFillColor(color(0x141414));
        context.drawRect(graph, kDrawFilled);
        context.setFrameColor(style.grid);
        context.setLineWidth(1.0);
        context.drawRect(graph, kDrawStroked);
    }

    template <std::size_t Capacity>
    void drawPolyline(CDrawContext& context, const CRect& graph,
        const std::array<SampleFamilyVisualPoint, Capacity>& points,
        uint32_t count, CColor lineColor, double lineWidth = 1.2)
    {
        count = std::min<uint32_t>(count, static_cast<uint32_t>(Capacity));
        if (count < 2u) return;
        auto path = owned(context.createGraphicsPath());
        if (!path) return;
        const CPoint first = mapVisualPoint(graph, points[0]);
        path->beginSubpath(first.x, first.y);
        for (uint32_t index = 1u; index < count; ++index) {
            const CPoint point = mapVisualPoint(graph, points[index]);
            path->addLine(point.x, point.y);
        }
        context.setFrameColor(lineColor);
        context.setLineWidth(lineWidth);
        context.drawGraphicsPath(path, CDrawContext::kPathStroked);
    }

    CPoint mapVisualPoint(const CRect& graph,
        const SampleFamilyVisualPoint& point) const
    {
        return CPoint(graph.left
                + std::clamp(static_cast<double>(point.x), 0.0, 1.0)
                    * graph.getWidth(),
            graph.top
                + std::clamp(static_cast<double>(point.y), 0.0, 1.0)
                    * graph.getHeight());
    }

    void drawVisualPoint(CDrawContext& context, const CRect& graph,
        const SampleFamilyVisualPoint& point, CColor pointColor,
        double radius = 3.0)
    {
        const CPoint center = mapVisualPoint(graph, point);
        context.setFillColor(pointColor);
        context.drawEllipse(rect(center.x - radius, center.y - radius,
            radius * 2.0, radius * 2.0), kDrawFilled);
    }

    void drawVisualizationGrid(CDrawContext& context, const CRect& graph,
        uint32_t vertical = 8u, uint32_t horizontal = 4u)
    {
        context.setFrameColor(color(0x292929));
        context.setLineWidth(1.0);
        for (uint32_t index = 1u; index < vertical; ++index) {
            const double x = graph.left
                + graph.getWidth() * index / static_cast<double>(vertical);
            context.drawLine(CPoint(x, graph.top), CPoint(x, graph.bottom));
        }
        for (uint32_t index = 1u; index < horizontal; ++index) {
            const double y = graph.top
                + graph.getHeight() * index / static_cast<double>(horizontal);
            context.drawLine(CPoint(graph.left, y), CPoint(graph.right, y));
        }
    }

    void drawAuxiliaryVisualization(CDrawContext& context)
    {
        if (!hasAuxiliaryVisualization()) return;
        const CRect visual = visualizationRect();
        if (visual.getHeight() < 8.0) return;
        if (config.visualization == SampleFamilyVisualization::Motion) {
            drawMotionScope(context);
            return;
        }
        SampleFamilyWavesetsScopeState wavesetsScope {};
        if (config.visualization == SampleFamilyVisualization::Wavesets
            && config.callbacks.getWavesetsScopeState)
            config.callbacks.getWavesetsScopeState(
                config.callbacks.context, &wavesetsScope);
        std::string panelTitle = config.visualization
                == SampleFamilyVisualization::Wavesets
            ? "WAVESET SCOPE / "
                + std::to_string(wavesetsScope.channelCount) + " CH"
            : config.visualization == SampleFamilyVisualization::Motion
            ? "SIGNAL FLOW / 3 VOICES"
            : config.visualization == SampleFamilyVisualization::Cutups
            ? "CUT PATTERN / STEP CLOCK × FILE LANE"
            : config.visualization == SampleFamilyVisualization::Grains
            ? "READ HEAD PATH / LIVE GRAIN EVENTS"
            : "READ HEAD PATH / PATH CLOCK × FILE LANE";
        foundation::drawPanel(context, visual, panelTitle.c_str(), font);
        const CRect graph = visualizationGraphRect();
        drawGraphFrame(context, graph);
        if (config.visualization != SampleFamilyVisualization::Wavesets)
            drawVisualizationGrid(context, graph, 8u,
                hasPathVisualization() ? 3u : 2u);

        if (hasPathVisualization()) {
            text(context, config.visualization
                    == SampleFamilyVisualization::Cutups
                    ? "X  PATTERN STEP"
                    : config.visualization == SampleFamilyVisualization::Grains
                        && paramNamed("Source Advance") >= 0.5
                    ? "X  GRAIN CLOCK" : config.visualization
                            == SampleFamilyVisualization::Grains
                        ? "X  SCAN CLOCK" : "X  PATH CLOCK",
                visual.left + 12.0, 401.0, 175.0,
                style.label, kLeftText, tinyFont);
            text(context, "Y  FILE LANE", visual.left + 12.0,
                418.0, 175.0, style.label,
                kLeftText, tinyFont);
            if (config.visualization == SampleFamilyVisualization::Cutups) {
                text(context, paramNamed("Regions") < 0.5
                        ? "REGIONS: EQUAL DIVISIONS"
                        : "REGIONS: DETECTED TRANSIENTS",
                    30.0, 451.0, 178.0, style.value, kLeftText, tinyFont);
                text(context, std::string("FILE ORDER: ")
                        + paramTextNamed("File Order"),
                    30.0, 468.0, 178.0, style.value, kLeftText, tinyFont);
                text(context, std::string("POLY PATH: ")
                        + paramTextNamed("Poly Path"),
                    30.0, 485.0, 178.0, style.value, kLeftText, tinyFont);
                text(context, "DRAG: FILE • OPTION: SOURCE", 30.0, 502.0,
                    178.0, style.value, kLeftText, tinyFont);
            } else {
                text(context, paramNamed("Lane Change") < 0.5
                        ? "LANE CHANGE: EQUAL-POWER"
                        : config.visualization
                                == SampleFamilyVisualization::Grains
                            ? "LANE CHANGE: DIRECT GRAIN CHOICE"
                            : "LANE CHANGE: SMOOTHED JUMP",
                    30.0, 451.0, 178.0, style.value, kLeftText, tinyFont);
                text(context, "EMPTY: USE NEAREST LOADED", 30.0, 468.0,
                    178.0, style.value, kLeftText, tinyFont);
                const bool manual = paramTextNamed("Lane Path") == "MANUAL";
                text(context, manual ? "ADD / DRAG • RIGHT-CLICK DELETE"
                        : paramTextNamed("Lane Path") == "RANDOM"
                            ? "RANDOM: SEEDED 8-STEP PATH"
                            : "SHAPE + CLOCK MODIFIERS APPLY",
                    30.0, 485.0, 178.0, style.value, kLeftText, tinyFont);
                if (manual)
                    text(context, "DIRECT PATH: MODIFIERS BYPASSED", 30.0,
                        502.0, 178.0, style.value, kLeftText, tinyFont);
            }
            for (uint32_t lane = 0u; lane < 4u; ++lane) {
                char number[4] {};
                std::snprintf(number, sizeof(number), "%u", lane + 1u);
                const double y = graph.top + lane * graph.getHeight() / 3.0;
                text(context, number, graph.left - 18.0, y - 6.0, 14.0,
                    style.value, kRightText, tinyFont);
            }
            drawPathVisualization(context, graph);
            return;
        }

        if (config.visualization == SampleFamilyVisualization::Wavesets) {
            std::string process = uppercaseAscii(
                wavesetsScope.processName[0]
                    ? wavesetsScope.processName : "REPEAT");
            if (wavesetsScope.hasFocusedVoice) {
                char focus[48] {};
                if (wavesetsScope.hasOutputRouting) {
                    if (wavesetsScope.outputWidth > 1u)
                        std::snprintf(focus, sizeof(focus),
                            " / N%03u>%02u/%02u", wavesetsScope.key,
                            wavesetsScope.outputFirst + 1u,
                            wavesetsScope.outputSecond + 1u);
                    else
                        std::snprintf(focus, sizeof(focus),
                            " / N%03u>%02u", wavesetsScope.key,
                            wavesetsScope.outputFirst + 1u);
                } else {
                    std::snprintf(focus, sizeof(focus), " / N%03u NEWEST",
                        wavesetsScope.key);
                }
                process += focus;
            }
            text(context, process, visual.left + 10.0,
                visual.top + 29.0, 142.0, style.label,
                kLeftText, tinyFont);
            char cycle[32] {};
            std::snprintf(cycle, sizeof(cycle), "G %02u  C %02u/%02u",
                wavesetsScope.groupSize, wavesetsScope.cycleOffset + 1u,
                wavesetsScope.groupSize);
            text(context, cycle, visual.left + 10.0, visual.top + 44.0,
                142.0, style.value, kLeftText, tinyFont);
            char repeat[36] {};
            std::snprintf(repeat, sizeof(repeat), "R %02u/%02u  D %3.0f%%",
                wavesetsScope.repeatIndex + 1u, wavesetsScope.repeats,
                wavesetsScope.processAmount * 100.0f);
            text(context, repeat, visual.left + 10.0, visual.top + 63.0,
                142.0, style.value, kLeftText, tinyFont);
            text(context, "GRAY SRC / COLOR RESULT", visual.left + 10.0,
                visual.top + 82.0, 144.0, style.dim,
                kLeftText, tinyFont);
            const double scopedWidth = graph.getWidth()
                / std::clamp(wavesetsScopeScale, 1.0, 16.0);
            const CRect scoped = rect(graph.getCenter().x - scopedWidth * 0.5,
                graph.top, scopedWidth, graph.getHeight());
            context.setFillColor(color(0x262626));
            context.drawRect(scoped, kDrawFilled);
            context.setFrameColor(style.dim);
            context.drawLine(CPoint(scoped.left, graph.top),
                CPoint(scoped.left, graph.bottom));
            context.drawLine(CPoint(scoped.right, graph.top),
                CPoint(scoped.right, graph.bottom));
            const uint32_t channelCount = std::max(1u,
                wavesetsScope.channelCount);
            const double laneHeight = graph.getHeight() / channelCount;
            for (uint32_t channel = 0u; channel < channelCount; ++channel) {
                const CRect lane = rect(graph.left,
                    graph.top + laneHeight * channel,
                    graph.getWidth(), laneHeight);
                const CRect scopedLane = rect(scoped.left, lane.top,
                    scoped.getWidth(), lane.getHeight());
                if (channel != 0u) {
                    context.setFrameColor(color(0x333333));
                    context.drawLine(CPoint(graph.left, lane.top),
                        CPoint(graph.right, lane.top));
                }
                context.setFrameColor(color(0x383838));
                context.drawLine(CPoint(scoped.left, lane.getCenter().y),
                    CPoint(scoped.right, lane.getCenter().y));
                std::array<SampleFamilyVisualPoint, 2048u> source {};
                std::array<SampleFamilyVisualPoint, 2048u> processed {};
                std::array<SampleFamilyVisualPoint, 64u> boundaries {};
                std::array<SampleFamilyVisualPoint, 1u> playhead {};
                const uint32_t series = channel * 4u;
                const uint32_t sourceCount = visualPoints(
                    series, source);
                const uint32_t processedCount = visualPoints(
                    series + 1u, processed);
                const uint32_t boundaryCount = visualPoints(
                    series + 2u, boundaries);
                const uint32_t playheadCount = visualPoints(
                    series + 3u, playhead);
                for (uint32_t boundary = 1u;
                     boundary + 1u < boundaryCount; ++boundary) {
                    const double x = scoped.left
                        + boundaries[boundary].x * scoped.getWidth();
                    context.setFrameColor(color(0x343434));
                    context.drawLine(CPoint(x, lane.top),
                        CPoint(x, lane.bottom));
                }
                drawPolyline(context, scopedLane, source, sourceCount,
                    color(0xa0a0a0), 1.0);
                drawPolyline(context, scopedLane, processed, processedCount,
                    color(channel % 2u == 0u ? 0x69d2dc : 0xd79a55),
                    1.25);
                if (playheadCount > 0u) {
                    const double x = scoped.left
                        + playhead[0u].x * scoped.getWidth();
                    context.setFrameColor(style.fill);
                    context.setLineWidth(1.0);
                    context.drawLine(CPoint(x, lane.top),
                        CPoint(x, lane.bottom));
                }
                char channelLabel[8] {};
                std::snprintf(channelLabel, sizeof(channelLabel), "%02u",
                    channel + 1u);
                text(context, channelLabel, graph.left + 4.0,
                    lane.top + 1.0, 24.0, color(0x676767),
                    kLeftText, tinyFont);
            }
            char scale[96] {};
            std::snprintf(scale, sizeof(scale),
                "GROUP BOUNDS / VIEW %.1fX / SCROLL SCALE / DOUBLE-CLICK FIT",
                wavesetsScopeScale);
            context.setFillColor(style.background);
            context.drawRect(rect(graph.right - 352.0,
                graph.bottom - 16.0, 350.0, 15.0), kDrawFilled);
            text(context, scale, graph.right - 350.0,
                graph.bottom - 14.0, 344.0, color(0x676767),
                kRightText, tinyFont);
            return;
        }

    }

    const char* motionSourceDescription() const
    {
        const int model = static_cast<int>(std::lround(paramNamed("Segment Model")));
        const int motion = static_cast<int>(std::lround(paramNamed("Motion")));
        switch (model) {
        case 1: case 5: return "LOCUS LOCK";
        case 2: case 6: return paramNamed("Step") <= 0.0
            ? "LIVE MOTION CURSOR" : "LOCUS + STEP SEQUENCE";
        case 3: return motion == 6 ? "RANDOM IN MOVING FIELD" : "RANDOM IN LOCUS FIELD";
        case 4: return "LOCUS + STEP GROUPS";
        default: return "LIVE MOTION CURSOR";
        }
    }

    void drawMotionScope(CDrawContext& context)
    {
        SampleFamilyMotionScopeState state {};
        if (config.callbacks.getMotionScopeState)
            config.callbacks.getMotionScopeState(config.callbacks.context, &state);
        const CRect visual = visualizationRect();
        const CRect graph = visualizationGraphRect();
        // Cocoa's scope is a single strip, with its title confined to the left.
        context.setFillColor(style.strip);
        context.drawRect(visual, kDrawFilled);
        context.setFrameColor(style.grid);
        context.setLineWidth(1.0);
        context.drawRect(visual, kDrawStroked);
        const int model = static_cast<int>(std::lround(paramNamed("Segment Model")));
        const int articulation = static_cast<int>(std::lround(paramNamed("Articulation")));
        const bool motor = articulation == 1;
        const bool packets = articulation != 0 || model != 0;
        const std::string lines[] {
            "SIGNAL FLOW / " + std::to_string(state.activeVoices) + " VOICES",
            "1A PATH  " + paramTextNamed("Motion"),
            std::string("1B SOURCE ") + motionSourceDescription(),
            "2 SOUND  " + paramTextNamed(model != 0 ? "Segment Model" : "Articulation")};
        for (uint32_t line = 0u; line < 4u; ++line)
            text(context, lines[line], visual.left + 10.0,
                visual.top + 8.0 + line * 19.0, 198.0, style.value,
                kLeftText, tinyFont);
        context.setFillColor(color(0x202020));
        context.drawRect(graph, kDrawFilled);
        context.setFrameColor(style.grid);
        context.drawRect(graph, kDrawStroked);
        std::array<SampleFamilyVisualPoint, 257u> trajectory {};
        const uint32_t count = visualPoints(0u, trajectory);
        for (uint32_t index = 0u; index < count; ++index)
            trajectory[index].y = 1.0f - trajectory[index].y;
        drawPolyline(context, rect(graph.left, graph.top + 6.0,
            graph.getWidth(), graph.getHeight() - 12.0), trajectory, count,
            style.accent, 1.25);
        if (packets) {
            if (motor || model != 0) {
                std::array<SampleFamilyVisualPoint, 129u> envelope {};
                const auto shape = static_cast<s3g::sample::MotorEnvelopeShape>(
                    std::clamp(static_cast<int>(std::lround(paramNamed("Motor Envelope"))), 0, 3));
                for (uint32_t index = 0u; index < envelope.size(); ++index) {
                    const double phase = index / 128.0;
                    envelope[index].x = static_cast<float>(phase);
                    envelope[index].y = 1.0f - s3g::sample::motorEnvelopeLevel(
                        static_cast<float>(phase), static_cast<float>(paramNamed("Symmetry", 0.5)), shape);
                }
                drawPolyline(context, rect(graph.left, graph.bottom - 22.0,
                    graph.getWidth(), 18.0), envelope, 129u, style.dim, 1.0);
            }
            const int ticks = static_cast<int>(std::clamp(std::lround(
                model != 0 ? paramNamed("Event Rate") : motor
                    ? paramNamed("Inner Rate") / std::max(0.05, paramNamed("Outer Rate"))
                    : paramNamed("Inner Rate") * 0.5), 1l, 64l));
            context.setFillColor(style.fill);
            for (int tick = 0; tick < ticks; ++tick)
                context.drawRect(rect(graph.left + (tick + 0.5) / ticks * graph.getWidth(),
                    graph.bottom - 8.0, 1.0, 4.0), kDrawFilled);
        }
        if (state.cursorPhase >= 0.0f) {
            const double x = graph.left + std::clamp(state.cursorPhase, 0.0f, 1.0f) * graph.getWidth();
            context.setFrameColor(style.fill);
            context.setLineWidth(1.0);
            context.drawLine(CPoint(x, graph.top), CPoint(x, graph.bottom));
        }
    }

    void drawPathVisualization(CDrawContext& context, const CRect& graph)
    {
        std::array<SampleFamilyVisualPoint, 256u> trace {};
        std::array<SampleFamilyVisualPoint, 256u> anchors {};
        std::array<SampleFamilyVisualPoint, 256u> cursors {};
        std::array<SampleFamilyVisualPoint, 256u> events {};
        const uint32_t traceCount = visualPoints(0u, trace);
        const uint32_t anchorCount = visualPoints(1u, anchors);
        const uint32_t cursorCount = visualPoints(2u, cursors);
        const uint32_t eventCount = visualPoints(3u, events);

        if (config.visualization == SampleFamilyVisualization::Cutups) {
            for (uint32_t index = 0u; index < traceCount; ++index) {
                const CPoint center = mapVisualPoint(graph, trace[index]);
                const double width = std::max(2.0,
                    static_cast<double>(trace[index].width)
                        * graph.getWidth());
                const CRect block = rect(center.x, center.y - 6.0,
                    std::min(width, graph.right - center.x), 12.0);
                context.setFillColor(style.accent);
                context.drawRect(block, kDrawFilled);
                if (index < anchorCount) {
                    const double sourceX = block.left + std::clamp(
                        static_cast<double>(anchors[index].width), 0.0, 1.0)
                        * block.getWidth();
                    context.setFrameColor(color(0x101010));
                    context.setLineWidth(2.0);
                    context.drawLine(CPoint(sourceX, block.top),
                        CPoint(sourceX, block.bottom));
                }
                char number[12] {};
                std::snprintf(number, sizeof(number), "%u", index + 1u);
                text(context, number, center.x + 1.0, graph.bottom - 17.0,
                    width - 2.0, style.value, kLeftText, tinyFont);
            }
        } else {
            drawPolyline(context, graph, trace, traceCount,
                color(0x6c7773), 1.1);
            drawPolyline(context, graph, anchors, anchorCount,
                style.accent, 1.5);
            for (uint32_t index = 0u; index < anchorCount; ++index)
                drawVisualPoint(context, graph, anchors[index], style.accent,
                    visualizationDragPoint == static_cast<int32_t>(index)
                        ? 4.2 : 3.1);
        }

        for (uint32_t index = 0u; index < eventCount; ++index) {
            const CPoint center = mapVisualPoint(graph, events[index]);
            const double width = std::max(3.0,
                static_cast<double>(events[index].width) * graph.getWidth());
            context.setFrameColor(index % 2u == 0u
                ? color(0xe39062) : color(0x8bc3cf));
            context.setLineWidth(std::max(1.0,
                std::clamp(static_cast<double>(events[index].intensity),
                    0.15, 1.0) * 2.0));
            context.drawLine(CPoint(center.x - width * 0.5, center.y),
                CPoint(center.x + width * 0.5, center.y));
        }
        for (uint32_t index = 0u; index < cursorCount; ++index)
            drawVisualPoint(context, graph, cursors[index],
                index % 2u == 0u ? style.fill : color(0xe39062), 3.7);
    }

    void drawCirculatorWaveforms(CDrawContext& context, const CRect& wave)
    {
        const double laneHeight = 54.0;
        for (uint32_t slot = 0u; slot < 2u; ++slot) {
            const CRect lane = rect(wave.left,
                66.0 + slot * laneHeight, wave.getWidth(), 48.0);
            context.setFillColor(color(0x111111));
            context.drawRect(rect(26.0, 64.0 + slot * laneHeight,
                708.0, 52.0), kDrawFilled);
            context.setFillColor(color(0x090a0a));
            context.drawRect(lane, kDrawFilled);
            if (slot != 0u) {
                context.setFrameColor(style.grid);
                context.drawLine(CPoint(lane.left, lane.top),
                    CPoint(lane.right, lane.top));
            }
            for (uint32_t channel = 0u; channel < 2u; ++channel) {
                const double center = 64.0 + slot * laneHeight + (channel == 0u ? 14.0 : 38.0);
                context.setFrameColor(alphaColor(0x343434, 217));
                context.setLineWidth(1.0);
                context.drawLine(CPoint(lane.left, center), CPoint(lane.right, center));
                std::array<SampleFamilyVisualPoint, 256u> points {};
                const uint32_t count = visualPoints(10u + slot * 2u + channel, points);
                context.setFrameColor(alphaColor(slot == 0u ? 0xd5d9d7 : 0xaeb4b1, 148));
                for (uint32_t index = 0u; index < count; ++index) {
                    const double x = lane.left + points[index].x * lane.getWidth();
                    context.drawLine(CPoint(x, center - points[index].width * 10.5),
                        CPoint(x, center - points[index].y * 10.5));
                }
            }
            char title[4] {};
            std::snprintf(title, sizeof(title), "%c",
                static_cast<int>('A' + slot));
            text(context, title, 28.0, 73.0 + slot * laneHeight,
                28.0, slot == 0u ? color(0x5f91a8) : color(0xb1845f),
                kLeftText, font);
            text(context, "L", 108.0, 71.0 + slot * laneHeight,
                12.0, style.value, kLeftText, tinyFont);
            text(context, "R", 108.0, 95.0 + slot * laneHeight,
                12.0, style.value, kLeftText, tinyFont);
            std::string status = sampleDetail(slot);
            if (status.empty()) status = "EMPTY";
            text(context, status, 660.0, 81.0 + slot * laneHeight,
                70.0, style.value, kLeftText, tinyFont);
            std::array<SampleFamilyVisualPoint, 256u> markers {};
            const uint32_t markerCount = visualPoints(3u + slot, markers);
            for (uint32_t marker = 0u; marker < markerCount; ++marker) {
                if (markers[marker].intensity <= 0.0f) continue;
                const double x = lane.left + std::clamp(
                    static_cast<double>(markers[marker].x), 0.0, 1.0)
                    * lane.getWidth();
                context.setFrameColor(markers[marker].kind == 2u
                    ? style.fill : color(0x777f7b));
                context.setLineWidth(markers[marker].kind == 2u ? 1.5 : 1.0);
                if (markers[marker].kind < 2u) {
                    for (double y = lane.top; y < lane.bottom; y += 6.0)
                        context.drawLine(CPoint(x, y), CPoint(x, std::min(y + 3.0, lane.bottom)));
                } else context.drawLine(CPoint(x, lane.top), CPoint(x, lane.bottom));
            }
            const double sourceScale = slot == 0u && paramNamed("Loop Model") < 0.5 ? 0.5 : 1.0;
            const double start = paramNamed(slot == 0u ? "Loop 1 Start" : "Loop 2 Start") * sourceScale;
            const double end = paramNamed(slot == 0u ? "Loop 1 End" : "Loop 2 End", 1.0) * sourceScale;
            context.setFrameColor(style.accent);
            context.setLineWidth(1.4);
            for (const double position : {start, end}) {
                const double x = lane.left + position * lane.getWidth();
                context.drawLine(CPoint(x, lane.top), CPoint(x, lane.bottom));
            }
            text(context, "S", lane.left + start * lane.getWidth() + 4.0, lane.top + 2.0,
                10.0, style.text, kLeftText, tinyFont);
            text(context, "E", lane.left + end * lane.getWidth() - 12.0, lane.top + 2.0,
                10.0, style.text, kLeftText, tinyFont);
        }
    }

    void drawCirculatorCrossfade(CDrawContext& context)
    {
        const CRect graph = visualizationRect();
        context.setFillColor(color(0x090a0a));
        context.drawRect(graph, kDrawFilled);
        const uint32_t stops = std::max(33u, static_cast<uint32_t>(graph.getHeight()));
        for (uint32_t stop = 0u; stop < stops; ++stop) {
            const double unit = static_cast<double>(stop) / (stops - 1u);
            const auto gains = s3g::crcltrCrossfadeGains(
                static_cast<s3g::CrcltrCrossfadeShape>(std::clamp(
                    static_cast<int>(std::lround(paramNamed("Fade Shape"))), 0, 8)),
                static_cast<float>(unit));
            const double a = gains.a / std::max(0.0001f, gains.a + gains.b);
            const double b = 1.0 - a;
            const double overlap = std::clamp(2.0 * std::min(gains.a, gains.b), 0.0, 1.0);
            const double energyScale = std::clamp(0.65 + 0.35
                * std::sqrt(gains.a * gains.a + gains.b * gains.b), 0.82, 1.08);
            const uint8_t red = static_cast<uint8_t>(255.0 * std::clamp(
                ((0.3725 * a + 0.6941 * b) * (1.0 - overlap)
                    + 0.5725 * overlap) * energyScale, 0.0, 1.0));
            const uint8_t green = static_cast<uint8_t>(255.0 * std::clamp(
                ((0.5686 * a + 0.5176 * b) * (1.0 - overlap)
                    + 0.4980 * overlap) * energyScale, 0.0, 1.0));
            const uint8_t blue = static_cast<uint8_t>(255.0 * std::clamp(
                ((0.6588 * a + 0.3725 * b) * (1.0 - overlap)
                    + 0.6471 * overlap) * energyScale, 0.0, 1.0));
            const uint32_t rgb = (static_cast<uint32_t>(red) << 16u)
                | (static_cast<uint32_t>(green) << 8u)
                | static_cast<uint32_t>(blue);
            const double top = graph.top + graph.getHeight() * stop / stops;
            const double bottom = graph.top + graph.getHeight()
                * (stop + 1u) / stops;
            context.setFillColor(color(rgb));
            context.drawRect(rect(graph.left, top, graph.getWidth(),
                bottom - top + 0.5), kDrawFilled);
        }
        context.setFrameColor(alphaColor(0x323434, 144));
        context.setLineWidth(1.0);
        for (uint32_t division = 1u; division < 4u; ++division) {
            const double y = graph.top + graph.getHeight()
                * division / 4.0;
            context.drawLine(CPoint(graph.left, y), CPoint(graph.right, y));
        }
        context.setFrameColor(style.grid);
        context.drawRect(graph, kDrawStroked);
        std::array<SampleFamilyVisualPoint, 256u> mix {};
        double unit = 0.5;
        if (visualPoints(2u, mix) > 0u) {
            unit = std::clamp(static_cast<double>(mix[0].x), 0.0, 1.0);
        }
        const double y = graph.top + unit * graph.getHeight();
        context.setFrameColor(alphaColor(0xc8c8c8, 200));
        context.setLineWidth(1.2);
        context.drawLine(CPoint(graph.left, y), CPoint(graph.right, y));
        context.setFillColor(color(0x050505));
        context.drawEllipse(rect(graph.getCenter().x - 4.0, y - 4.0,
            8.0, 8.0), kDrawFilled);
        context.setFillColor(style.accent);
        context.drawEllipse(rect(graph.getCenter().x - 2.5, y - 2.5,
            5.0, 5.0), kDrawFilled);
        if (std::abs(mix[0u].y) > 0.5f) {
            const double direction = mix[0u].y > 0.0f ? 1.0 : -1.0;
            const double x = graph.right + 7.0;
            auto arrow = owned(context.createGraphicsPath());
            if (arrow) {
                arrow->beginSubpath(x - 3.5, y - direction * 3.0);
                arrow->addLine(x + 3.5, y - direction * 3.0);
                arrow->addLine(x, y + direction * 4.0);
                arrow->closeSubpath();
                context.setFillColor(alphaColor(0xc5c5c5, 209));
                context.drawGraphicsPath(arrow, CDrawContext::kPathFilled);
            }
        }
    }

    void drawRingField(CDrawContext& context)
    {
        constexpr double innerRadius = 34.0;
        constexpr double outerRadius = 292.0;
        constexpr double twoPi = 6.28318530717958647692;
        constexpr uint32_t segments = 320u;
        const CPoint center(428.0, 378.0);
        context.setFillColor(color(0x0e0e0e));
        context.drawEllipse(rect(center.x - outerRadius - 10.0,
            center.y - outerRadius - 10.0,
            (outerRadius + 10.0) * 2.0,
            (outerRadius + 10.0) * 2.0), kDrawFilled);

        struct RingRef { uint32_t slot; uint32_t channel; };
        std::array<RingRef, 32u> catalog {};
        std::array<int32_t, 4u> first {{-1,-1,-1,-1}};
        std::array<int32_t, 4u> last {{-1,-1,-1,-1}};
        uint32_t ringCount = 0u;
        for (uint32_t slot = 0u; slot < std::min(4u, sampleSlotCount());
             ++slot) {
            const auto* current = asset(slot);
#if defined(_WIN32)
            if (!current || !windowsAssetValid(current)) continue;
#else
            if (!current || !current->valid()) continue;
#endif
            first[slot] = static_cast<int32_t>(ringCount);
            const uint32_t width = std::min<uint32_t>(current->channelCount,
                8u);
            for (uint32_t channel = 0u;
                 channel < width && ringCount < catalog.size(); ++channel)
                catalog[ringCount++] = {slot, channel};
            last[slot] = static_cast<int32_t>(ringCount) - 1;
        }
#if defined(_WIN32) && defined(_MSC_VER)
        const auto radiusForRing = [ringCount, innerRadius, outerRadius](uint32_t ring) {
#else
        const auto radiusForRing = [ringCount](uint32_t ring) {
#endif
            return ringCount <= 1u ? (innerRadius + outerRadius) * 0.5
                : innerRadius + (outerRadius - innerRadius) * ring
                    / static_cast<double>(ringCount - 1u);
        };
        const double pitch = ringCount <= 1u ? 20.0
            : (outerRadius - innerRadius) / (ringCount - 1u);
        const auto polar = [&](double radius, double phase) {
            const double angle = phase * twoPi;
            return CPoint(center.x + std::sin(angle) * radius,
                center.y - std::cos(angle) * radius);
        };
        constexpr uint32_t slotColors[] {
            0x989898, 0x909090, 0x888888, 0x808080,
        };
        for (uint32_t ring = 0u; ring < ringCount; ++ring) {
            const RingRef ref = catalog[ring];
            const auto* current = asset(ref.slot);
            if (!current || ref.channel >= current->channels.size()) continue;
            const double radius = radiusForRing(ring);
            context.setFrameColor(color(0x303030));
            context.setLineWidth(0.45);
            context.drawEllipse(rect(center.x - radius, center.y - radius,
                radius * 2.0, radius * 2.0), kDrawStroked);
            const auto& samples = current->channels[ref.channel];
            if (samples.empty()) continue;
#if defined(_WIN32)
            if (config.callbacks.getWindowsOwnedAsset) {
                char cacheNudgeName[64] {};
                std::snprintf(cacheNudgeName, sizeof(cacheNudgeName), "Slot %c Wrap Nudge", static_cast<int>('A' + ref.slot));
                if (windowsRingsPaths[ring].draw(context, *current, ref.channel, radius, pitch,
                        paramNamed(cacheNudgeName), center, ref.slot == selectedSlot ? 0xd8d8d8 : slotColors[ref.slot]))
                    continue;
            }
#endif
            std::array<float, segments + 1u> peaks {};
            std::array<float, segments + 1u> rms {};
            std::array<float, segments + 1u> means {};
            float channelPeak = 1.0e-8f;
            char nudgeName[64] {};
            std::snprintf(nudgeName, sizeof(nudgeName), "Slot %c Wrap Nudge",
                static_cast<int>('A' + ref.slot));
            const std::size_t nudgeIndex = parameterIndex(nudgeName);
            const double nudge = nudgeIndex < parameters.size()
                ? param(parameters[nudgeIndex].id) : 0.0;
            const std::size_t window = std::max<std::size_t>(1u,
                samples.size() / segments);
            for (uint32_t point = 0u; point < segments; ++point) {
                double phase = static_cast<double>(point) / segments + nudge;
                phase -= std::floor(phase);
                const std::size_t start = std::min(samples.size() - 1u,
                    static_cast<std::size_t>(phase * samples.size()));
                float peak = 0.0f;
                double mean = 0.0, squareSum = 0.0;
                for (uint32_t tap = 0u; tap < 12u; ++tap) {
                    const std::size_t offset = window * tap / 12u;
                    const float sample = samples[(start + offset)
                        % samples.size()];
                    peak = std::max(peak, std::abs(sample));
                    mean += sample;
                    squareSum += static_cast<double>(sample) * sample;
                }
                rms[point] = static_cast<float>(std::sqrt(squareSum / 12.0));
                peaks[point] = peak;
                means[point] = static_cast<float>(mean / 12.0);
                channelPeak = std::max(channelPeak, peak);
            }
            peaks[segments] = peaks[0u];
            rms[segments] = rms[0u];
            means[segments] = means[0u];
            const double amplitude = std::clamp(pitch * 0.38, 1.6, 4.6);
            for (const bool usePeak : {true, false}) {
                const auto& levels = usePeak ? peaks : rms;
            auto envelope = owned(context.createGraphicsPath());
            if (envelope) {
                CPoint point = polar(radius + amplitude
                    * levels[0u] / channelPeak, 0.0);
                envelope->beginSubpath(point.x, point.y);
                for (uint32_t index = 1u; index <= segments; ++index) {
                    point = polar(radius + amplitude
                        * levels[index] / channelPeak,
                        static_cast<double>(index) / segments);
                    envelope->addLine(point.x, point.y);
                }
                for (uint32_t index = segments + 1u; index-- > 0u;) {
                    point = polar(radius - amplitude
                        * levels[index] / channelPeak,
                        static_cast<double>(index) / segments);
                    envelope->addLine(point.x, point.y);
                }
                envelope->closeSubpath();
                context.setFillColor(alphaColor(ref.slot == selectedSlot
                        ? 0xd8d8d8 : slotColors[ref.slot], usePeak ? 26 : 61));
                context.drawGraphicsPath(envelope,
                    CDrawContext::kPathFilled);
            }
            }
            auto contour = owned(context.createGraphicsPath());
            if (contour) {
                CPoint point = polar(radius + amplitude * 0.70
                    * means[0u] / channelPeak, 0.0);
                contour->beginSubpath(point.x, point.y);
                for (uint32_t index = 1u; index <= segments; ++index) {
                    point = polar(radius + amplitude * 0.70
                        * means[index] / channelPeak,
                        static_cast<double>(index) / segments);
                    contour->addLine(point.x, point.y);
                }
                context.setFrameColor(alphaColor(ref.slot == selectedSlot
                        ? 0xd8d8d8 : slotColors[ref.slot], 184));
                context.setLineWidth(0.72);
                context.drawGraphicsPath(contour,
                    CDrawContext::kPathStroked);
            }
        }
        for (uint32_t slot = 0u; slot < 4u; ++slot) {
            if (first[slot] < 0) continue;
            if (first[slot] > 0) {
                const double boundary = radiusForRing(
                    static_cast<uint32_t>(first[slot])) - pitch * 0.5;
                context.setFrameColor(color(0x4b6961));
                context.setLineWidth(3.0);
                context.drawEllipse(rect(center.x - boundary,
                    center.y - boundary, boundary * 2.0, boundary * 2.0),
                    kDrawStroked);
                context.setFrameColor(color(0x7fa397));
                context.setLineWidth(1.0);
                context.drawEllipse(rect(center.x - boundary,
                    center.y - boundary, boundary * 2.0, boundary * 2.0),
                    kDrawStroked);
            }
            const double labelRadius = (radiusForRing(
                static_cast<uint32_t>(first[slot])) + radiusForRing(
                static_cast<uint32_t>(last[slot]))) * 0.5;
            char label[32] {};
            std::snprintf(label, sizeof(label), "%c  %d-%d",
                static_cast<int>('A' + slot), first[slot] + 1,
                last[slot] + 1);
            text(context, label, center.x + labelRadius + 5.0,
                center.y - 7.0, 72.0, style.value, kLeftText, tinyFont);
        }
        if (ringCount == 0u) {
            text(context, "LOAD OR CAPTURE AUDIO TO ADD CHANNEL RINGS",
                center.x - 151.0, center.y - 7.0, 302.0, style.value,
                kCenterText);
            return;
        } else {
            char summary[64] {};
            const auto formationIndex = parameterIndex("Head Formation");
            const std::string formationName = formationIndex < parameters.size()
                ? paramText(parameters[formationIndex]) : "FREE";
            std::snprintf(summary, sizeof(summary), "%u RINGS / %s",
                ringCount, formationName.c_str());
            text(context, summary, center.x - 88.0, center.y - 7.0,
                176.0, style.value, kCenterText);
        }

        constexpr double visibleFloor = 0.015;
        const uint32_t mask = static_cast<uint32_t>(std::lround(paramNamed("Head Mask", 255.0)));
        std::array<SampleFamilyRingsHeadState, 8u> heads {};
        for (uint32_t head = 0u; head < 8u; ++head)
            if (config.callbacks.getRingsHeadState)
                config.callbacks.getRingsHeadState(config.callbacks.context, head, &heads[head]);
        const uint32_t formation = static_cast<uint32_t>(std::lround(paramNamed("Head Formation")));
        const uint32_t groupSize = formation == 1u ? 2u : formation == 2u ? 4u : formation == 3u ? 8u : 1u;
        constexpr uint32_t headColors[] {0xf4f4f4,0xdedede,0xc8c8c8,0xb2b2b2,0x9d9d9d,0x898989,0x767676,0x646464};
        const auto pointForRead = [&](uint32_t head, uint32_t side) {
            const auto& state = heads[head];
            return polar(radiusForRing(std::min(ringCount - 1u, side ? state.ringB : state.ringA)),
                side ? state.phaseB : state.phaseA);
        };
        // Link only members of the same radial formation, separately for each
        // actual read. Free heads have no formation rake.
        if (groupSize > 1u) {
            for (uint32_t leader = 0u; leader < 8u; leader += groupSize) {
                for (uint32_t side = 0u; side < 2u; ++side) {
                    auto rake = owned(context.createGraphicsPath());
                    if (!rake) continue;
                    bool started = false;
                    double alpha = 1.0;
                    for (uint32_t head = leader; head < leader + groupSize; ++head) {
                        if (!(mask & (1u << head)) || !heads[head].hasSource) continue;
                        const double mix = std::clamp(static_cast<double>(heads[head].mix), 0.0, 1.0);
                        if ((!side && mix >= 1.0 - visibleFloor) || (side && mix <= visibleFloor)) continue;
                        const auto point = pointForRead(head, side);
                        if (started) rake->addLine(point.x, point.y);
                        else rake->beginSubpath(point.x, point.y);
                        started = true;
                        alpha = side ? mix : 1.0 - mix;
                    }
                    if (!started) continue;
                    context.setFrameColor(alphaColor(leader == selectedHead ? 0xffffff : headColors[leader],
                        static_cast<uint8_t>(255.0 * std::max(0.16, alpha * 0.7))));
                    context.setLineWidth(1.4);
                    context.drawGraphicsPath(rake, CDrawContext::kPathStroked);
                }
            }
        }
        for (uint32_t head = 0u; head < 8u; ++head) {
            if (!(mask & (1u << head)) || !heads[head].hasSource) continue;
            const auto a = pointForRead(head, 0u), b = pointForRead(head, 1u);
            const double mix = std::clamp(static_cast<double>(heads[head].mix), 0.0, 1.0);
            const bool separate = heads[head].ringA != heads[head].ringB
                || std::abs(a.x - b.x) > 1.0 || std::abs(a.y - b.y) > 1.0;
            const bool blend = separate && mix > visibleFloor && mix < 1.0 - visibleFloor;
            const uint32_t ink = head == selectedHead ? 0xffffff : headColors[head];
            if (blend) {
                context.setFrameColor(alphaColor(ink, 97));
                context.setLineWidth(0.8);
                context.drawLine(a, b);
            }
            const auto dot = [&](const CPoint& point, double alpha, bool label) {
                context.setFillColor(color(0x050505));
                context.drawEllipse(rect(point.x - 6.0, point.y - 6.0, 12.0, 12.0), kDrawFilled);
                context.setFillColor(alphaColor(ink, static_cast<uint8_t>(255.0 * std::max(0.18, alpha))));
                context.drawEllipse(rect(point.x - 3.5, point.y - 3.5, 7.0, 7.0), kDrawFilled);
                if (label) {
                    char title[8] {};
                    std::snprintf(title, sizeof(title), "H%u", head + 1u);
                    text(context, title, point.x + 7.0, point.y - 8.0, 28.0, style.value, kLeftText, tinyFont);
                }
            };
            if (mix >= 1.0 - visibleFloor) dot(b, 1.0, true);
            else dot(a, blend ? 1.0 - mix : 1.0, true);
            if (blend) dot(b, mix, false);
        }
    }

    int32_t visualizationPointAt(const CPoint& point) const
    {
        if (!hasPathVisualization()) return -1;
        std::array<SampleFamilyVisualPoint, 256u> anchors {};
        const uint32_t count = visualPoints(1u, anchors);
        const CRect graph = visualizationGraphRect();
        if (config.visualization == SampleFamilyVisualization::Cutups
            && count > 0u && contains(graph, point)) {
            const double normalized = std::clamp(
                (point.x - graph.left) / graph.getWidth(), 0.0, 0.999999);
            return static_cast<int32_t>(std::min<uint32_t>(count - 1u,
                static_cast<uint32_t>(normalized * count)));
        }
        int32_t best = -1;
        double bestDistance = 10.0;
        for (uint32_t index = 0u; index < count; ++index) {
            const CPoint candidate = mapVisualPoint(graph, anchors[index]);
            const double dx = point.x - candidate.x;
            const double dy = point.y - candidate.y;
            const double distance = std::sqrt(dx * dx + dy * dy);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = static_cast<int32_t>(index);
            }
        }
        return best;
    }

    CPoint normalizedVisualizationPoint(const CPoint& point) const
    {
        const CRect graph = visualizationGraphRect();
        return CPoint(std::clamp((point.x - graph.left) / graph.getWidth(),
                          0.0, 1.0),
            std::clamp((point.y - graph.top) / graph.getHeight(), 0.0, 1.0));
    }

    void updateVisualizationPoint(const CPoint& point)
    {
        if (visualizationDragPoint < 0
            || (!config.callbacks.setVisualizationPoint
                && !config.callbacks.setVisualizationPointWithAlternate))
            return;
        const CPoint normalized = normalizedVisualizationPoint(point);
        if (config.callbacks.setVisualizationPointWithAlternate)
            config.callbacks.setVisualizationPointWithAlternate(
                config.callbacks.context,
                static_cast<uint32_t>(visualizationDragPoint),
                static_cast<float>(normalized.x),
                static_cast<float>(normalized.y),
                visualizationAlternateDrag);
        else
            config.callbacks.setVisualizationPoint(config.callbacks.context,
                static_cast<uint32_t>(visualizationDragPoint),
                static_cast<float>(normalized.x),
                static_cast<float>(normalized.y));
        invalid();
    }

    void drawMarkers(CDrawContext& context, const CRect& lane)
    {
        for (uint32_t index = 0u; index < config.markerCount; ++index) {
            const auto& marker = config.markers[index];
            const auto* info = parameterInfo(marker.parameterId);
            if (!info || !(info->maximum > info->minimum)) continue;
            const double normalized = std::clamp(
                (param(marker.parameterId) - info->minimum)
                    / (info->maximum - info->minimum), 0.0, 1.0);
            if (!waveformValueVisible(normalized)) continue;
            const double x = waveformXForNormalized(lane, normalized);
            context.setFrameColor(index < 2u ? style.accent : style.dim);
            context.setLineWidth(1.25);
            context.drawLine(CPoint(x, lane.top), CPoint(x, lane.bottom));
            text(context, marker.label ? marker.label : "",
                std::clamp(x + 3.0, lane.left + 3.0, lane.right - 28.0),
                lane.top + 16.0 + (index % 2u) * 15.0,
                26.0, style.label, kLeftText, tinyFont);
        }
    }

    uint32_t markerAt(const CPoint& point) const
    {
        if (config.visualization == SampleFamilyVisualization::Circulator) {
            const CRect lane = waveformSlotRect(selectedSlot);
            const double scale = selectedSlot == 0u && paramNamed("Loop Model") < 0.5 ? 0.5 : 1.0;
            const char* names[] {selectedSlot == 0u ? "Loop 1 Start" : "Loop 2 Start",
                selectedSlot == 0u ? "Loop 1 End" : "Loop 2 End"};
            uint32_t result = 0u;
            double distance = 11.0;
            for (const auto* name : names) {
                const auto index = parameterIndex(name);
                if (index >= parameters.size()) continue;
                const double candidate = std::abs(point.x - lane.left
                    - param(parameters[index].id) * scale * lane.getWidth());
                if (candidate < distance) { distance = candidate; result = parameters[index].id; }
            }
            return result;
        }
        if (!asset(selectedSlot)) return 0u;
        const CRect lane = waveformSlotRect(selectedSlot);
        uint32_t best = 0u;
        double bestDistance = 11.0;
        for (uint32_t index = 0u; index < config.markerCount; ++index) {
            const auto& marker = config.markers[index];
            const auto* info = parameterInfo(marker.parameterId);
            if (!info || !(info->maximum > info->minimum)) continue;
            const double normalized = std::clamp(
                (param(marker.parameterId) - info->minimum)
                    / (info->maximum - info->minimum), 0.0, 1.0);
            if (!waveformValueVisible(normalized)) continue;
            const double distance = std::abs(point.x
                - waveformXForNormalized(lane, normalized));
            if (distance < bestDistance) {
                bestDistance = distance;
                best = marker.parameterId;
            }
        }
        return best;
    }

    void updateMarker(const CPoint& point)
    {
        const auto* info = parameterInfo(markerDragParameter);
        if (!info) return;
        const CRect lane = waveformSlotRect(selectedSlot);
        double normalized = waveformNormalizedAtX(lane, point.x);
        if (config.visualization == SampleFamilyVisualization::Circulator) {
            const double scale = selectedSlot == 0u && paramNamed("Loop Model") < 0.5 ? 0.5 : 1.0;
            normalized = std::clamp(normalized / scale, 0.0, 1.0);
            const bool start = std::strstr(info->name, "Start") != nullptr;
            const double other = paramNamed(selectedSlot == 0u
                ? (start ? "Loop 1 End" : "Loop 1 Start")
                : (start ? "Loop 2 End" : "Loop 2 Start"), start ? 1.0 : 0.0);
            normalized = start ? std::min(normalized, std::max(0.0, other - 0.0001))
                : std::max(normalized, std::min(1.0, other + 0.0001));
        }
        markerEdit.set(info->minimum
            + normalized * (info->maximum - info->minimum));
        invalid();
    }

    std::size_t parameterIndex(const char* name) const
    {
        if (!name) return parameters.size();
        const auto found = std::find_if(parameters.begin(), parameters.end(),
            [name](const SampleFamilyParameterInfo& value) {
                return std::strcmp(value.name, name) == 0;
            });
        return found == parameters.end() ? parameters.size()
                                         : static_cast<std::size_t>(
                                             found - parameters.begin());
    }

    void addPanel(ParameterLayout& layout, const CRect& bounds,
        const char* title) const
    {
        layout.panels.push_back({bounds, title ? title : ""});
    }

    void addReferenceRow(ParameterLayout& layout, const char* parameterName,
        const char* label, const CRect& panel, double y, bool menu) const
    {
        const std::size_t index = parameterIndex(parameterName);
        if (index >= parameters.size()) return;
        const double controlX = panel.left + 104.0;
        const CRect rowBounds = rect(panel.left + 8.0, y - 8.0,
            panel.getWidth() - 16.0, 24.0);
        const CRect control = menu
            ? rect(controlX, y - 1.0, panel.getWidth() - 116.0, 15.0)
            : rect(controlX, y - 2.0, panel.right - 8.0 - controlX, 16.0);
        const CRect track = rect(controlX, y + 1.0,
            std::max(12.0, panel.getWidth() - 164.0), 9.0);
        layout.cells.push_back({index, rowBounds, control, track,
            label ? label : parameterName, menu});
    }

    void addProcessorRow(ParameterLayout& layout, const char* parameterName,
        const char* label, const CRect& cell, bool menu) const
    {
        const std::size_t index = parameterIndex(parameterName);
        if (index >= parameters.size()) return;
        addProcessorRow(layout, index, label, cell, menu);
    }

    void addProcessorRow(ParameterLayout& layout, std::size_t index,
        const char* label, const CRect& cell, bool menu) const
    {
        if (index >= parameters.size()) return;
        const double controlX = cell.left + 108.0;
        const CRect control = menu
            ? rect(controlX, cell.top - 1.0,
                std::max(12.0, cell.getWidth() - 124.0), 15.0)
            : rect(controlX, cell.top - 2.0,
                std::max(12.0, cell.right - 8.0 - controlX), 16.0);
        const double valueWidth = 42.0;
        const double trackWidth = std::max(12.0,
            std::min(150.0, cell.getWidth() - 108.0 - 16.0
                - valueWidth - 8.0));
        const CRect track = rect(controlX, cell.top + 1.0,
            trackWidth, 9.0);
        layout.cells.push_back({index,
            rect(cell.left + 8.0, cell.top - 8.0,
                cell.getWidth() - 16.0, 24.0),
            control, track, label ? label : parameters[index].name, menu});
    }

    void addProcessorRowById(ParameterLayout& layout, uint32_t parameterId,
        const char* label, const CRect& cell, bool menu) const
    {
        const auto found = std::find_if(parameters.begin(), parameters.end(),
            [parameterId](const SampleFamilyParameterInfo& value) {
                return value.id == parameterId;
            });
        if (found == parameters.end()) return;
        addProcessorRow(layout,
            static_cast<std::size_t>(found - parameters.begin()), label,
            cell, menu);
    }

    void buildWavesetsLayout(ParameterLayout& layout) const
    {
        const CRect playback = rect(18.0, 442.0, 300.0, 382.0);
        const CRect waveset = rect(330.0, 442.0, 300.0, 382.0);
        const CRect output = rect(642.0, 442.0, 320.0, 382.0);
        addPanel(layout, playback, "SAMPLE / PLAYBACK / PITCH");
        addPanel(layout, waveset, "WAVESETS / PROCESS");
        addPanel(layout, output, "OUTPUT / AMP / MIDI");
        const struct Row { const char* parameter; const char* label;
            double y; bool menu; } playbackRows[] {
            {"Play Mode", "PLAY MODE", 482.0, true},
            {"Stereo Source", "STEREO", 507.0, true},
            {"Start", "START", 532.0, false},
            {"End", "END", 557.0, false},
            {"Loop Start", "LOOP START", 582.0, false},
            {"Loop End", "LOOP END", 607.0, false},
            {"Voice Mode", "VOICE", 632.0, true},
            {"Trigger", "TRIGGER", 657.0, true},
            {"Root Note", "ROOT", 682.0, false},
            {"Tune", "TUNE", 707.0, false},
            {"Fine Tune", "FINE", 732.0, false},
        };
        for (const auto& row : playbackRows)
            addReferenceRow(layout, row.parameter, row.label, playback,
                row.y, row.menu);
        const Row wavesetRows[] {
            {"Time", "TIME", 482.0, true},
            {"Group", "GROUP", 507.0, true},
            {"Repeat", "REPEAT", 532.0, true},
            {"Stride", "STRIDE", 557.0, true},
            {"Order", "ORDER", 582.0, true},
            {"Process", "PROCESS", 607.0, true},
            {"Depth", "DEPTH", 632.0, false},
            {"Join", "JOIN", 657.0, false},
            {"Crossing Detail", "DETAIL", 682.0, true},
        };
        for (const auto& row : wavesetRows)
            addReferenceRow(layout, row.parameter, row.label, waveset,
                row.y, row.menu);
        const Row outputRows[] {
            {"Out", "OUT", 482.0, false},
            {"Attack", "ATTACK", 532.0, false},
            {"Release", "RELEASE", 557.0, false},
            {"Velocity", "VELOCITY", 582.0, false},
            {"Output Order", "ROUTE", 607.0, true},
            {"Voice Output", "VOICE OUT", 632.0, true},
            {"Stereo Pair Map", "PAIR MAP", 657.0, true},
            {"Output Count", "OUTPUTS", 682.0, true},
            {"MIDI Receive", "MIDI", 707.0, true},
        };
        for (const auto& row : outputRows)
            addReferenceRow(layout, row.parameter, row.label, output,
                row.y, row.menu);
    }

    void buildMotionLayout(ParameterLayout& layout) const
    {
        const CRect motion = rect(18.0, 442.0, 300.0, 382.0);
        const CRect motor = rect(330.0, 442.0, 300.0, 382.0);
        const CRect output = rect(642.0, 442.0, 320.0, 382.0);
        const int eventModel = static_cast<int>(std::lround(
            paramNamed("Segment Model")));
        const int articulation = static_cast<int>(std::lround(
            paramNamed("Articulation")));
        addPanel(layout, motion, "1  SOURCE FIELD + MOTION");
        addPanel(layout, motor, eventModel == 0
            ? "2  DIRECT PLAYBACK" : "2  EVENT PROCESS");
        addPanel(layout, output, outputRoutingPage
            ? "3  OUTPUT / ROUTING"
            : "3  OUTPUT / VOICE / PITCH / MIDI");
        const struct Row { const char* parameter; const char* label;
            double y; bool menu; } motionRows[] {
            {"Motion", "PATH", 482.0, true},
            {"Start", "START", 507.0, false},
            {"End", "END", 532.0, false},
            {"Locus", "LOCUS", 557.0, false},
            {"Field", "FIELD WIDTH", 582.0, false},
            {"Rate Basis", "RATE BASIS", 607.0, true},
            {"Motion Rate", "PLAY SPEED", 632.0, false},
            {"Travel", "TRAVEL", 657.0, false},
            {"Jitter", "SHARED RAND", 682.0, false},
            {"Step", "SOURCE STEP", 707.0, false},
        };
        for (const auto& row : motionRows)
            addReferenceRow(layout, row.parameter, row.label, motion,
                row.y, row.menu);
        if (eventModel != 0) {
            addReferenceRow(layout, "Segment Model", "SOUND", motor,
                482.0, true);
            addReferenceRow(layout, "Segment Trigger", "TRIGGER", motor,
                507.0, true);
            struct EventRow { const char* parameter; const char* label; };
            constexpr EventRow freeze[] {
                {"Event Rate","RATE"}, {"Field","LENGTH"},
                {"Pitch Scatter","PITCH RAND"},
                {"Level Variation","AMP RAND"}, {"Jitter","TIME RAND"},
                {"Join","SPLICE"},
            };
            constexpr EventRow iterate[] {
                {"Event Rate","RATE"}, {"Field","LENGTH"},
                {"Step","STEP"}, {"Pitch Scatter","PITCH RAND"},
                {"Level Variation","AMP RAND"}, {"Jitter","TIME RAND"},
            };
            constexpr EventRow pulser[] {
                {"Event Rate","RATE"}, {"Packet Duty","DURATION"},
                {"Field","SOURCE AREA"}, {"Pitch Scatter","PITCH RAND"},
                {"Level Variation","AMP RAND"}, {"Jitter","TIME RAND"},
            };
            constexpr EventRow doublets[] {
                {"Motion Rate","PLAY SPEED"}, {"Event Repeats","REPEATS"},
                {"Field","SEGMENT"}, {"Step","ADVANCE"},
                {"Join","SPLICE"}, {"Level Variation","AMP RAND"},
            };
            constexpr EventRow bounce[] {
                {"Event Rate","START RATE"}, {"Event Repeats","BOUNCES"},
                {"Field","LENGTH"}, {"Level Variation","DECAY"},
                {"Interval Curve","ACCEL"}, {"Packet Duty","MIN LENGTH"},
            };
            const EventRow* rows = eventModel == 1 ? freeze
                : (eventModel == 2 || eventModel == 6) ? iterate
                : eventModel == 3 ? pulser
                : eventModel == 4 ? doublets : bounce;
            for (uint32_t index = 0u; index < 6u; ++index)
                addReferenceRow(layout, rows[index].parameter,
                    rows[index].label, motor, 532.0 + index * 25.0, false);
            if (eventModel == 3) {
                addReferenceRow(layout, "Motor Envelope", "SHAPE", motor,
                    682.0, true);
                addReferenceRow(layout, "Event Overlap", "OVERLAP", motor,
                    707.0, true);
            } else {
                addReferenceRow(layout, "Event Overlap", "OVERLAP", motor,
                    682.0, true);
                addReferenceRow(layout, "Seed", "SEED", motor, 707.0,
                    false);
            }
            if (static_cast<int>(std::lround(
                    paramNamed("Segment Trigger"))) == 1)
                addReferenceRow(layout, "Inner Rate", "PACKET RATE", motor,
                    732.0, false);
        } else {
            addReferenceRow(layout, "Articulation", "SOUND", motor, 482.0,
                true);
            if (articulation == 1) {
                addReferenceRow(layout, "Motor Envelope", "SHAPE", motor,
                    507.0, true);
                addReferenceRow(layout, "Inner Rate", "INNER RATE", motor,
                    532.0, false);
                addReferenceRow(layout, "Outer Rate", "OUTER RATE", motor,
                    557.0, false);
                addReferenceRow(layout, "Packet Duty", "DUTY", motor,
                    582.0, false);
                addReferenceRow(layout, "Symmetry", "SYMMETRY", motor,
                    607.0, false);
                addReferenceRow(layout, "Join", "JOIN", motor, 632.0,
                    false);
                addReferenceRow(layout, "Seed", "SEED", motor, 657.0,
                    false);
            } else if (articulation == 2) {
                addReferenceRow(layout, "Inner Rate", "INNER RATE", motor,
                    507.0, false);
                addReferenceRow(layout, "Packet Duty", "DUTY", motor,
                    532.0, false);
                addReferenceRow(layout, "Join", "JOIN", motor, 557.0,
                    false);
                addReferenceRow(layout, "Seed", "SEED", motor, 582.0,
                    false);
            } else {
                addReferenceRow(layout, "Join", "JOIN", motor, 507.0,
                    false);
                addReferenceRow(layout, "Seed", "SEED", motor, 532.0,
                    false);
            }
        }
        const Row voiceRows[] {
            {"Out", "OUT", 482.0, false},
            {"Voice Mode", "VOICE", 507.0, true},
            {"Trigger", "TRIGGER", 532.0, true},
            {"Root Note", "ROOT", 557.0, false},
            {"Tune", "TUNE", 582.0, false},
            {"Fine Tune", "FINE", 607.0, false},
            {"Attack", "ATTACK", 632.0, false},
            {"Release", "RELEASE", 657.0, false},
            {"Velocity", "VELOCITY", 682.0, false},
            {"MIDI Receive", "MIDI", 707.0, true},
        };
        const Row routingRows[] {
            {"Out", "OUT", 482.0, false},
            {"Output Order", "ROUTE", 507.0, true},
            {"Voice Output", "VOICE OUT", 532.0, true},
            {"Stereo Pair Map", "PAIR MAP", 557.0, true},
            {"Output Count", "OUTPUTS", 582.0, true},
            {"Route On", "ROUTE ON", 607.0, true},
            {"Avoid Adjacent", "AVOID NEAR", 632.0, true},
            {"Attack", "ATTACK", 657.0, false},
            {"Release", "RELEASE", 682.0, false},
        };
        const Row* outputRows = outputRoutingPage
            ? routingRows : voiceRows;
        const std::size_t outputRowCount = outputRoutingPage
            ? std::size(routingRows) : std::size(voiceRows);
        for (std::size_t index = 0u; index < outputRowCount; ++index) {
            const auto& row = outputRows[index];
            addReferenceRow(layout, row.parameter, row.label, output,
                row.y, row.menu);
        }
    }

    void buildLaneFamilyLayout(ParameterLayout& layout) const
    {
        const bool grains = config.visualization
            == SampleFamilyVisualization::Grains;
        const bool cutups = config.visualization
            == SampleFamilyVisualization::Cutups;
        const CRect timing = rect(880.0, 54.0, 382.0,
            cutups ? 96.0 : 120.0);
        if (!grains) {
            char timingTitle[96] {};
            if (cutups) {
                char bpmName[32] {};
                std::snprintf(bpmName, sizeof(bpmName), "Lane %u BPM",
                    selectedSlot + 1u);
                const std::size_t bpmIndex = parameterIndex(bpmName);
                const std::string bpm = bpmIndex < parameters.size()
                    ? uppercaseAscii(paramText(parameters[bpmIndex]))
                    : "NO BPM";
                std::snprintf(timingTitle, sizeof(timingTitle),
                    "LANE %u / %s", selectedSlot + 1u, bpm.c_str());
            } else {
                std::snprintf(timingTitle, sizeof(timingTitle),
                    "LANE %u TIMING / STRETCH KEEPS PITCH",
                    selectedSlot + 1u);
            }
            addPanel(layout, timing, timingTitle);
        }

        const CRect loop = cutups ? rect(880.0, 162.0, 382.0, 188.0)
            : grains ? rect(880.0, 54.0, 382.0, 176.0)
                     : rect(880.0, 186.0, 382.0, 164.0);
        const CRect path = cutups ? rect(880.0, 362.0, 382.0, 308.0)
            : grains ? rect(880.0, 478.0, 382.0, 248.0)
                     : rect(880.0, 362.0, 382.0, 342.0);
        addPanel(layout, loop, cutups
            ? "1  CUT CLOCK / HOST DIVISION OR FREE RATE"
            : grains ? "1  SOURCE SCAN / NORMAL RATE IS FILE SPEED"
                     : "1  LOOP TRANSPORT");
        if (grains)
            addPanel(layout, rect(880.0, 242.0, 382.0, 224.0),
                "2  GRAIN SOURCE / WINDOW + TEXTURE");
        addPanel(layout, path, cutups
            ? "2  CUT ADDRESS / FILE + REGION ORDER"
            : grains ? "3  SOURCE FIELD PATH / WHICH FILE LANE"
                     : "2  SOURCE FIELD PATH");

        if (cutups) {
            addPanel(layout, rect(18.0, 554.0, 850.0, 116.0),
                "3  CUT PLAYBACK / JOIN + VARIATION");
            addPanel(layout, rect(18.0, 682.0, 1244.0, 130.0),
                outputRoutingPage ? "4  OUTPUT / ROUTING"
                                  : "4  VOICE / PITCH / MIDI");
            char bpmName[32] {};
            std::snprintf(bpmName, sizeof(bpmName), "Lane %u BPM",
                selectedSlot + 1u);
            addReferenceRow(layout, bpmName, "FILE BPM", timing,
                94.0, false);
            addReferenceRow(layout, "Tempo Sync", "TEMPO SYNC", timing,
                118.0, true);
            const struct Row { const char* parameter; const char* label;
                CRect panel; double y; bool menu; } rows[] {
                {"Cut Clock", "CLOCK", loop, 202.0, true},
                {"Division", "DIVISION", loop, 226.0, true},
                {"Free Rate", "FREE RATE", loop, 250.0, false},
                {"Swing", "SWING", loop, 274.0, false},
                {"Time Variation", "TIME VAR", loop, 298.0, false},
                {"File Order", "FILE ORDER", path, 402.0, true},
                {"Source Order", "SOURCE ORDER", path, 426.0, true},
                {"Poly Path", "POLY PATH", path, 450.0, true},
                {"Regions", "REGION MODE", path, 474.0, true},
                {"Steps / Regions", "STEPS / REGIONS", path, 498.0, false},
                {"Repeat", "STEP REPEAT", path, 522.0, false},
                {"Start", "START", path, 546.0, false},
                {"End", "END", path, 570.0, false},
                {"Seed", "SEED", path, 594.0, false},
                {"Transient Preroll", "ONSET PREROLL", path, 618.0, false},
                {"Join", "JOIN", rect(18.0,554.0,270.0,116.0),594.0,false},
                {"Gate", "GATE", rect(18.0,554.0,270.0,116.0),618.0,false},
                {"Reverse Chance", "REVERSE", rect(300.0,554.0,270.0,116.0),594.0,false},
                {"Pitch Variation", "PITCH VAR", rect(300.0,554.0,270.0,116.0),618.0,false},
                {"Level Variation", "LEVEL VAR", rect(582.0,554.0,286.0,116.0),594.0,false},
            };
            for (const auto& row : rows)
                addReferenceRow(layout, row.parameter, row.label, row.panel,
                    row.y, row.menu);
            const CRect column1 = rect(18.0,682.0,392.0,130.0);
            const CRect column2 = rect(422.0,682.0,392.0,130.0);
            const CRect column3 = rect(826.0,682.0,436.0,130.0);
            if (outputRoutingPage) {
                const struct OutputRow { const char* parameter;
                    const char* label; CRect panel; double y; } routes[] {
                    {"Output Mode","SOURCE MODE",column1,722.0},
                    {"Active Outputs","ACTIVE OUT",column1,746.0},
                    {"Allocation Clock","ALLOC CLOCK",column1,770.0},
                    {"Output Traversal","TRAVERSAL",column2,722.0},
                    {"Output Voice Width","CUT WIDTH",column2,746.0},
                    {"Stereo Pair Layout","PAIR LAYOUT",column2,770.0},
                    {"Avoid Adjacent","AVOID NEAR",column3,722.0},
                };
                for (const auto& row : routes)
                    addReferenceRow(layout, row.parameter, row.label,
                        row.panel, row.y, true);
            } else {
                const struct OutputRow { const char* parameter;
                    const char* label; CRect panel; double y; bool menu; }
                    voices[] {
                    {"Out","OUT",column1,722.0,false},
                    {"Pan","PAN",column1,746.0,false},
                    {"Voice Mode","VOICE",column1,770.0,true},
                    {"Trigger","TRIGGER",column1,794.0,true},
                    {"Root Note","ROOT",column2,722.0,false},
                    {"Tune","TUNE",column2,746.0,false},
                    {"Fine Tune","FINE",column2,770.0,false},
                    {"MIDI Receive","MIDI",column2,794.0,true},
                    {"Attack","ATTACK",column3,722.0,false},
                    {"Release","RELEASE",column3,746.0,false},
                    {"Velocity","VELOCITY",column3,770.0,false},
                };
                for (const auto& row : voices)
                    addReferenceRow(layout, row.parameter, row.label,
                        row.panel, row.y, row.menu);
            }
            return;
        }

        if (grains) {
            const CRect grain = rect(880.0, 242.0, 382.0, 224.0);
            const CRect event = rect(18.0, 554.0, 850.0, 172.0);
            const CRect voice = rect(18.0, 738.0, 1244.0, 174.0);
            addPanel(layout, event,
                "4  GRAIN PROCESS / TIMING / VARIATION");
            addPanel(layout, voice,
                outputRoutingPage ? "5  OUTPUT / ROUTING"
                    : "5  POLYPHONY / SCAN RATE / MIDI / STEREO");
            const struct Row { const char* parameter; const char* label;
                CRect panel; double y; bool menu; } rows[] {
                {"Transport", "DIRECTION", loop,94.0,true},
                {"Rate Basis", "RATE BASIS", loop,118.0,true},
                {"Rate", "SCAN SPEED", loop,142.0,false},
                {"Start", "START", loop,166.0,false},
                {"End", "END", loop,190.0,false},
                {"Source Advance", "SOURCE ADVANCE", loop,214.0,true},
                {"Grain Source", "SOURCE MODE", grain,282.0,true},
                {"Grain Envelope", "ENVELOPE", grain,306.0,true},
                {"Density", "DENSITY", grain,330.0,false},
                {"Grain Size", "GRAIN SIZE", grain,354.0,false},
                {"Source Position", "POSITION", grain,378.0,false},
                {"Position Spray", "POSITION SPRAY", grain,402.0,false},
                {"Pitch Spray", "PITCH SPRAY", grain,426.0,false},
                {"Reverse Chance", "REVERSE", grain,450.0,false},
                {"Lane Path", "PATH", path,518.0,true},
                {"Lane Change", "LANE CHANGE", path,542.0,true},
                {"Path Shape", "SHAPE", path,566.0,true},
                {"Path Cycles", "CYCLES", path,590.0,false},
                {"Path Offset", "OFFSET", path,614.0,false},
                {"Path Skew", "SKEW", path,638.0,false},
                {"Path Curve", "CURVE", path,662.0,false},
                {"Seed", "SEED", path,686.0,false},
                {"Grain Timing", "TIMING", rect(18.0,554.0,270.0,172.0),594.0,true},
                {"Timing Scatter", "SCATTER DEPTH", rect(18.0,554.0,270.0,172.0),618.0,false},
                {"Mutate Process", "GRAIN PROCESS", rect(18.0,554.0,270.0,172.0),642.0,true},
                {"Mutate Amount", "AMOUNT", rect(18.0,554.0,270.0,172.0),666.0,false},
                {"Source-time Sync", "SOURCE SYNC", rect(18.0,554.0,270.0,172.0),690.0,true},
                {"Size Variation", "SIZE VAR", rect(300.0,554.0,270.0,172.0),594.0,false},
                {"Level Variation", "LEVEL VAR", rect(300.0,554.0,270.0,172.0),618.0,false},
                {"Envelope Skew", "ENV SKEW", rect(300.0,554.0,270.0,172.0),642.0,false},
                {"Position Bias", "SPRAY BIAS", rect(582.0,554.0,286.0,172.0),594.0,true},
                {"Regions", "REGIONS", rect(582.0,554.0,286.0,172.0),618.0,false},
                {"Grain Pitch Shift", "PITCH SHIFT", rect(582.0,554.0,286.0,172.0),642.0,false},
            };
            for (const auto& row : rows)
                addReferenceRow(layout, row.parameter, row.label, row.panel,
                    row.y, row.menu);
            const CRect column1 = rect(18.0,738.0,392.0,174.0);
            const CRect column2 = rect(422.0,738.0,392.0,174.0);
            const CRect column3 = rect(826.0,738.0,436.0,174.0);
            if (outputRoutingPage) {
                const struct OutputRow { const char* parameter;
                    const char* label; CRect panel; double y; } routes[] {
                    {"Output Mode","ROUTING MODE",column1,776.0},
                    {"Active Outputs","ACTIVE OUT",column1,800.0},
                    {"Output Traversal","TRAVERSAL",column2,776.0},
                    {"Output Voice Width","GRAIN WIDTH",column2,800.0},
                    {"Stereo Pair Layout","PAIR LAYOUT",column2,824.0},
                    {"Avoid Adjacent","AVOID NEAR",column3,776.0},
                };
                for (const auto& row : routes)
                    addReferenceRow(layout, row.parameter, row.label,
                        row.panel, row.y, true);
            } else {
                const struct OutputRow { const char* parameter;
                    const char* label; CRect panel; double y; bool menu; }
                    voices[] {
                    {"Out","OUT",column1,776.0,false},
                    {"Pan","PAN",column1,800.0,false},
                    {"Voice Mode","POLYPHONY",column1,824.0,true},
                    {"Trigger","TRIGGER",column1,848.0,true},
                    {"MIDI Receive","MIDI",column1,872.0,true},
                    {"Scan Root Note","SCAN ROOT",column2,776.0,false},
                    {"Scan Tune","SCAN TUNE",column2,800.0,false},
                    {"Scan Fine Tune","SCAN FINE",column2,824.0,false},
                    {"Attack","ATTACK",column2,848.0,false},
                    {"Release","RELEASE",column2,872.0,false},
                    {"Channel Mode","CHANNEL MODE",column3,776.0,true},
                    {"Stereo Link","STEREO LINK",column3,800.0,true},
                    {"Mono Spread","MONO SPREAD",column3,824.0,false},
                    {"Velocity","VELOCITY",column3,848.0,false},
                };
                for (const auto& row : voices)
                    addReferenceRow(layout, row.parameter, row.label,
                        row.panel, row.y, row.menu);
            }
            return;
        }

        const CRect voice = rect(18.0, 554.0, 850.0, 150.0);
        addPanel(layout, voice, outputRoutingPage
            ? "3  OUTPUT / ROUTING" : "3  VOICE / PITCH / MIDI");
        char speedName[32] {};
        char stretchName[32] {};
        char nudgeName[32] {};
        std::snprintf(speedName, sizeof(speedName), "Lane %u Speed",
            selectedSlot + 1u);
        std::snprintf(stretchName, sizeof(stretchName), "Lane %u Stretch",
            selectedSlot + 1u);
        std::snprintf(nudgeName, sizeof(nudgeName), "Lane %u Nudge",
            selectedSlot + 1u);
        const struct Row { const char* parameter; const char* label;
            CRect panel; double y; bool menu; } rows[] {
            {speedName,"SPEED",timing,94.0,false},
            {stretchName,"STRETCH",timing,120.0,false},
            {nudgeName,"WRAP NUDGE",timing,146.0,false},
            {"Transport","DIRECTION",loop,222.0,true},
            {"Rate Basis","RATE BASIS",loop,244.0,true},
            {"Rate","PLAY SPEED",loop,266.0,false},
            {"Start","START",loop,288.0,false},
            {"End","END",loop,310.0,false},
            {"Loop Crossfade","LOOP JOIN",loop,332.0,false},
            {"Lane Path","PATH",path,405.0,true},
            {"Lane Change","LANE CHANGE",path,430.0,true},
            {"Path Shape","SHAPE",path,455.0,true},
            {"Path Cycles","CYCLES",path,480.0,false},
            {"Path Offset","OFFSET",path,505.0,false},
            {"Path Skew","SKEW",path,530.0,false},
            {"Path Curve","CURVE",path,555.0,false},
            {"Lane Slew","JUMP SLEW",path,580.0,false},
            {"Seed","SEED",path,605.0,false},
        };
        for (const auto& row : rows)
            addReferenceRow(layout, row.parameter, row.label, row.panel,
                row.y, row.menu);
        const CRect column1 = rect(18.0,554.0,270.0,150.0);
        const CRect column2 = rect(300.0,554.0,270.0,150.0);
        const CRect column3 = rect(582.0,554.0,286.0,150.0);
        if (outputRoutingPage) {
            const struct OutputRow { const char* parameter;
                const char* label; CRect panel; double y; } routes[] {
                {"Output Mode","SOURCE MODE",column1,592.0},
                {"Active Outputs","ACTIVE OUT",column1,618.0},
                {"Allocation Clock","ALLOC CLOCK",column1,644.0},
                {"Output Traversal","TRAVERSAL",column2,592.0},
                {"Output Voice Width","VOICE WIDTH",column2,618.0},
                {"Stereo Pair Layout","PAIR LAYOUT",column2,644.0},
                {"Avoid Adjacent","AVOID NEAR",column3,592.0},
            };
            for (const auto& row : routes)
                addReferenceRow(layout, row.parameter, row.label,
                    row.panel, row.y, true);
        } else {
            const struct OutputRow { const char* parameter;
                const char* label; CRect panel; double y; bool menu; }
                voices[] {
                {"Out","OUT",column1,592.0,false},
                {"Pan","PAN",column1,616.0,false},
                {"Voice Mode","VOICE",column1,640.0,true},
                {"Trigger","TRIGGER",column1,664.0,true},
                {"Root Note","ROOT",column2,592.0,false},
                {"Tune","TUNE",column2,616.0,false},
                {"Fine Tune","FINE",column2,640.0,false},
                {"Attack","ATTACK",column2,664.0,false},
                {"Release","RELEASE",column3,592.0,false},
                {"Velocity","VELOCITY",column3,616.0,false},
                {"MIDI Receive","MIDI",column3,640.0,true},
            };
            for (const auto& row : voices)
                addReferenceRow(layout, row.parameter, row.label,
                    row.panel, row.y, row.menu);
        }
    }

    void buildDoublesLayout(ParameterLayout& layout) const
    {
        const CRect decks = rect(18.0, 306.0, 494.0, 210.0);
        const CRect phase = rect(528.0, 306.0, 494.0, 210.0);
        const CRect transport = rect(18.0, 528.0, 1004.0, 230.0);
        addPanel(layout, decks, "OUTPUT / DECKS / SOURCE");
        addPanel(layout, phase, "DECK B PHASE / CUT / MIDI");
        addPanel(layout, transport, "TRANSPORT / CROSSFADER");
        const struct Row { const char* parameter; const char* label;
            CRect panel; double y; bool menu; } rows[] {
            {"Out","OUT",decks,342.0,false},
            {"Speed","SPEED",decks,368.0,false},
            {"Sample BPM","SAMPLE BPM",decks,394.0,false},
            {"Start","START",decks,420.0,false},
            {"End","END",decks,446.0,false},
            {"Cue Preroll","CUE PREROLL",decks,472.0,false},
            {"Deck B Offset","B OFFSET",phase,342.0,false},
            {"Phase Drift","B DRIFT",phase,368.0,false},
            {"Deck B Live Phase","B LIVE PHASE",phase,394.0,false},
            {"Phase Step","PHASE STEP",phase,420.0,true},
            {"Loop","LOOP",phase,446.0,true},
            {"Crossfader Curve","XFADE CURVE",phase,472.0,true},
            {"MIDI Receive","MIDI RECEIVE",phase,498.0,true},
            {"Deck A Level","A LEVEL",decks,664.0,false},
            {"Deck B Level","B LEVEL",phase,664.0,false},
        };
        for (const auto& row : rows)
            addReferenceRow(layout, row.parameter, row.label, row.panel,
                row.y, row.menu);
        const std::size_t crossfader = parameterIndex("Crossfader");
        if (crossfader < parameters.size()) {
            layout.cells.push_back({crossfader,
                rect(143.0, 706.0, 754.0, 38.0),
                rect(172.0, 708.0, 696.0, 30.0),
                rect(172.0, 716.0, 696.0, 14.0),
                "__CROSSFADER__", false});
        }
    }

    void drawDoublesSpecials(CDrawContext& context)
    {
        SampleFamilyDoublesState state {};
        if (config.callbacks.getDoublesState)
            config.callbacks.getDoublesState(config.callbacks.context, &state);
        const auto button = [&](const CRect& bounds, const std::string& label,
                                bool active = false, CColor accent = color(0xc5c5c5)) {
            context.setFillColor(active ? color(0x303030) : style.strip);
            context.drawRect(bounds, kDrawFilled);
            context.setFrameColor(active ? accent : style.grid);
            context.setLineWidth(1.0);
            context.drawRect(bounds, kDrawStroked);
            foundation::drawTextInRect(context, label, bounds, style.label, font);
        };
        button(rect(568.0,263.0,44.0,17.0),
            "1/2");
        button(rect(616.0,263.0,52.0,17.0), "AUTO", state.automaticTempo);
        button(rect(672.0,263.0,44.0,17.0), "X2");
        const std::size_t linkIndex = parameterIndex("Link Decks");
        const bool linked = linkIndex < parameters.size()
            && param(parameters[linkIndex].id) >= 0.5;
        button(rect(554.0,558.0,80.0,28.0), "LINK", linked);
        for (uint32_t index = 0u; index < config.actionCount; ++index) {
            const char* source = config.actions[index].label
                ? config.actions[index].label : "";
            std::string label = source;
            if (label == "RESTART") label = "RESTART / 36";
            else if (label == "PLAY") label = "PLAY / 43";
            else if (label == "STOP") label = "STOP / 37";
            else if (label == "SYNC") label = "SYNC / 38";
            else if (label == "STEP -") label = "STEP - / 39";
            else if (label == "STEP +") label = "STEP + / 42";
            else if (label == "DECK A") label = "A P/P / 44";
            else if (label == "DECK B") label = "B P/P / 45";
            else if (label == "PUNCH A") label = "PUNCH A / 40";
            else if (label == "PUNCH B") label = "PUNCH B / 41";
            else if (label == "DRAG A") label = "DRAG A / 46";
            else if (label == "DRAG B") label = "DRAG B / 47";
            else if (label == "SET CUE A") label = "CUE A / 61";
            else if (label == "SET CUE B") label = "CUE B / 63";
            else if (label == "TRIGGER A") label = "TRIG A / 62";
            else if (label == "TRIGGER B") label = "TRIG B / 64";
            bool active = heldAction == index + 1u
                || (state.activeActions & config.actions[index].actionId) != 0u;
            const std::string action(source);
            if (action == "DECK A") active |= (state.activeDecks & 1u) != 0u;
            else if (action == "DECK B") active |= (state.activeDecks & 2u) != 0u;
            else if (action == "PLAY") active |= state.playing;
            else if (action == "STOP") active |= !state.playing;
            else if (action == "SET CUE A") active |= (state.cueMask & 1u) != 0u;
            else if (action == "SET CUE B") active |= (state.cueMask & 2u) != 0u;
            const bool deckA = !action.empty() && action.back() == 'A';
            const bool deckB = !action.empty() && action.back() == 'B';
            button(actionButtonRect(index), label, active,
                deckA ? color(0x69d2dc) : deckB ? color(0xff7047) : style.accent);
        }
        const std::size_t crossfaderIndex = parameterIndex("Crossfader");
        if (crossfaderIndex < parameters.size()) {
            const auto& info = parameters[crossfaderIndex];
            const double normalized = std::clamp((param(info.id)
                    - info.minimum) / (info.maximum - info.minimum),
                0.0, 1.0);
            const CRect track = rect(172.0, 716.0, 696.0, 14.0);
            context.setFillColor(style.strip);
            context.drawRect(track, kDrawFilled);
            context.setFillColor(color(0x25383b));
            context.drawRect(rect(track.left, track.top,
                track.getWidth() * 0.5, track.getHeight()), kDrawFilled);
            context.setFillColor(color(0x3b2822));
            context.drawRect(rect(track.left + track.getWidth() * 0.5,
                track.top, track.getWidth() * 0.5, track.getHeight()),
                kDrawFilled);
            context.setFrameColor(style.grid);
            context.drawRect(track, kDrawStroked);
            const double x = track.left + normalized * track.getWidth();
            context.setFillColor(style.text);
            context.drawRect(rect(x - 7.0, track.top - 8.0, 14.0,
                track.getHeight() + 16.0), kDrawFilled);
            text(context, "A", 151.0, 711.0, 18.0, color(0x69d2dc),
                kCenterText);
            text(context, "B", 871.0, 711.0, 18.0, color(0xff7047),
                kCenterText);
        }
        const CRect help = rect(18.0, 770.0, 1004.0, 50.0);
        context.setFillColor(style.cell);
        context.drawRect(help, kDrawFilled);
        context.setFrameColor(style.grid);
        context.drawRect(help, kDrawStroked);
        text(context, "NOTES  36 RST  37 STOP  38 SYNC  39/42 STEP  40/41 PUNCH  44/45 P/P  46/47 DRAG  61/63 CUE  62/64 TRIG",
            30.0, 780.0, 980.0, style.text, kLeftText, tinyFont);
        text(context, "48-60 OFFSET+SYNC  /  CC16 XFADE  CC17 A LVL  CC18 B LVL  CC19 LIVE PHASE  /  VOL=PUNCH/DRAG DEPTH",
            30.0, 799.0, 980.0, style.dim, kLeftText, tinyFont);
    }

    void drawMotionSpecials(CDrawContext& context)
    {
        const int model = static_cast<int>(std::lround(
            paramNamed("Segment Model")));
        if (model == 0) {
            text(context, "LINK  FULL PATH FOLLOW", 340.0, 690.0, 280.0,
                style.accent, kLeftText, tinyFont);
            text(context, "SOURCE  LIVE MOTION CURSOR", 340.0, 703.0,
                280.0, style.label, kLeftText, tinyFont);
            text(context, "MOTION  POSITION / PLAY SPEED / DIRECTION",
                340.0, 716.0, 280.0, style.label, kLeftText, tinyFont);
            return;
        }
        const int motion = static_cast<int>(std::lround(paramNamed("Motion")));
        const int trigger = static_cast<int>(std::lround(
            paramNamed("Segment Trigger")));
        const double step = paramNamed("Step");
        const char* link = ((model == 2 || model == 6) && step <= 0.0)
            ? "FULL PATH FOLLOW"
            : model == 3 && motion == 6 ? "MOVING FIELD"
            : model == 5 && trigger != 0
                ? "POSITION OVERRIDE / ACCEL NEEDS CLOCK"
            : model == 4 && trigger != 0
                ? "POSITION OVERRIDE / CLOCK MAKES CONTIGUOUS GROUPS"
            : "POSITION OVERRIDE";
        const char* source = (model == 1 || model == 5) ? "LOCUS LOCK"
            : (model == 2 || model == 6)
                ? step <= 0.0 ? "LIVE MOTION CURSOR"
                              : "LOCUS + STEP SEQUENCE"
            : model == 3 ? motion == 6 ? "RANDOM IN MOVING FIELD"
                                       : "RANDOM IN LOCUS FIELD"
            : model == 4 ? "LOCUS + STEP GROUPS"
                         : "LIVE MOTION CURSOR";
        const char* description = model == 1
            ? "REPEATS ONE LOCUS SLICE / FLUID FREEZE"
            : model == 2
                ? "MOVING ITERATIONS / NATURAL TIME, PITCH + LEVEL DRIFT"
            : model == 3
                ? "SHORT ENVELOPED PACKETS FROM RANDOM SOURCE POINTS"
            : model == 4 ? "GROUPED SOURCE SLICES / AAABBBCCC"
            : model == 5
                ? "ACCELERATING, DECAYING + SHRINKING REPEATS"
                : "NATURAL ITERATIONS / NEW OUTPUT PER EVENT";
        const char* motionRole = ((model == 2 || model == 6) && step <= 0.0)
            ? trigger == 1
                ? "POSITION / SPEED / DIRECTION / PACKET CLOCK"
                : trigger == 2
                    ? "POSITION / SPEED / DIRECTION / TURN CLOCK"
                    : "POSITION / SPEED / DIRECTION"
            : (model == 3 && motion == 6)
                ? trigger == 1
                    ? "MOVING FIELD / SPEED / DIRECTION / PACKET CLOCK"
                    : trigger == 2
                        ? "MOVING FIELD / SPEED / DIRECTION / TURN CLOCK"
                        : "MOVING FIELD / SPEED / DIRECTION"
                : trigger == 1
                    ? "PLAY SPEED / ONSET DIRECTION / PACKET CLOCK"
                    : trigger == 2
                        ? "PLAY SPEED / ONSET DIRECTION / TURN CLOCK"
                        : "PLAY SPEED / ONSET DIRECTION";
        const double y = trigger == 1 ? 758.0 : 738.0;
        text(context, std::string("LINK  ") + link, 340.0, y, 280.0,
            style.accent, kLeftText, tinyFont);
        text(context, std::string("SOURCE  ") + source, 340.0, y + 13.0,
            280.0, style.label, kLeftText, tinyFont);
        text(context, std::string("MOTION  ") + motionRole, 340.0,
            y + 26.0, 280.0, style.label, kLeftText, tinyFont);
        text(context, description, 340.0, y + 39.0, 280.0, style.dim,
            kLeftText, tinyFont);
    }

    void buildRingsLayout(ParameterLayout& layout) const
    {
        const CRect capture = rect(850.0, 42.0, 488.0, 134.0);
        const CRect motion = rect(850.0, 186.0, 488.0, 254.0);
        const CRect head = rect(850.0, 450.0, 488.0, 166.0);
        const CRect slot = rect(850.0, 626.0, 488.0, 192.0);
        addPanel(layout, capture, "1  INPUT / CAPTURE");
        addPanel(layout, motion, "2  FIELD PATH / HEAD CLOCKS");
        char headTitle[64] {};
        std::snprintf(headTitle, sizeof(headTitle),
            "3  HEAD H%u / EFFECTIVE READ", selectedHead + 1u);
        addPanel(layout, head, headTitle);
        char slotTitle[64] {};
        std::snprintf(slotTitle, sizeof(slotTitle),
            "4  SLOT %c / LOOP CLOCK", static_cast<int>('A' + selectedSlot));
        addPanel(layout, slot, slotTitle);
        const auto cell = [](const CRect& panel, uint32_t column,
                              uint32_t row, double top = 10.0) {
            const double width = (panel.getWidth() - 10.0) * 0.5;
            return rect(panel.left + column * (width + 10.0),
                panel.top + 27.0 + top + row * 27.0, width, 24.0);
        };
        const struct Row { const char* parameter; const char* label;
            CRect panel; uint32_t column; uint32_t row; bool menu; } rows[] {
            {"Capture Target","TARGET",capture,0u,0u,true},
            {"Capture Mode","MODE",capture,1u,0u,true},
            {"Input Width","INPUT WIDTH",capture,0u,1u,true},
            {"Capture Quantize","QUANTIZE",capture,1u,1u,true},
            {"Monitor","MONITOR",capture,0u,2u,true},
            {"Overdub Feedback","OVERDUB FB",capture,1u,2u,false},
            {"Radial Path","RADIAL PATH",motion,0u,0u,true},
            {"Head Formation","FORMATION",motion,1u,0u,true},
            {"Ring Position","RING POSITION",motion,0u,1u,false},
            {"Head Relationship","ANGULAR MODEL",motion,1u,1u,true},
            {"Radial Ratio","RADIAL RATIO",motion,0u,2u,false},
            {"Playback Rate","MASTER RATE",motion,1u,2u,false},
            {"Path Depth","PATH DEPTH",motion,0u,3u,false},
            {"Relationship Amount","ANGLE AMOUNT",motion,1u,3u,false},
            {"Path Offset","PATH OFFSET",motion,0u,4u,false},
            {"Head Pivot","HEAD PIVOT",motion,1u,4u,false},
            {"Formation Spread","FORM SPREAD",motion,0u,5u,false},
            {"Head Drift","ANGLE DRIFT",motion,1u,5u,false},
            {"Ring Blend","RING BLEND",motion,0u,6u,false},
            {"Relationship Glide","ANGLE GLIDE",motion,1u,6u,false},
            {"Path Slew","PATH SLEW",motion,0u,7u,false},
            {"Motion Seed","MOTION SEED",motion,1u,7u,false},
        };
        for (const auto& row : rows)
            addProcessorRow(layout, row.parameter, row.label,
                cell(row.panel, row.column, row.row), row.menu);

        SampleFamilyRingsHeadState headState {};
        const bool haveHeadState = config.callbacks.getRingsHeadState
            && config.callbacks.getRingsHeadState(config.callbacks.context,
                selectedHead, &headState);
        const bool radialManual = static_cast<uint32_t>(std::lround(
            paramNamed("Radial Path"))) == 7u;
        const bool angularManual = static_cast<uint32_t>(std::lround(
            paramNamed("Head Relationship"))) == 7u;
        if (haveHeadState && radialManual)
            addProcessorRowById(layout, headState.manualRingParameterId,
                "MANUAL RING", cell(head, 0u, 0u, 38.0), false);
        if (haveHeadState && angularManual) {
            addProcessorRowById(layout, headState.manualPhaseParameterId,
                "MANUAL PHASE", cell(head, 1u, 0u, 38.0), false);
            addProcessorRowById(layout, headState.manualRateParameterId,
                "MANUAL RATE", cell(head, 0u, 1u, 38.0), false);
        }

        const CRect output = rect(18.0, 732.0, 820.0, 60.0);
        addProcessorRow(layout, "Output Gain", "GAIN",
            rect(output.left + 68.0, output.top + 8.0,240.0,24.0),false);
        addProcessorRow(layout, "Output Format", "FORMAT",
            rect(output.left + 316.0,output.top + 8.0,240.0,24.0),true);
        addProcessorRow(layout, "Output Rotation", "ROTATE",
            rect(output.left + 564.0,output.top + 8.0,240.0,24.0),false);
        addProcessorRow(layout, "MIDI Mode", "MIDI",
            rect(output.left + 68.0,output.top + 34.0,240.0,24.0),true);
        addProcessorRow(layout, "MIDI Root", "ROOT",
            rect(output.left + 316.0,output.top + 34.0,240.0,24.0),false);

        constexpr const char* suffixes[] {
            "Start", "End", "Speed", "Stretch", "Pitch", "Wrap Nudge",
            "Gain",
        };
        constexpr const char* labels[] {
            "START", "END", "SPEED", "STRETCH", "PITCH", "WRAP NUDGE",
            "GAIN",
        };
        constexpr uint8_t columns[] {0u,1u,0u,1u,0u,1u,0u};
        constexpr uint8_t rowNumbers[] {0u,0u,1u,1u,2u,2u,3u};
        for (uint32_t index = 0u; index < 7u; ++index) {
            char name[64] {};
            std::snprintf(name, sizeof(name), "Slot %c %s",
                static_cast<int>('A' + selectedSlot), suffixes[index]);
            addProcessorRow(layout, name, labels[index],
                cell(slot, columns[index], rowNumbers[index]), false);
        }
        addProcessorRow(layout, "Loop Join", "LOOP JOIN",
            cell(slot, 1u, 3u), false);
        addProcessorRow(layout, "Seam Duck", "SEAM DUCK",
            cell(slot, 0u, 4u), false);
    }

    void drawRingsSpecials(CDrawContext& context)
    {
        const CRect output = rect(18.0, 732.0, 820.0, 60.0);
        text(context, "OUTPUT", output.left + 10.0, output.top + 25.0,
            52.0, style.label);
        const SampleFamilyTransportState transport
            = config.callbacks.getTransportState
            ? config.callbacks.getTransportState(config.callbacks.context)
            : paramNamed("Play", 1.0) >= 0.5
                ? SampleFamilyTransportState::Playing
                : SampleFamilyTransportState::Paused;
        const bool playing = transport == SampleFamilyTransportState::Playing;
        const bool paused = transport == SampleFamilyTransportState::Paused;
        const bool stopped = transport == SampleFamilyTransportState::Stopped;
        const bool recording = paramNamed("Capture") >= 0.5;
        const bool radialReverse = paramNamed("Reverse Radial Path") >= 0.5;
        const bool angularReverse = paramNamed("Reverse Angular Motion")
            >= 0.5;
        const uint32_t radialPath = static_cast<uint32_t>(std::lround(
            paramNamed("Radial Path")));
        const bool radialMoves = radialPath != 0u && radialPath != 7u;
        const uint32_t headMask = static_cast<uint32_t>(std::lround(
            paramNamed("Head Mask", 255.0)));
        const uint32_t selectedBit = 1u << selectedHead;
        char reverseName[64] {};
        std::snprintf(reverseName, sizeof(reverseName), "Slot %c Reverse",
            static_cast<int>('A' + selectedSlot));
        const bool reverse = paramNamed(reverseName) >= 0.5;
        foundation::drawButton(context, rect(470.0,46.0,130.0,19.0),
            storageLabel(selectedSlot), font);
        foundation::drawButton(context, rect(606.0,46.0,52.0,19.0),
            "PLAY", font, playing);
        foundation::drawButton(context, rect(664.0,46.0,58.0,19.0),
            "PAUSE", font, paused);
        foundation::drawButton(context, rect(728.0,46.0,48.0,19.0),
            "STOP", font, stopped);
        foundation::drawButton(context, rect(782.0,46.0,44.0,19.0),
            "SYNC", font);
        foundation::drawButton(context, rect(1238.0,46.0,88.0,19.0),
            recording ? "STOP" : "RECORD", font, recording);
        foundation::drawButton(context, rect(1158.0,190.0,80.0,19.0),
            radialMoves ? "RAD REV" : "RAD REV —", font,
            radialMoves && radialReverse);
        foundation::drawButton(context, rect(1246.0,190.0,80.0,19.0),
            "ANG REV", font, angularReverse);
        foundation::drawButton(context, rect(1198.0,454.0,62.0,19.0),
            (headMask & selectedBit) == 0u ? "UNMUTE" : "MUTE", font,
            (headMask & selectedBit) == 0u);
        foundation::drawButton(context, rect(1266.0,454.0,60.0,19.0),
            headMask == selectedBit ? "UNSOLO" : "SOLO", font,
            headMask == selectedBit);
        foundation::drawButton(context, rect(1106.0,630.0,66.0,19.0),
            "LOAD", font);
        foundation::drawButton(context, rect(1178.0,630.0,66.0,19.0),
            "CLEAR", font);
        foundation::drawButton(context, rect(1250.0,630.0,76.0,19.0),
            asset(selectedSlot) ? "REVERSE" : "REVERSE —", font, reverse);
        for (uint32_t head = 0u; head < 8u; ++head) {
            char label[8] {};
            std::snprintf(label, sizeof(label), "H%u", head + 1u);
            foundation::drawButton(context,
                rect(860.0 + head * 58.0, 484.0, 52.0, 21.0), label,
                font, head == selectedHead
                    && (headMask & (1u << head)) != 0u);
        }
        const char* labels[] {"RING READ", "PHASE", "RATE", "SOURCE",
            "XFADE", "GROUP"};
        std::array<std::string, 6u> values {{
            "NO SOURCE", "—", "—", "NO SOURCE", "NO SOURCE", "FREE H1",
        }};
        SampleFamilyRingsHeadState head {};
        const bool haveHeadState = config.callbacks.getRingsHeadState
            && config.callbacks.getRingsHeadState(config.callbacks.context,
                selectedHead, &head);
        const bool radialManual = radialPath == 7u;
        const bool angularManual = static_cast<uint32_t>(std::lround(
            paramNamed("Head Relationship"))) == 7u;
        if (haveHeadState && head.hasSource) {
            const bool splitRead = (head.ringA != head.ringB
                || head.sourceSlotA != head.sourceSlotB
                || head.sourceChannelA != head.sourceChannelB)
                && head.mix > 0.015f && head.mix < 0.985f;
            char value[64] {};
            if (splitRead)
                std::snprintf(value, sizeof(value), "R%u > R%u",
                    head.ringA + 1u, head.ringB + 1u);
            else
                std::snprintf(value, sizeof(value), "R%u",
                    (head.mix >= 0.5f ? head.ringB : head.ringA) + 1u);
            values[0] = value;
            double degrees = std::clamp(static_cast<double>(head.phase),
                0.0, 1.0) * 360.0;
            if (degrees > 180.0) degrees -= 360.0;
            std::snprintf(value, sizeof(value), "%+.1f DEG", degrees);
            values[1] = value;
            std::snprintf(value, sizeof(value), "%+.3fX",
                static_cast<double>(head.rate));
            values[2] = value;
            if (splitRead)
                std::snprintf(value, sizeof(value), "%c%u > %c%u",
                    static_cast<int>('A' + head.sourceSlotA),
                    head.sourceChannelA + 1u,
                    static_cast<int>('A' + head.sourceSlotB),
                    head.sourceChannelB + 1u);
            else
                std::snprintf(value, sizeof(value), "%c%u",
                    static_cast<int>('A' + (head.mix >= 0.5f
                        ? head.sourceSlotB : head.sourceSlotA)),
                    (head.mix >= 0.5f
                        ? head.sourceChannelB : head.sourceChannelA) + 1u);
            values[3] = value;
            const double mix = std::clamp(static_cast<double>(head.mix),
                0.0, 1.0);
            if (splitRead) {
                std::snprintf(value, sizeof(value), "%.0f / %.0f%%",
                    (1.0 - mix) * 100.0, mix * 100.0);
                values[4] = value;
            } else {
                values[4] = "HARD READ";
            }
        }
        const uint32_t formation = static_cast<uint32_t>(std::lround(
            paramNamed("Head Formation")));
        const uint32_t groupSize = haveHeadState ? head.groupSize
            : formation == 1u ? 2u
            : formation == 2u ? 4u : formation == 3u ? 8u : 1u;
        const uint32_t leader = haveHeadState ? head.groupLeader
            : selectedHead - selectedHead % groupSize;
        char group[32] {};
        if (groupSize == 1u)
            std::snprintf(group, sizeof(group), "FREE H%u", selectedHead + 1u);
        else
            std::snprintf(group, sizeof(group), "H%u–H%u", leader + 1u,
                leader + groupSize);
        values[5] = group;
        for (uint32_t index = 0u; index < 6u; ++index) {
            if ((index == 0u && radialManual)
                || ((index == 1u || index == 2u) && angularManual))
                continue;
            const double width = 239.0;
            const uint32_t column = index % 2u;
            const uint32_t row = index / 2u;
            const CRect cell = rect(850.0 + column * 249.0,
                515.0 + row * 27.0, width, 24.0);
            text(context, labels[index], cell.left + 16.0, cell.top,
                88.0, style.label);
            foundation::drawMenuBox(context,
                rect(cell.left + 108.0, cell.top - 1.0,
                    cell.getWidth() - 124.0, 15.0), values[index], font);
        }
        text(context, sampleDetail(selectedSlot), 860.0, 798.0, 466.0,
            style.value, kLeftText, tinyFont);
    }

    void buildCirculatorLayout(ParameterLayout& layout) const
    {
        const CRect output = rect(18.0, 196.0, 352.0, 132.0);
        const CRect capture = rect(18.0, 340.0, 352.0, 184.0);
        const CRect loops = rect(388.0, 196.0, 354.0, 314.0);
        const CRect crossfade = rect(388.0, 522.0, 354.0, 106.0);
        addPanel(layout, output, "OUTPUT");
        addPanel(layout, capture, "CAPTURE");
        addPanel(layout, loops, "LOOPS");
        addPanel(layout, crossfade, "CROSSFADE");
        const auto cell = [](const CRect& panel, uint32_t row) {
            return rect(panel.left, panel.top + 36.0 + row * 26.0,
                panel.getWidth(), 24.0);
        };
        const struct Row { const char* parameter; const char* label;
            CRect panel; uint32_t row; bool menu; } rows[] {
            {"Playing","PLAY [C#2]",output,0u,true},
            {"Input Gain","IN",output,1u,false},
            {"Blend","BLEND",output,2u,false},
            {"Output Gain","OUT",output,3u,false},
            {"Record","REC [C2 HOLD]",capture,0u,true},
            {"Record Target","TARGET",capture,1u,true},
            {"Record Mode","MODE",capture,2u,true},
            {"Record Monitor","MONITOR",capture,3u,true},
            {"Overdub Feedback","FEEDBACK",capture,4u,false},
            {"Loop Model","MODEL",loops,0u,true},
            {"Loop 1 Rate","A RATE",loops,1u,false},
            {"Loop 1 Reverse","A REVERSE [D2]",loops,2u,true},
            {"Loop 1 Start","A START",loops,3u,false},
            {"Loop 1 End","A END",loops,4u,false},
            {"Loop 1 Join","A JOIN",loops,5u,true},
            {"Loop 2 Rate","B RATE",loops,6u,false},
            {"Loop 2 Reverse","B REVERSE [D#2]",loops,7u,true},
            {"Loop 2 Start","B START",loops,8u,false},
            {"Loop 2 End","B END",loops,9u,false},
            {"Loop 2 Join","B JOIN",loops,10u,true},
            {"Crossfade Motion","MOTION",crossfade,0u,true},
            {"Fade Shape","SHAPE",crossfade,1u,true},
            {"Position / Rate","POSITION",crossfade,2u,false},
        };
        for (const auto& row : rows)
            addProcessorRow(layout, row.parameter, row.label,
                cell(row.panel, row.row), row.menu);
    }

    void drawCirculatorSpecials(CDrawContext& context)
    {
        text(context, "ERASE", 34.0, 504.0, 84.0, style.label);
        foundation::drawButton(context, rect(126.0,505.0,111.0,15.0),
            "CLEAR A", font);
        foundation::drawButton(context, rect(243.0,505.0,111.0,15.0),
            "CLEAR B", font);
        text(context, "C2: HOLD TO CAPTURE", 34.0, 536.0, 320.0,
            style.dim, kLeftText, tinyFont);
    }

    ParameterLayout parameterLayout() const
    {
        ParameterLayout result;
        if (parameters.empty()) return result;
        switch (config.visualization) {
        case SampleFamilyVisualization::Doubles:
            buildDoublesLayout(result);
            return result;
        case SampleFamilyVisualization::Wavesets:
            buildWavesetsLayout(result);
            return result;
        case SampleFamilyVisualization::Motion:
            buildMotionLayout(result);
            return result;
        case SampleFamilyVisualization::Lanes:
        case SampleFamilyVisualization::Grains:
        case SampleFamilyVisualization::Cutups:
            buildLaneFamilyLayout(result);
            return result;
        case SampleFamilyVisualization::Rings:
            buildRingsLayout(result);
            return result;
        case SampleFamilyVisualization::Circulator:
            buildCirculatorLayout(result);
            return result;
        default:
            break;
        }
        const double top = kPanelTop + samplePanelHeight() + kPanelGap;
        const double bottom = config.nativeHeight - kOuterInset;
        const double availableHeight = std::max(80.0, bottom - top);
        const uint32_t rowCapacity = std::max(1u,
            static_cast<uint32_t>(std::floor(
                (availableHeight - 28.0) / kParameterRowHeight)));
        uint32_t columns = static_cast<uint32_t>((parameters.size()
            + rowCapacity - 1u) / rowCapacity);
        columns = std::clamp(std::max(columns, config.minimumColumns),
            1u, 6u);
        const double usableWidth = config.nativeWidth - 2.0 * kOuterInset
            - (columns - 1u) * kPanelGap;
        const double columnWidth = usableWidth / columns;

        std::size_t parameterIndex = 0u;
        for (uint32_t column = 0u;
             column < columns && parameterIndex < parameters.size();
             ++column) {
            const std::size_t remaining = parameters.size() - parameterIndex;
            const uint32_t remainingColumns = columns - column;
            const uint32_t rows = std::min<uint32_t>(rowCapacity,
                static_cast<uint32_t>((remaining + remainingColumns - 1u)
                    / remainingColumns));
            const double x = kOuterInset
                + column * (columnWidth + kPanelGap);
            const std::string module = parameters[parameterIndex].module[0]
                ? parameters[parameterIndex].module : "PARAMETERS";
            const double panelHeight = 27.0
                + rows * kParameterRowHeight;
            result.panels.push_back({ rect(x, top, columnWidth,
                std::min(panelHeight, availableHeight)),
                uppercaseAscii(module) });
            for (uint32_t row = 0u;
                 row < rows && parameterIndex < parameters.size(); ++row) {
                const double y = top + 23.0 + row * kParameterRowHeight;
                const CRect rowRect = rect(x + 7.0, y,
                    columnWidth - 14.0, kParameterRowHeight);
                const double controlX = x + columnWidth * 0.45;
                const CRect control = rect(controlX, y + 2.0,
                    x + columnWidth - 8.0 - controlX, 16.0);
                const CRect track = rect(control.left, y + 6.0,
                    std::max(18.0, control.getWidth() - 62.0), 8.0);
                result.cells.push_back({ parameterIndex, rowRect,
                    control, track, uppercaseAscii(
                        parameters[parameterIndex].name),
                    parameters[parameterIndex].stepped
                        || parameters[parameterIndex].readOnly });
                ++parameterIndex;
            }
        }
        return result;
    }

    void drawParameterPanels(CDrawContext& context)
    {
        const ParameterLayout layout = parameterLayout();
        if (config.visualization == SampleFamilyVisualization::Rings) {
            const CRect output = rect(18.0, 732.0, 820.0, 60.0);
            context.setFillColor(style.cell);
            context.drawRect(output, kDrawFilled);
            context.setFrameColor(style.grid);
            context.drawRect(output, kDrawStroked);
        }
        for (const auto& panel : layout.panels)
            foundation::drawPanel(context, panel.bounds, panel.title,
                font);
        for (const auto& cell : layout.cells) {
            const auto& info = parameters[cell.parameterIndex];
            if (cell.label == "__CROSSFADER__") continue;
            const std::string inactive = controlInactiveReason(info);
            const double centerY = cell.menu ? cell.control.getCenter().y : cell.track.getCenter().y;
            const bool manual = config.visualization == SampleFamilyVisualization::Rings && cell.label.rfind("MANUAL ", 0u) == 0u;
            auto controlStyle = style;
            if (!inactive.empty()) {
                controlStyle.strip = color(0x111111);
                controlStyle.grid = color(0x303030);
                controlStyle.fill = color(0x292929);
                controlStyle.text = color(0x5d5d5d);
                controlStyle.label = color(0x626262);
                controlStyle.value = color(0x686868);
                if (config.visualization == SampleFamilyVisualization::Motion) {
                    controlStyle.strip = color(0x161616);
                    controlStyle.fill = color(0x363636);
                    controlStyle.text = color(0x555555);
                    controlStyle.label = color(0x606060);
                    controlStyle.value = color(0x555555);
                }
            } else if (manual) {
                controlStyle.grid = color(0x668078);
                controlStyle.fill = color(0x78958c);
                controlStyle.label = color(0xc3d2cd);
                controlStyle.value = color(0xb8cbc5);
            }
            std::string label = cell.label;
            if (config.visualization == SampleFamilyVisualization::Motion
                && std::strcmp(info.name, "Event Rate") == 0
                && paramNamed("Segment Model") == 3 && paramNamed("Segment Trigger") != 0)
                label = "DURATION BASIS";
            foundation::drawTextInRect(context, uppercaseAscii(label),
                rect(cell.row.left, centerY - 7.5, cell.control.left - cell.row.left - 5.0, 15.0),
                controlStyle.label, font, kLeftText);
            const std::string displayedValue = inactive.empty()
                    || config.visualization == SampleFamilyVisualization::Motion
                ? paramText(info) : inactive;
            if (cell.menu || info.readOnly) {
                foundation::drawMenuBox(context, cell.control,
                    displayedValue, font, controlStyle);
            } else {
                const double normalized = parameterToNormalized(info, param(info.id));
                constexpr double handleHeight = 14.0;
                foundation::drawHorizontalSlider(context, cell.track, normalized,
                    cell.track.getCenter().y - handleHeight * 0.5, handleHeight, controlStyle);
                const auto valueBounds = rect(cell.track.right + 4.0, centerY - 7.5,
                    cell.control.right - cell.track.right - 4.0, 15.0);
                foundation::drawTextInRect(context,
                    foundation::sliderValueTextToFit(context, displayedValue,
                        valueBounds.getWidth(), font), valueBounds,
                    controlStyle.value, font, kRightText);
            }
        }
        if (hasOutputRoutingControls()) {
            const char* label = outputRoutingPage
                ? config.visualization == SampleFamilyVisualization::Grains
                    ? "POLYPHONY / SCAN" : "VOICE / PITCH"
                : "ROUTING";
            foundation::drawButton(context, outputPageButtonRect(), label,
                font, outputRoutingPage);
        }
        if (config.visualization == SampleFamilyVisualization::Wavesets
            || config.visualization == SampleFamilyVisualization::Motion
            || isLaneFamily()) {
            for (uint32_t action = 0u; action < config.actionCount; ++action) {
                std::string label = config.actions[action].label
                    ? uppercaseAscii(config.actions[action].label) : "";
                if (label == "PREVIEW") label = "PLAY";
                foundation::drawButton(context, actionButtonRect(action),
                    label, font, heldAction == action + 1u);
            }
        }
        if (config.visualization == SampleFamilyVisualization::Doubles)
            drawDoublesSpecials(context);
        else if (config.visualization == SampleFamilyVisualization::Motion)
            drawMotionSpecials(context);
        else if (config.visualization == SampleFamilyVisualization::Rings)
            drawRingsSpecials(context);
        else if (config.visualization
            == SampleFamilyVisualization::Circulator)
            drawCirculatorSpecials(context);
    }

    void updateContinuousParameter(const SampleFamilyParameterInfo& info,
        const ParameterCell& cell, const CPoint& point)
    {
        const double normalized = std::clamp(
            (point.x - cell.track.left) / cell.track.getWidth(), 0.0, 1.0);
        const double value = parameterFromNormalized(info, normalized);
        parameterEdit.set(info.stepped ? std::round(value) : value);
        invalid();
    }

    void updateCirculatorMix(const CPoint& point)
    {
        const CRect graph = visualizationRect();
        parameterEdit.set(std::clamp((point.y - graph.top) / graph.getHeight(), 0.0, 1.0));
        markPresetEdited();
        invalid();
    }

    double parameterToNormalized(const SampleFamilyParameterInfo& info,
        double value) const
    {
        if (config.callbacks.parameterToNormalized)
            return std::clamp(config.callbacks.parameterToNormalized(
                config.callbacks.context, info.id, value), 0.0, 1.0);
        if (!(info.maximum > info.minimum)) return 0.0;
        return std::clamp((value - info.minimum)
            / (info.maximum - info.minimum), 0.0, 1.0);
    }

    double parameterFromNormalized(const SampleFamilyParameterInfo& info,
        double normalized) const
    {
        normalized = std::clamp(normalized, 0.0, 1.0);
        if (config.callbacks.parameterFromNormalized)
            return std::clamp(config.callbacks.parameterFromNormalized(
                config.callbacks.context, info.id, normalized),
                info.minimum, info.maximum);
        return info.minimum
            + normalized * (info.maximum - info.minimum);
    }

    void finishEdits()
    {
        if (ringDrag) { parameterEdit.end(); markerEdit.end(); }
        ringDrag = false;
        cueDragDeck = -1;
        if (dragParameter != 0u || circulatorMixDrag) parameterEdit.end();
        circulatorMixDrag = false;
        if (markerDragParameter != 0u) markerEdit.end();
        dragParameter = 0u;
        markerDragParameter = 0u;
        visualizationDragPoint = -1;
        visualizationAlternateDrag = false;
    }

    void releaseHeldAction()
    {
        if (heldAction == 0u || heldAction > config.actionCount) return;
        const auto& action = config.actions[heldAction - 1u];
        if (config.callbacks.performAction)
            config.callbacks.performAction(config.callbacks.context,
                action.actionId, false);
        heldAction = 0u;
        invalid();
    }

    void selectSample()
    {
        if (!config.callbacks.loadSample) return;
        foundation::FileDialogOptions options;
        options.title = "Load sample";
        const std::string path = foundation::runFileDialog(getFrame(), options);
        if (!path.empty())
            config.callbacks.loadSample(config.callbacks.context,
                selectedSlot, path.c_str());
    }

    void selectPreset(bool save)
    {
        if ((save && !config.callbacks.savePreset)
            || (!save && !config.callbacks.loadPreset)) return;
        foundation::FileDialogOptions options;
        options.save = save;
        options.title = save ? "Save s3g preset" : "Load s3g preset";
        options.extensionDescription = "s3g preset";
        options.extension = "s3gpreset";
        options.defaultSaveName = "Preset.s3gpreset";
        options.initialDirectory = foundation::presetDirectory(
            config.pluginName);
        const std::string path = foundation::runFileDialog(getFrame(), options);
        if (path.empty()) return;
        const bool succeeded = save
            ? config.callbacks.savePreset(config.callbacks.context,
                path.c_str())
            : config.callbacks.loadPreset(config.callbacks.context,
                path.c_str());
        if (!succeeded) return;
        const std::string stem = foundation::pathToUtf8(
            foundation::pathFromUtf8(path.c_str()).stem());
        presetName = stem.empty() ? "CUSTOM" : stem;
        invalid();
    }

    SampleFamilyEditorConfig config;
    const foundation::Palette& style = foundation::palette();
    SharedPointer<CFontDesc> font;
    SharedPointer<CFontDesc> titleFont;
    SharedPointer<CFontDesc> tinyFont;
    SharedPointer<CVSTGUITimer> refreshTimer;
    std::unique_ptr<SampleCursorPresenter> cursorPresenter;
    bool cursorPresentationActive = false;
    bool cursorPresenterUnavailable = false;
    std::vector<SampleFamilyParameterInfo> parameters;
    foundation::ParameterEditSession parameterEdit;
    foundation::ParameterEditSession markerEdit;
    std::string presetName { "INIT" };
    uint32_t selectedSlot = 0u;
    bool ringDrag = false;
    int32_t cueDragDeck = -1;
    int32_t soloHead = -1;
    uint32_t selectedHead = 0u;
    uint32_t soloRestoreMask = 255u;
    bool outputRoutingPage = false;
    uint32_t dragParameter = 0u;
    bool circulatorMixDrag = false;
    uint32_t markerDragParameter = 0u;
    uint32_t heldAction = 0u;
    int32_t visualizationDragPoint = -1;
    bool visualizationAlternateDrag = false;
    CanvasMenuKind canvasMenuKind = CanvasMenuKind::None;
    std::vector<CanvasMenuEntry> canvasMenuEntries;
    uint32_t canvasMenuParameterId = 0u;
    uint32_t canvasMenuColumns = 1u;
    uint32_t canvasMenuRows = 0u;
    double canvasMenuColumnWidth = 0.0;
    CRect canvasMenuBounds {};
    int32_t canvasMenuSelected = -1;
    int32_t canvasMenuHover = -1;
    int32_t selectedPreset = 0;
    double wavesetsWaveZoom = 1.0;
    double wavesetsViewStart = 0.0;
    double wavesetsScopeScale = 2.0;
    const sample::SampleAsset* displayedWaveformAsset = nullptr;
};

} // namespace

class SampleFamilyEditor final : public foundation::EditorHost {
public:
    SampleFamilyEditor(const SampleFamilyEditorConfig& editorConfig,
        uint32_t requestedWidth, uint32_t requestedHeight)
        : EditorHost(editorConfig.nativeWidth, editorConfig.nativeHeight,
            requestedWidth, requestedHeight)
        , config(editorConfig)
    {
    }

    bool build()
    {
        return ready() && attach(new SampleFamilyView(config));
    }

private:
    SampleFamilyEditorConfig config;
};

SampleFamilyEditor* createSampleFamilyEditor(
    const SampleFamilyEditorConfig& config, uint32_t width, uint32_t height)
{
    auto* editor = new (std::nothrow) SampleFamilyEditor(
        config, width, height);
    if (!editor) return nullptr;
    if (!editor->build()) {
        delete editor;
        return nullptr;
    }
    return editor;
}

void destroySampleFamilyEditor(SampleFamilyEditor* editor) { delete editor; }

bool setSampleFamilyEditorParent(
    SampleFamilyEditor* editor, void* nativeParent)
{
    return editor && editor->setParent(nativeParent);
}

bool setSampleFamilyEditorSize(
    SampleFamilyEditor* editor, uint32_t width, uint32_t height)
{
    return editor && editor->setSize(width, height);
}

bool setSampleFamilyEditorVisible(
    SampleFamilyEditor* editor, bool visible)
{
    return editor && editor->setVisible(visible);
}

} // namespace s3g::portable_gui
