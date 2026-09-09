#include "s3g_sample_player_vstgui.h"

#include "s3g_gui_layout.h"
#include "s3g_sample_storage.h"
#include "s3g_vstgui_foundation.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/dragging.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/idatapackage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>

namespace s3g::portable_gui {
namespace {

using namespace VSTGUI;

constexpr uint32_t kPlayModeParamId = 1u;
constexpr uint32_t kStartParamId = 2u;
constexpr uint32_t kLengthParamId = 3u;
constexpr uint32_t kLoopStartParamId = 4u;
constexpr uint32_t kLoopEndParamId = 5u;
constexpr uint32_t kTuneParamId = 6u;
constexpr uint32_t kFineTuneParamId = 7u;
constexpr uint32_t kRootNoteParamId = 8u;
constexpr uint32_t kAttackParamId = 9u;
constexpr uint32_t kDecayParamId = 10u;
constexpr uint32_t kSustainParamId = 11u;
constexpr uint32_t kReleaseParamId = 12u;
constexpr uint32_t kGainParamId = 13u;
constexpr uint32_t kPanParamId = 14u;
constexpr uint32_t kVelocityParamId = 15u;
constexpr uint32_t kLoopCrossfadeParamId = 16u;
constexpr uint32_t kFilterTypeParamId = 17u;
constexpr uint32_t kFilterCutoffParamId = 18u;
constexpr uint32_t kFilterResonanceParamId = 19u;
constexpr uint32_t kFilterEnvelopeParamId = 20u;
constexpr uint32_t kPitchModeParamId = 21u;
constexpr uint32_t kSyncModeParamId = 22u;
constexpr uint32_t kSourceTempoParamId = 23u;
constexpr uint32_t kTriggerModeParamId = 24u;
constexpr uint32_t kRetriggerModeParamId = 25u;
constexpr uint32_t kVoiceModeParamId = 26u;
constexpr uint32_t kGlideParamId = 27u;
constexpr uint32_t kMidiReceiveParamId = 28u;

constexpr uint64_t kMaximumEmbeddedAudioBytes =
    1024ull * 1024ull * 1024ull;

using foundation::color;

CRect rect(double x, double y, double width, double height)
{
    return foundation::rect(x, y, width, height);
}

CRect rect(const gui_layout::Rect& value)
{
    return rect(value.x, value.y, value.width, value.height);
}

bool contains(const CRect& bounds, const CPoint& point)
{
    return foundation::contains(bounds, point);
}

const CRect kSamplePanel = rect(18.0, 42.0, 944.0, 282.0);
const CRect kWaveform = rect(30.0, 70.0, 920.0, 210.0);
const CRect kPlaybackPanel = rect(18.0, 336.0, 456.0, 294.0);
const CRect kOutputPanel = rect(486.0, 336.0, 476.0, 128.0);
const CRect kFilterPanel = rect(486.0, 476.0, 476.0, 154.0);
const CRect kPitchPanel = rect(18.0, 642.0, 456.0, 184.0);
const CRect kEnvelopePanel = rect(486.0, 642.0, 476.0, 166.0);
const CRect kKillAllButton = rect(876.0, 339.0, 74.0, 15.0);
const CRect kSampleLoadButton = rect(700.0, 45.0, 88.0, 15.0);
const CRect kStorageButton = rect(796.0, 45.0, 154.0, 15.0);

struct SliderSpec {
    uint32_t id;
    const char* label;
    double panelX;
    double panelWidth;
    double y;
};

constexpr std::array<SliderSpec, 20u> kSliders {{
    { kStartParamId, "START", 18.0, 456.0, 476.0 },
    { kLengthParamId, "LENGTH", 18.0, 456.0, 502.0 },
    { kLoopStartParamId, "LOOP START", 18.0, 456.0, 528.0 },
    { kLoopEndParamId, "LOOP END", 18.0, 456.0, 554.0 },
    { kLoopCrossfadeParamId, "LOOP XFADE", 18.0, 456.0, 580.0 },
    { kSourceTempoParamId, "SAMPLE BPM", 18.0, 456.0, 606.0 },
    { kGainParamId, "OUT", 486.0, 476.0, 372.0 },
    { kPanParamId, "PAN", 486.0, 476.0, 398.0 },
    { kVelocityParamId, "VELOCITY", 486.0, 476.0, 450.0 },
    { kFilterCutoffParamId, "CUTOFF", 486.0, 476.0, 538.0 },
    { kFilterResonanceParamId, "RESONANCE", 486.0, 476.0, 564.0 },
    { kFilterEnvelopeParamId, "ENV AMOUNT", 486.0, 476.0, 590.0 },
    { kGlideParamId, "GLIDE", 18.0, 456.0, 730.0 },
    { kTuneParamId, "TUNE", 18.0, 456.0, 756.0 },
    { kFineTuneParamId, "FINE", 18.0, 456.0, 782.0 },
    { kRootNoteParamId, "ROOT NOTE", 18.0, 456.0, 808.0 },
    { kAttackParamId, "ATTACK", 486.0, 476.0, 678.0 },
    { kDecayParamId, "DECAY", 486.0, 476.0, 704.0 },
    { kSustainParamId, "SUSTAIN", 486.0, 476.0, 730.0 },
    { kReleaseParamId, "RELEASE", 486.0, 476.0, 756.0 },
}};

constexpr std::array<const char*, 6u> kPlayModeItems {{
    "FORWARD", "FORWARD LOOP", "REVERSE", "REVERSE LOOP",
    "FORWARD PING-PONG", "REVERSE PING-PONG",
}};
constexpr std::array<const char*, 4u> kTriggerModeItems {{
    "AUTO", "GATE", "ONE SHOT", "TOGGLE",
}};
constexpr std::array<const char*, 3u> kRetriggerModeItems {{
    "LAYER", "RESTART", "IGNORE",
}};
constexpr std::array<const char*, 2u> kSyncModeItems {{ "FREE", "HOST" }};
constexpr std::array<const char*, 5u> kFilterTypeItems {{
    "OFF", "LOW PASS", "BAND PASS", "HIGH PASS", "NOTCH",
}};
constexpr std::array<const char*, 3u> kPitchModeItems {{
    "RATE", "STRETCH", "RATE BELOW / STRETCH ABOVE",
}};
constexpr std::array<const char*, 3u> kVoiceModeItems {{
    "POLY", "MONO", "LEGATO",
}};
constexpr std::array<const char*, 17u> kMidiReceiveItems {{
    "OMNI", "CHANNEL 1", "CHANNEL 2", "CHANNEL 3", "CHANNEL 4",
    "CHANNEL 5", "CHANNEL 6", "CHANNEL 7", "CHANNEL 8", "CHANNEL 9",
    "CHANNEL 10", "CHANNEL 11", "CHANNEL 12", "CHANNEL 13",
    "CHANNEL 14", "CHANNEL 15", "CHANNEL 16",
}};

struct MenuSpec {
    uint32_t id;
    const char* label;
    double panelX;
    double panelWidth;
    double y;
    CRect dropdown;
    const char* const* items;
    uint32_t count;
};

const std::array<MenuSpec, 8u> kMenus {{
    { kPlayModeParamId, "PLAY MODE", 18.0, 456.0, 372.0,
        rect(125.0, 387.0, 331.0, 120.0), kPlayModeItems.data(), 6u },
    { kTriggerModeParamId, "TRIGGER", 18.0, 456.0, 398.0,
        rect(125.0, 413.0, 331.0, 80.0), kTriggerModeItems.data(), 4u },
    { kRetriggerModeParamId, "RETRIGGER", 18.0, 456.0, 424.0,
        rect(125.0, 439.0, 331.0, 60.0), kRetriggerModeItems.data(), 3u },
    { kSyncModeParamId, "TEMPO SYNC", 18.0, 456.0, 450.0,
        rect(125.0, 465.0, 331.0, 40.0), kSyncModeItems.data(), 2u },
    { kFilterTypeParamId, "FILTER TYPE", 486.0, 476.0, 512.0,
        rect(593.0, 527.0, 351.0, 100.0), kFilterTypeItems.data(), 5u },
    { kPitchModeParamId, "PITCH MODE", 18.0, 456.0, 677.0,
        rect(125.0, 692.0, 331.0, 60.0), kPitchModeItems.data(), 3u },
    { kVoiceModeParamId, "VOICE MODE", 18.0, 456.0, 703.0,
        rect(125.0, 718.0, 331.0, 60.0), kVoiceModeItems.data(), 3u },
    { kMidiReceiveParamId, "RECEIVE", 486.0, 476.0, 424.0,
        rect(593.0, 82.0, 351.0, 340.0), kMidiReceiveItems.data(), 17u },
}};

CRect menuRect(const MenuSpec& menu)
{
    return rect(gui_layout::processorControlX(menu.panelX), menu.y - 1.0,
        gui_layout::processorMenuWidth(menu.panelWidth), 15.0);
}

CRect sliderTrackRect(const SliderSpec& slider)
{
    return rect(gui_layout::processorControlX(slider.panelX), slider.y + 1.0,
        gui_layout::processorTrackWidth(slider.panelWidth), 9.0);
}

CRect sliderHitRect(const SliderSpec& slider)
{
    return rect(slider.panelX + gui_layout::kStandardMetrics.hitInset,
        slider.y - 8.0,
        slider.panelWidth - 2.0 * gui_layout::kStandardMetrics.hitInset,
        gui_layout::kStandardMetrics.hitHeight);
}

struct ParamRange {
    double minimum;
    double maximum;
};

ParamRange paramRange(uint32_t id)
{
    switch (id) {
    case kStartParamId:
    case kLengthParamId:
    case kLoopStartParamId:
    case kLoopEndParamId:
    case kVelocityParamId:
    case kFilterResonanceParamId:
    case kSustainParamId: return { 0.0, 1.0 };
    case kLoopCrossfadeParamId: return { 0.0, 0.5 };
    case kSourceTempoParamId: return { 20.0, 999.0 };
    case kGainParamId: return { -60.0, 12.0 };
    case kPanParamId:
    case kFilterEnvelopeParamId: return { -1.0, 1.0 };
    case kFilterCutoffParamId: return { 20.0, 20000.0 };
    case kGlideParamId: return { 0.0, 2000.0 };
    case kTuneParamId: return { -60.0, 60.0 };
    case kFineTuneParamId: return { -100.0, 100.0 };
    case kRootNoteParamId: return { 0.0, 127.0 };
    case kAttackParamId:
    case kDecayParamId:
    case kReleaseParamId: return { 0.0, 1.0 };
    default: return { 0.0, 1.0 };
    }
}

double sliderNormalizedValue(uint32_t id, double value)
{
    const auto range = paramRange(id);
    if (id == kFilterCutoffParamId) {
        return std::clamp(std::log(value / range.minimum)
            / std::log(range.maximum / range.minimum), 0.0, 1.0);
    }
    return std::clamp((value - range.minimum)
        / (range.maximum - range.minimum), 0.0, 1.0);
}

double sliderValueFromNormalized(uint32_t id, double normalized)
{
    const auto range = paramRange(id);
    normalized = std::clamp(normalized, 0.0, 1.0);
    if (id == kFilterCutoffParamId) {
        return range.minimum * std::pow(
            range.maximum / range.minimum, normalized);
    }
    return range.minimum + normalized * (range.maximum - range.minimum);
}

class SamplePlayerView final : public foundation::ContentView,
                               public IDropTarget {
public:
    explicit SamplePlayerView(const SamplePlayerEditorConfig& editorConfig)
        : ContentView(rect(0.0, 0.0, editorConfig.nativeWidth,
            editorConfig.nativeHeight))
        , config(editorConfig)
    {
        const auto& metrics = foundation::fontMetrics();
        font = foundation::makeUiFont(metrics.body);
        titleFont = foundation::makeUiFont(metrics.title);
        channelFont = foundation::makeUiFont(metrics.channel);
        tinyChannelFont = foundation::makeUiFont(metrics.tiny);
        const foundation::ParameterEditCallbacks callbacks {
            config.callbacks.context, config.callbacks.beginParamEdit,
            config.callbacks.setParam, config.callbacks.endParamEdit
        };
        parameterEdit.setCallbacks(callbacks);
        companionEdit.setCallbacks(callbacks);
    }

    void loadDocumentationSampleIfRequested()
    {
        const char* path = std::getenv("S3G_GUI_DOCUMENTATION_SAMPLE_PATH");
        if (!path || !path[0] || !config.callbacks.loadDocumentationSample)
            return;
        if (config.callbacks.loadDocumentationSample(
                config.callbacks.context)) {
            waveZoom = 1.0;
            waveViewStart = 0.0;
        }
    }

    void startRefresh() override
    {
        if (refreshTimer) return;
        refreshTimer = makeOwned<CVSTGUITimer>(
            [this](CVSTGUITimer*) {
                if (config.callbacks.service)
                    config.callbacks.service(config.callbacks.context);
                if (isVisible()) invalid();
            }, 33u);
    }

    void stopRefresh() override { refreshTimer = nullptr; }

    void draw(CDrawContext* context) override
    {
        if (!context) return;
        context->setDrawMode(kAntiAliasing);
        context->setFillColor(style.background);
        context->drawRect(getViewSize(), kDrawFilled);
        drawTitleBand(*context);
        drawPanel(*context, kSamplePanel, "SAMPLE");
        drawButton(*context, kSampleLoadButton, "LOAD SAMPLE", false);
        std::string storage = "STORE ";
        if (config.callbacks.getStorageModeName) {
            const char* mode = config.callbacks.getStorageModeName(
                config.callbacks.context);
            if (mode) storage += mode;
        }
        const bool storageActive = config.callbacks.getStorageMode
            && config.callbacks.getStorageMode(config.callbacks.context) != 1u;
        drawButton(*context, kStorageButton, storage, storageActive);
        const char* status = config.callbacks.getStatus
            ? config.callbacks.getStatus(config.callbacks.context) : "";
        text(*context, status ? status : "", 390.0, 44.0, 300.0,
            style.value, kRightText);

        drawWaveform(*context);
        drawPanel(*context, kPlaybackPanel, "PLAYBACK");
        drawPanel(*context, kOutputPanel, "OUTPUT / MIDI");
        drawPanel(*context, kFilterPanel, "FILTER");
        drawPanel(*context, kPitchPanel, "PITCH");
        drawPanel(*context, kEnvelopePanel, "AMP ENVELOPE");
        drawButton(*context, kKillAllButton, "KILL ALL", false);

        for (const auto& menu : kMenus) drawMenu(*context, menu);
        for (const auto& slider : kSliders) {
            if (isParamExposed(slider.id)) drawSlider(*context, slider);
        }
        const uint32_t outputChannels = config.callbacks.getOutputChannelCount
            ? config.callbacks.getOutputChannelCount(config.callbacks.context)
            : 2u;
        if (outputChannels == 16u) {
            text(*context,
                "PAN DISABLED / SOURCE CHANNEL RELATIONSHIPS PRESERVED",
                536.0, 400.0, 400.0, style.label);
        }
        if (activeMenu >= 0)
            drawDropdown(*context, kMenus[static_cast<std::size_t>(activeMenu)]);
        setDirty(false);
    }

    void onMouseDownEvent(MouseDownEvent& event) override
    {
        if (!event.buttonState.isLeft()) return;
        const CPoint point = event.mousePosition;
        if (activeMenu >= 0) {
            const auto& menu = kMenus[static_cast<std::size_t>(activeMenu)];
            const int selected = dropdownHitIndex(menu, point);
            closeMenu();
            if (selected >= 0 && config.callbacks.setParam) {
                parameterEdit.perform(menu.id, static_cast<double>(selected));
                invalid();
                event.consumed = true;
                return;
            }
        }

        const auto titleBand = gui_layout::encoderTitleBand({
            static_cast<double>(config.nativeWidth),
            static_cast<double>(config.nativeHeight),
        });
        if (contains(rect(titleBand.presetMenu), point)) {
            if (config.callbacks.resetToDefaults)
                config.callbacks.resetToDefaults(config.callbacks.context);
            presetName = "INIT";
            waveZoom = 1.0;
            waveViewStart = 0.0;
            invalid();
            event.consumed = true;
            return;
        }
        if (contains(rect(titleBand.loadButton), point)) {
            selectPreset(false);
            event.consumed = true;
            return;
        }
        if (contains(rect(titleBand.saveButton), point)) {
            selectPreset(true);
            event.consumed = true;
            return;
        }
        if (contains(kKillAllButton, point)) {
            if (config.callbacks.killAll)
                config.callbacks.killAll(config.callbacks.context);
            invalid();
            event.consumed = true;
            return;
        }
        if (contains(kSampleLoadButton, point)) {
            selectSample();
            event.consumed = true;
            return;
        }
        if (contains(kStorageButton, point)) {
            if (config.callbacks.cycleStorageMode)
                config.callbacks.cycleStorageMode(config.callbacks.context);
            invalid();
            event.consumed = true;
            return;
        }
        for (std::size_t i = 0u; i < kMenus.size(); ++i) {
            if (contains(menuRect(kMenus[i]), point)) {
                activeMenu = static_cast<int>(i);
                menuHover = -1;
                invalid();
                event.consumed = true;
                return;
            }
        }

        if (contains(kWaveform, point) && asset()) {
            waveDragParam = waveBoundaryAtPoint(point);
            if (waveDragParam != 0u) {
                parameterEdit.begin(waveDragParam);
                if (waveDragParam == kStartParamId)
                    companionEdit.begin(kLengthParamId);
                waveFixedStart = param(kStartParamId);
                waveFixedEnd = std::min(1.0,
                    waveFixedStart + param(kLengthParamId));
                waveFixedLoopStart = std::clamp(param(kLoopStartParamId),
                    waveFixedStart, waveFixedEnd);
                waveFixedLoopEnd = std::clamp(param(kLoopEndParamId),
                    waveFixedLoopStart, waveFixedEnd);
                updateWaveBoundary(point);
                event.consumed = true;
                return;
            }
            if (event.clickCount >= 2u) {
                waveZoom = 1.0;
                waveViewStart = 0.0;
                invalid();
                event.consumed = true;
                return;
            }
        }

        for (const auto& slider : kSliders) {
            if (!isParamExposed(slider.id)
                || !contains(sliderHitRect(slider), point)) continue;
            if (event.clickCount >= 2u && config.callbacks.getDefaultValue
                && config.callbacks.setParam) {
                double value = 0.0;
                if (config.callbacks.getDefaultValue(
                        config.callbacks.context, slider.id, &value)) {
                    parameterEdit.perform(slider.id, value);
                }
            } else {
                dragParam = slider.id;
                parameterEdit.begin(slider.id);
                updateSlider(slider, point);
            }
            invalid();
            event.consumed = true;
            return;
        }
    }

    void onMouseMoveEvent(MouseMoveEvent& event) override
    {
        if (waveDragParam != 0u
            && event.buttonState.has(MouseButton::Left)) {
            updateWaveBoundary(event.mousePosition);
            event.consumed = true;
            return;
        }
        if (dragParam != 0u && event.buttonState.has(MouseButton::Left)) {
            if (const auto* slider = sliderForId(dragParam))
                updateSlider(*slider, event.mousePosition);
            event.consumed = true;
            return;
        }
        if (activeMenu >= 0) {
            const int nextHover = dropdownHitIndex(
                kMenus[static_cast<std::size_t>(activeMenu)],
                event.mousePosition);
            if (nextHover != menuHover) {
                menuHover = nextHover;
                invalid();
            }
        }
    }

    void onMouseUpEvent(MouseUpEvent& event) override
    {
        if (dragParam == 0u && waveDragParam == 0u) return;
        parameterEdit.end();
        companionEdit.end();
        dragParam = 0u;
        waveDragParam = 0u;
        event.consumed = true;
    }

    void onMouseCancelEvent(MouseCancelEvent& event) override
    {
        if (dragParam == 0u && waveDragParam == 0u) return;
        parameterEdit.end();
        companionEdit.end();
        dragParam = 0u;
        waveDragParam = 0u;
        event.consumed = true;
    }

    void onMouseExitEvent(MouseExitEvent&) override
    {
        if (menuHover >= 0) {
            menuHover = -1;
            invalid();
        }
    }

    void onMouseWheelEvent(MouseWheelEvent& event) override
    {
        if (!contains(kWaveform, event.mousePosition) || !asset()) return;
        const double oldSpan = waveVisibleSpan();
        const bool pan = event.modifiers.has(ModifierKey::Shift)
            || std::abs(event.deltaX) > std::abs(event.deltaY);
        if (pan && waveZoom > 1.0) {
            const double delta = std::abs(event.deltaX) > std::abs(event.deltaY)
                ? event.deltaX : event.deltaY;
            waveViewStart = std::clamp(waveViewStart
                + delta * oldSpan * 0.0125, 0.0, 1.0 - oldSpan);
        } else {
            const double anchor = std::clamp(
                (event.mousePosition.x - kWaveform.left)
                    / kWaveform.getWidth(), 0.0, 1.0);
            const double sourceAnchor = waveViewStart + anchor * oldSpan;
            waveZoom = std::clamp(waveZoom
                * std::exp(event.deltaY * 0.08), 1.0, 128.0);
            const double newSpan = waveVisibleSpan();
            waveViewStart = std::clamp(sourceAnchor - anchor * newSpan,
                0.0, 1.0 - newSpan);
        }
        invalid();
        event.consumed = true;
    }

    SharedPointer<IDropTarget> getDropTarget() override { return this; }

    DragOperation onDragEnter(DragEventData data) override
    {
        const auto droppedPath = firstDroppedFile(data.drag);
        return droppedPath.empty()
            ? DragOperation::None : DragOperation::Copy;
    }

    DragOperation onDragMove(DragEventData data) override
    {
        return onDragEnter(data);
    }

    void onDragLeave(DragEventData) override {}

    bool onDrop(DragEventData data) override
    {
        const std::string path = firstDroppedFile(data.drag);
        return !path.empty() && config.callbacks.loadSample
            && config.callbacks.loadSample(config.callbacks.context,
                path.c_str());
    }

private:
    const sample::SampleAsset* asset() const
    {
        return config.callbacks.getAsset
            ? config.callbacks.getAsset(config.callbacks.context) : nullptr;
    }

    double param(uint32_t id) const
    {
        return config.callbacks.getParam
            ? config.callbacks.getParam(config.callbacks.context, id) : 0.0;
    }

    bool isParamExposed(uint32_t id) const
    {
        return !config.callbacks.isParamExposed
            || config.callbacks.isParamExposed(config.callbacks.context, id);
    }

    std::string paramText(uint32_t id, double value) const
    {
        char buffer[64] {};
        if (config.callbacks.getParamText
            && config.callbacks.getParamText(config.callbacks.context, id,
                value, buffer, static_cast<uint32_t>(sizeof(buffer)))) {
            return buffer;
        }
        std::snprintf(buffer, sizeof(buffer), "%.3g", value);
        return buffer;
    }

    void text(CDrawContext& context, const std::string& value,
              double x, double y, double width, CColor textColor,
              CHoriTxtAlign align = kLeftText, bool title = false,
              CFontRef overrideFont = nullptr)
    {
        CFontRef selectedFont = overrideFont;
        if (!selectedFont)
            selectedFont = title ? titleFont.get () : font.get ();
        foundation::drawTextLine(context, value, x, y, width, textColor,
            selectedFont, align);
    }

    void textInRect(CDrawContext& context, const std::string& value,
                    const CRect& bounds, CColor textColor,
                    CHoriTxtAlign align = kCenterText)
    {
        foundation::drawTextInRect(context, value, bounds,
            textColor, font, align);
    }

    void drawTitleBand(CDrawContext& context)
    {
        const auto band = gui_layout::encoderTitleBand({
            static_cast<double>(config.nativeWidth),
            static_cast<double>(config.nativeHeight),
        });
        foundation::drawPluginTitle(context, config.pluginName ? config.pluginName : "",
            rect(band.titleX, band.titleY - 2.0,
                band.presetLabelX - band.titleX - 8.0, 15.0), titleFont);
        text(context, "PRESET", band.presetLabelX, band.controlY + 1.0,
            band.presetMenu.x - band.presetLabelX - 4.0, style.label);
        const auto presetBounds = rect(band.presetMenu);
        foundation::drawMenuBox(context, presetBounds, presetName, font);
        drawButton(context, rect(band.loadButton), "LOAD", false);
        drawButton(context, rect(band.saveButton), "SAVE", false);

        const float peak = config.callbacks.getOutputPeak
            ? config.callbacks.getOutputPeak(config.callbacks.context) : 0.0f;
        char status[32] {};
        std::snprintf(status, sizeof(status), "PK %+.1f",
            20.0 * std::log10(std::max(0.000001f, peak)));
        context.setFont(font);
        const double width = context.getStringWidth(status);
        text(context, status, config.nativeWidth - width
            - band.statusRightInset, band.titleY, width + 1.0, style.value);
    }

    void drawPanel(CDrawContext& context, const CRect& bounds,
                   const char* title)
    {
        foundation::drawPanel(context, bounds, title, font,
            gui_layout::kStandardMetrics.headerHeight,
            gui_layout::kStandardMetrics.headerLabelInset);
    }

    void drawButton(CDrawContext& context, const CRect& bounds,
                    const std::string& label, bool active)
    {
        foundation::drawButton(context, bounds, label, font, active);
    }

    void drawMenu(CDrawContext& context, const MenuSpec& menu)
    {
        const double labelX = gui_layout::processorLabelX(menu.panelX);
        const auto bounds = menuRect(menu);
        textInRect(context, menu.label,
            rect(labelX, bounds.getCenter().y - 7.5, bounds.left - labelX - 4.0, 15.0),
            style.label, kLeftText);
        const int selected = std::clamp(static_cast<int>(std::lround(
            param(menu.id))), 0, static_cast<int>(menu.count) - 1);
        foundation::drawMenuBox(context, bounds, menu.items[selected], font);
    }

    void drawSlider(CDrawContext& context, const SliderSpec& slider)
    {
        const double value = param(slider.id);
        const double norm = sliderNormalizedValue(slider.id, value);
        const double labelX = gui_layout::processorLabelX(slider.panelX);
        const double valueX = gui_layout::processorValueX(
            slider.panelX, slider.panelWidth);
        const auto track = sliderTrackRect(slider);
        textInRect(context, slider.label,
            rect(labelX, track.getCenter().y - 7.5, track.left - labelX - 4.0, 15.0),
            style.label, kLeftText);
        foundation::drawHorizontalSlider(
            context, track, norm, track.getCenter().y - 6.5, 13.0);
        textInRect(context, foundation::sliderValueTextToFit(context,
                paramText(slider.id, value),
                gui_layout::kStandardMetrics.processorValueWidth, font),
            rect(valueX, track.getCenter().y - 7.5,
                gui_layout::kStandardMetrics.processorValueWidth, 15.0), style.value, kRightText);
    }

    void drawDropdown(CDrawContext& context, const MenuSpec& menu)
    {
        const auto outer = rect(menu.dropdown.left - 2.0,
            menu.dropdown.top - 2.0, menu.dropdown.getWidth() + 4.0,
            menu.dropdown.getHeight() + 4.0);
        context.setFillColor(color(0x090909));
        context.drawRect(outer, kDrawFilled);
        context.setFillColor(color(0x1b1b1b));
        context.drawRect(menu.dropdown, kDrawFilled);
        context.setFrameColor(color(0x7e7e7e));
        context.drawRect(menu.dropdown, kDrawStroked);
        const int selected = std::clamp(static_cast<int>(std::lround(
            param(menu.id))), 0, static_cast<int>(menu.count) - 1);
        constexpr double rowHeight = 20.0;
        for (uint32_t i = 0u; i < menu.count; ++i) {
            const auto row = rect(menu.dropdown.left,
                menu.dropdown.top + rowHeight * i,
                menu.dropdown.getWidth(), rowHeight);
            if (static_cast<int>(i) == menuHover) {
                context.setFillColor(color(0x444444));
                context.drawRect(rect(row.left + 1.0, row.top + 1.0,
                    row.getWidth() - 2.0, row.getHeight() - 2.0),
                    kDrawFilled);
            } else if (static_cast<int>(i) == selected) {
                context.setFillColor(color(0x373737));
                context.drawRect(rect(row.left + 1.0, row.top + 1.0,
                    row.getWidth() - 2.0, row.getHeight() - 2.0),
                    kDrawFilled);
            } else if ((i % 2u) == 1u) {
                context.setFillColor(style.strip);
                context.drawRect(rect(row.left + 1.0, row.top + 1.0,
                    row.getWidth() - 2.0, row.getHeight() - 2.0),
                    kDrawFilled);
            }
            if (static_cast<int>(i) == selected
                || static_cast<int>(i) == menuHover) {
                context.setFillColor(style.fill);
                context.drawRect(rect(row.left + 2.0, row.top + 2.0,
                    3.0, row.getHeight() - 4.0), kDrawFilled);
            }
            if (i > 0u) {
                context.setFrameColor(color(0x4b4b4b));
                context.drawLine(CPoint(row.left, row.top),
                    CPoint(row.right, row.top));
            }
            text(context, menu.items[i], row.left + 9.0,
                row.top + 4.0, row.getWidth() - 18.0, style.value);
        }
    }

    void drawWaveform(CDrawContext& context)
    {
        context.setFillColor(style.strip);
        context.drawRect(kWaveform, kDrawFilled);
        context.setFrameColor(style.grid);
        context.setLineWidth(1.0);
        context.drawRect(kWaveform, kDrawStroked);
        const auto* currentAsset = asset();
        if (!currentAsset || !currentAsset->valid()) {
            text(context, "DROP AUDIO HERE", 420.0, 142.0, 180.0,
                style.value);
            return;
        }
        const std::size_t width = static_cast<std::size_t>(
            kWaveform.getWidth());
        const double visibleSpan = waveVisibleSpan();
        const double visibleEnd = waveViewStart + visibleSpan;
        const double usableHeight = kWaveform.getHeight() - 4.0;
        const double laneHeight = usableHeight
            / static_cast<double>(currentAsset->channelCount);
        CFontRef laneFont = currentAsset->channelCount > 8u
            ? tinyChannelFont : channelFont;
        for (uint32_t channel = 0u;
             channel < currentAsset->channelCount; ++channel) {
            const auto& samples = currentAsset->channels[channel];
            const double laneY = kWaveform.top + 2.0
                + laneHeight * channel;
            const double center = laneY + laneHeight * 0.5;
            auto trace = owned(context.createGraphicsPath());
            if (trace) {
                for (std::size_t pixel = 0u; pixel < width; ++pixel) {
                    const double firstPosition = waveViewStart
                        + static_cast<double>(pixel) / width * visibleSpan;
                    const double lastPosition = waveViewStart
                        + static_cast<double>(pixel + 1u) / width * visibleSpan;
                    const std::size_t first = std::min(samples.size() - 1u,
                        static_cast<std::size_t>(std::floor(firstPosition
                            * samples.size())));
                    const std::size_t last = std::max(first + 1u,
                        std::min(samples.size(), static_cast<std::size_t>(
                            std::ceil(lastPosition * samples.size()))));
                    const std::size_t boundedLast = std::min(last,
                        samples.size());
                    const std::size_t stride = std::max<std::size_t>(1u,
                        (boundedLast - first) / 32u);
                    float minimum = 1.0f;
                    float maximum = -1.0f;
                    for (std::size_t frame = first; frame < boundedLast;
                         frame += stride) {
                        minimum = std::min(minimum, samples[frame]);
                        maximum = std::max(maximum, samples[frame]);
                    }
                    if (boundedLast > first) {
                        const float finalSample = samples[boundedLast - 1u];
                        minimum = std::min(minimum, finalSample);
                        maximum = std::max(maximum, finalSample);
                    }
                    const double x = kWaveform.left + pixel;
                    trace->beginSubpath(x, center - maximum
                        * laneHeight * 0.44);
                    trace->addLine(x, center - minimum
                        * laneHeight * 0.44);
                }
                context.setFrameColor(color(channel % 2u == 0u
                    ? 0x878f8b : 0x7b837f));
                context.setLineWidth(1.0);
                context.drawGraphicsPath(trace, CDrawContext::kPathStroked);
            }
            if (channel != 0u) {
                context.setFillColor(color(0x424242));
                context.drawRect(rect(kWaveform.left + 1.0, laneY,
                    kWaveform.getWidth() - 2.0, 1.0), kDrawFilled);
            }
            char label[8] {};
            std::snprintf(label, sizeof(label), "%02u", channel + 1u);
            text(context, label, kWaveform.left + 5.0,
                laneY + std::max(0.0, (laneHeight - 10.0) * 0.5),
                24.0, color(0x7b7b7b), kLeftText, false, laneFont);
        }

        const double start = param(kStartParamId);
        const double end = std::min(1.0, start + param(kLengthParamId));
        const double loopStart = std::clamp(param(kLoopStartParamId),
            start, end);
        const double loopEnd = std::clamp(param(kLoopEndParamId),
            loopStart, end);
        struct Marker {
            double position;
            CColor markerColor;
            const char* label;
            bool upper;
        };
        const std::array<Marker, 4u> markers {{
            { start, style.accent, "S", true },
            { end, style.accent, "E", true },
            { loopStart, style.dim, "LS", false },
            { loopEnd, style.dim, "LE", false },
        }};
        for (const auto& marker : markers) {
            if (marker.position < waveViewStart
                || marker.position > visibleEnd) continue;
            const double x = waveXForNormalized(marker.position);
            context.setFrameColor(marker.markerColor);
            context.setLineWidth(1.5);
            context.drawLine(CPoint(x, kWaveform.top),
                CPoint(x, kWaveform.bottom));
            const double labelY = marker.upper
                ? kWaveform.top + 3.0 : kWaveform.bottom - 15.0;
            text(context, marker.label,
                std::clamp(x + 3.0, kWaveform.left + 3.0,
                    kWaveform.right - 20.0),
                labelY, 24.0, style.label);
        }

        std::array<float, sample::kMaximumVoices> positions {};
        std::array<uint8_t, sample::kMaximumVoices> keys {};
        const uint32_t cursorCount = config.callbacks.getVoiceCursors
            ? std::min<uint32_t>(config.callbacks.getVoiceCursors(
                config.callbacks.context, positions.data(), keys.data(),
                static_cast<uint32_t>(positions.size())),
                static_cast<uint32_t>(positions.size())) : 0u;
        for (uint32_t cursor = 0u; cursor < cursorCount; ++cursor) {
            if (positions[cursor] < waveViewStart
                || positions[cursor] > visibleEnd) continue;
            const CColor cursorColor = cursor % 3u == 0u ? style.fill
                : (cursor % 3u == 1u ? style.accent : style.dim);
            const double x = waveXForNormalized(positions[cursor]);
            context.setFrameColor(cursorColor);
            context.setLineWidth(1.25);
            context.drawLine(CPoint(x, kWaveform.top),
                CPoint(x, kWaveform.bottom));
            char cursorLabel[16] {};
            std::snprintf(cursorLabel, sizeof(cursorLabel), "N%u",
                static_cast<unsigned>(keys[cursor]));
            context.setFont(font);
            const double labelWidth = context.getStringWidth(cursorLabel);
            double labelX = x + 3.0;
            if (labelX + labelWidth + 6.0 > kWaveform.right)
                labelX = x - labelWidth - 7.0;
            const double labelY = kWaveform.top + 18.0
                + static_cast<double>(cursor % 12u) * 15.0;
            context.setFillColor(style.background);
            context.drawRect(rect(labelX - 2.0, labelY - 1.0,
                labelWidth + 5.0, 14.0), kDrawFilled);
            text(context, cursorLabel, labelX, labelY,
                labelWidth + 1.0, style.label);
        }

        const uint64_t decodedBytes = static_cast<uint64_t>(
            currentAsset->channelCount) * currentAsset->frameCount()
            * sizeof(float);
        const char* samplePath = config.callbacks.getSamplePath
            ? config.callbacks.getSamplePath(config.callbacks.context) : "";
        const std::string path = samplePath ? samplePath : "";
        const uint8_t storageMode = config.callbacks.getStorageMode
            ? config.callbacks.getStorageMode(config.callbacks.context) : 0u;
        const bool safetyPayload = path.empty();
        const bool embeddedPayload = storageMode == 2u
            && decodedBytes <= kMaximumEmbeddedAudioBytes;
        const std::string storageDetail = safetyPayload
            ? "PCM SAFETY " + sample_storage::formatByteCount(decodedBytes)
            : (embeddedPayload
                ? "PCM STATE " + sample_storage::formatByteCount(decodedBytes)
                : sample_storage::abbreviatedPath(path, 42u));
        char details[256] {};
        std::snprintf(details, sizeof(details),
            "%u VOICES  /  %u CH  /  %u FRAMES  /  %.0f HZ  /  %s",
            cursorCount, static_cast<unsigned>(currentAsset->channelCount),
            currentAsset->frameCount(), currentAsset->sampleRate,
            storageDetail.c_str());
        text(context, details, 28.0, 286.0, 920.0, style.value);
        char help[192] {};
        std::snprintf(help, sizeof(help),
            "DRAG S / E / LS / LE   SCROLL ZOOM   SHIFT-SCROLL PAN   DOUBLE-CLICK FIT   ZOOM %.1fX",
            waveZoom);
        text(context, help, 28.0, 304.0, 920.0, style.label);
    }

    const SliderSpec* sliderForId(uint32_t id) const
    {
        for (const auto& slider : kSliders)
            if (slider.id == id) return &slider;
        return nullptr;
    }

    void updateSlider(const SliderSpec& slider, const CPoint& point)
    {
        if (!config.callbacks.setParam) return;
        const auto track = sliderTrackRect(slider);
        const double normalized = std::clamp(
            (point.x - track.left) / track.getWidth(), 0.0, 1.0);
        parameterEdit.set(
            sliderValueFromNormalized(slider.id, normalized));
        invalid();
    }

    double waveVisibleSpan() const
    {
        return 1.0 / std::clamp(waveZoom, 1.0, 128.0);
    }

    double waveNormalizedAtPoint(const CPoint& point) const
    {
        const double local = std::clamp(
            (point.x - kWaveform.left) / kWaveform.getWidth(), 0.0, 1.0);
        return std::clamp(waveViewStart + local * waveVisibleSpan(),
            0.0, 1.0);
    }

    double waveXForNormalized(double value) const
    {
        return kWaveform.left
            + (value - waveViewStart) / waveVisibleSpan()
                * kWaveform.getWidth();
    }

    uint32_t waveBoundaryAtPoint(const CPoint& point) const
    {
        if (!contains(kWaveform, point) || !asset()) return 0u;
        const double start = param(kStartParamId);
        const double end = std::min(1.0, start + param(kLengthParamId));
        const double loopStart = std::clamp(param(kLoopStartParamId),
            start, end);
        const double loopEnd = std::clamp(param(kLoopEndParamId),
            loopStart, end);
        const bool upper = point.y < (kWaveform.top + kWaveform.bottom) * 0.5;
        const std::array<std::pair<uint32_t, double>, 2u> candidates = upper
            ? std::array<std::pair<uint32_t, double>, 2u> {{
                { kStartParamId, start }, { kLengthParamId, end },
            }}
            : std::array<std::pair<uint32_t, double>, 2u> {{
                { kLoopStartParamId, loopStart },
                { kLoopEndParamId, loopEnd },
            }};
        uint32_t nearest = 0u;
        double distance = 11.0;
        for (const auto& candidate : candidates) {
            const double nextDistance = std::abs(
                point.x - waveXForNormalized(candidate.second));
            if (nextDistance < distance) {
                distance = nextDistance;
                nearest = candidate.first;
            }
        }
        return nearest;
    }

    void updateWaveBoundary(const CPoint& point)
    {
        const auto* currentAsset = asset();
        if (!currentAsset || !config.callbacks.setParam
            || waveDragParam == 0u) return;
        const double frameStep = 1.0 / static_cast<double>(
            std::max(1u, currentAsset->frameCount()));
        const double value = waveNormalizedAtPoint(point);
        if (waveDragParam == kStartParamId) {
            const double start = std::clamp(value, 0.0,
                std::max(0.0, waveFixedEnd - frameStep));
            parameterEdit.set(start);
            companionEdit.set(waveFixedEnd - start);
        } else if (waveDragParam == kLengthParamId) {
            const double end = std::clamp(value,
                std::min(1.0, waveFixedStart + frameStep), 1.0);
            parameterEdit.set(end - waveFixedStart);
        } else if (waveDragParam == kLoopStartParamId) {
            parameterEdit.set(std::clamp(value, waveFixedStart,
                std::max(waveFixedStart, waveFixedLoopEnd - frameStep)));
        } else if (waveDragParam == kLoopEndParamId) {
            parameterEdit.set(std::clamp(value,
                std::min(waveFixedEnd, waveFixedLoopStart + frameStep),
                waveFixedEnd));
        }
        invalid();
    }

    int dropdownHitIndex(const MenuSpec& menu, const CPoint& point) const
    {
        if (!contains(menu.dropdown, point) || menu.count == 0u) return -1;
        return static_cast<int>(std::min<uint32_t>(menu.count - 1u,
            static_cast<uint32_t>((point.y - menu.dropdown.top) / 20.0)));
    }

    void closeMenu()
    {
        activeMenu = -1;
        menuHover = -1;
        invalid();
    }

    void selectSample()
    {
        if (!config.callbacks.loadSample) return;
        foundation::FileDialogOptions options;
        options.title = "Load sample";
        const std::string path = foundation::runFileDialog(getFrame(), options);
        if (!path.empty())
            config.callbacks.loadSample(config.callbacks.context, path.c_str());
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
        options.initialDirectory = foundation::presetDirectory(config.pluginName);
        const std::string path = foundation::runFileDialog(getFrame(), options);
        if (path.empty()) return;
        const bool succeeded = save
            ? config.callbacks.savePreset(config.callbacks.context, path.c_str())
            : config.callbacks.loadPreset(config.callbacks.context, path.c_str());
        if (!succeeded) return;
        const auto stem = foundation::pathToUtf8(
            foundation::pathFromUtf8(path.c_str()).stem());
        presetName = stem.empty() ? "CUSTOM" : stem;
        waveZoom = 1.0;
        waveViewStart = 0.0;
        invalid();
    }

    static std::string firstDroppedFile(IDataPackage* package)
    {
        if (!package) return {};
        for (uint32_t i = 0u; i < package->getCount(); ++i) {
            const void* data = nullptr;
            IDataPackage::Type type = IDataPackage::kError;
            const uint32_t size = package->getData(i, data, type);
            if (type != IDataPackage::kFilePath || !data || size == 0u)
                continue;
            const char* path = static_cast<const char*>(data);
            const void* terminator = std::memchr(path, '\0', size);
            const std::size_t length = terminator
                ? static_cast<std::size_t>(
                    static_cast<const char*>(terminator) - path)
                : static_cast<std::size_t>(size);
            return std::string(path, length);
        }
        return {};
    }

    SamplePlayerEditorConfig config;
    const foundation::Palette& style = foundation::palette();
    SharedPointer<CFontDesc> font;
    SharedPointer<CFontDesc> titleFont;
    SharedPointer<CFontDesc> channelFont;
    SharedPointer<CFontDesc> tinyChannelFont;
    SharedPointer<CVSTGUITimer> refreshTimer;
    foundation::ParameterEditSession parameterEdit;
    foundation::ParameterEditSession companionEdit;
    std::string presetName { "INIT" };
    uint32_t dragParam = 0u;
    uint32_t waveDragParam = 0u;
    int activeMenu = -1;
    int menuHover = -1;
    double waveZoom = 1.0;
    double waveViewStart = 0.0;
    double waveFixedStart = 0.0;
    double waveFixedEnd = 1.0;
    double waveFixedLoopStart = 0.0;
    double waveFixedLoopEnd = 1.0;
};

} // namespace

class SamplePlayerEditor final : public foundation::EditorHost {
public:
    SamplePlayerEditor(const SamplePlayerEditorConfig& editorConfig,
                       uint32_t requestedWidth, uint32_t requestedHeight)
        : EditorHost(editorConfig.nativeWidth, editorConfig.nativeHeight,
            requestedWidth, requestedHeight)
        , config(editorConfig)
    {
    }

    bool build()
    {
        if (!ready()) return false;
        auto* view = new SamplePlayerView(config);
        view->loadDocumentationSampleIfRequested();
        return attach(view);
    }

private:
    SamplePlayerEditorConfig config;
};

SamplePlayerEditor* createSamplePlayerEditor(
    const SamplePlayerEditorConfig& config, uint32_t width, uint32_t height)
{
    auto* editor = new (std::nothrow) SamplePlayerEditor(config, width, height);
    if (!editor) return nullptr;
    if (!editor->build()) {
        delete editor;
        return nullptr;
    }
    return editor;
}

void destroySamplePlayerEditor(SamplePlayerEditor* editor) { delete editor; }

bool setSamplePlayerEditorParent(SamplePlayerEditor* editor,
                                 void* nativeParent)
{
    return editor && editor->setParent(nativeParent);
}

bool setSamplePlayerEditorSize(SamplePlayerEditor* editor,
                               uint32_t width, uint32_t height)
{
    return editor && editor->setSize(width, height);
}

bool setSamplePlayerEditorVisible(SamplePlayerEditor* editor, bool visible)
{
    return editor && editor->setVisible(visible);
}

} // namespace s3g::portable_gui
