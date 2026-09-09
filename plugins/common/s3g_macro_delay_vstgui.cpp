#include "s3g_macro_delay_vstgui.h"

#include "s3g_gui_layout.h"
#include "s3g_vstgui_foundation.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace s3g::portable_gui {
namespace {

using namespace VSTGUI;

constexpr uint32_t kTimeParamId = 1u;
constexpr uint32_t kFeedbackParamId = 2u;
constexpr uint32_t kToneParamId = 3u;
constexpr uint32_t kCharacterParamId = 4u;
constexpr uint32_t kSmearParamId = 5u;
constexpr uint32_t kSpreadParamId = 6u;
constexpr uint32_t kDeviationParamId = 7u;
constexpr uint32_t kSkewParamId = 8u;
constexpr uint32_t kMixParamId = 9u;
constexpr uint32_t kOutputParamId = 10u;
constexpr uint32_t kCenterParamId = 11u;
constexpr uint32_t kGlideParamId = 12u;

using foundation::color;

CRect rect(double x, double y, double width, double height)
{
    return foundation::rect(x, y, width, height);
}

CRect rect(const gui_layout::Rect& value)
{
    return rect(value.x, value.y, value.width, value.height);
}

bool contains(const gui_layout::Rect& value, const CPoint& point)
{
    return foundation::contains(rect(value), point);
}

class MacroDelayView final : public foundation::ContentView {
public:
    explicit MacroDelayView(const MacroDelayEditorConfig& editorConfig)
        : ContentView(rect(0.0, 0.0,
            editorConfig.nativeWidth, editorConfig.nativeHeight))
        , config(editorConfig)
    {
        font = foundation::makeUiFont(foundation::fontMetrics().body);
        titleFont = foundation::makeUiFont(foundation::fontMetrics().title);
        parameterEdit.setCallbacks({ config.callbacks.context,
            config.callbacks.beginParamEdit, config.callbacks.setParam,
            config.callbacks.endParamEdit });
    }

    void startRefresh() override
    {
        if (refreshTimer) return;
        refreshTimer = makeOwned<CVSTGUITimer>(
            [this](CVSTGUITimer*) { if (isVisible()) invalid(); }, 50u);
    }

    void stopRefresh() override { refreshTimer = nullptr; }

    void draw(CDrawContext* context) override
    {
        if (!context) return;
        context->setDrawMode(kAntiAliasing);
        context->setFillColor(style.background);
        context->drawRect(getViewSize(), kDrawFilled);

        const auto params = config.callbacks.getParams
            ? config.callbacks.getParams(config.callbacks.context)
            : s3g::MacroDelayParams {};
        const float peak = config.callbacks.getOutputPeak
            ? config.callbacks.getOutputPeak(config.callbacks.context) : 0.0f;
        drawTitleBand(*context, peak);

        const auto& family = gui_layout::kMacroFamilyLayout;
        drawPanel(*context, "OUTPUT", family.output);
        drawPanel(*context, "ENGINE", family.delayEngine);
        drawPanel(*context, "RELATIONSHIPS", family.delayRelationships);
        drawPanel(*context, "LANE DELAY REL", family.preview);

        drawSlider(*context, "OUT", format("%+.1f dB", params.outputGainDb),
            (params.outputGainDb + 60.0) / 72.0,
            gui_layout::rowY(family.output, 0u), family.output);
        drawSlider(*context, "MIX", format("%.0f%%", params.mix * 100.0),
            params.mix, gui_layout::rowY(family.output, 1u), family.output);
        drawSlider(*context, "TIME", format("%.0f ms", params.timeMs),
            (params.timeMs - 5.0) / 1995.0,
            gui_layout::rowY(family.delayEngine, 0u), family.delayEngine);
        drawSlider(*context, "FDBK", format("%.0f%%", params.feedback * 100.0),
            params.feedback / 0.78,
            gui_layout::rowY(family.delayEngine, 1u), family.delayEngine);
        drawSlider(*context, "TONE", format("%.0f%%", params.tone * 100.0),
            params.tone, gui_layout::rowY(family.delayEngine, 2u), family.delayEngine);
        drawSlider(*context, "CHR", format("%.0f%%", params.character * 100.0),
            params.character, gui_layout::rowY(family.delayEngine, 3u), family.delayEngine);
        drawSlider(*context, "SMR", format("%.0f%%", params.smear * 100.0),
            params.smear, gui_layout::rowY(family.delayEngine, 4u), family.delayEngine);
        drawSlider(*context, "SPRD", format("%.0f%%", params.spread * 100.0),
            params.spread, gui_layout::rowY(family.delayRelationships, 0u),
            family.delayRelationships);
        drawSlider(*context, "DEV", format("%.0f%%", params.deviation * 100.0),
            params.deviation, gui_layout::rowY(family.delayRelationships, 1u),
            family.delayRelationships);
        drawSlider(*context, "SKW", format("%+.2f", params.skew),
            (params.skew + 1.0) * 0.5,
            gui_layout::rowY(family.delayRelationships, 2u), family.delayRelationships);
        drawSlider(*context, "CTR", format("%.0f%%", params.center * 100.0),
            params.center, gui_layout::rowY(family.delayRelationships, 3u),
            family.delayRelationships);
        drawSlider(*context, "GLD", format("%.0f ms", params.glideMs),
            (params.glideMs - 10.0) / 1990.0,
            gui_layout::rowY(family.delayRelationships, 4u), family.delayRelationships);

        drawRelationshipPreview(*context, params,
            rect(family.preview.frame.x + 12.0, family.preview.frame.y + 32.0,
                family.preview.frame.width - 24.0,
                family.preview.frame.height - 44.0));
        setDirty(false);
    }

    void onMouseDownEvent(MouseDownEvent& event) override
    {
        if (!event.buttonState.isLeft()) return;
        const CPoint point = event.mousePosition;
        const auto& family = gui_layout::kMacroFamilyLayout;
        const auto band = gui_layout::macroTitleBand(family.canvas);
        if (contains(band.presetMenu, point)) {
            if (config.callbacks.resetToDefaults) {
                config.callbacks.resetToDefaults(config.callbacks.context);
                presetName = "INIT";
                invalid();
            }
            event.consumed = true;
            return;
        }
        if (contains(band.loadButton, point)) {
            selectPreset(false);
            event.consumed = true;
            return;
        }
        if (contains(band.saveButton, point)) {
            selectPreset(true);
            event.consumed = true;
            return;
        }

        const std::array<uint32_t, 2> outputIds { kOutputParamId, kMixParamId };
        const std::array<uint32_t, 5> engineIds {
            kTimeParamId, kFeedbackParamId, kToneParamId,
            kCharacterParamId, kSmearParamId
        };
        const std::array<uint32_t, 5> relationshipIds {
            kSpreadParamId, kDeviationParamId, kSkewParamId,
            kCenterParamId, kGlideParamId
        };
        const auto hitRows = [&](const gui_layout::Panel& panel, const auto& ids) {
            for (uint32_t row = 0u; row < ids.size(); ++row) {
                if (contains(gui_layout::sliderHitRect(panel, row), point)) {
                    beginSlider(ids[row], point, event.clickCount);
                    return true;
                }
            }
            return false;
        };
        if (hitRows(family.output, outputIds)
            || hitRows(family.delayEngine, engineIds)
            || hitRows(family.delayRelationships, relationshipIds)) {
            event.consumed = true;
        }
    }

    void onMouseMoveEvent(MouseMoveEvent& event) override
    {
        if (dragParam == 0u || !event.buttonState.has(MouseButton::Left)) return;
        updateSlider(event.mousePosition);
        event.consumed = true;
    }

    void onMouseUpEvent(MouseUpEvent& event) override
    {
        if (dragParam == 0u) return;
        parameterEdit.end();
        dragParam = 0u;
        event.consumed = true;
    }

    void onMouseCancelEvent(MouseCancelEvent& event) override
    {
        if (dragParam == 0u) return;
        parameterEdit.end();
        dragParam = 0u;
        event.consumed = true;
    }

private:
    static std::string format(const char* pattern, double value)
    {
        char buffer[64] {};
        std::snprintf(buffer, sizeof(buffer), pattern, value);
        return buffer;
    }

    void text(CDrawContext& context, const std::string& value,
              double x, double y, double width, CColor textColor,
              CHoriTxtAlign align = kLeftText, bool title = false)
    {
        foundation::drawTextLine(context, value, x, y, width, textColor,
            title ? titleFont : font, align);
    }

    void textInRect(CDrawContext& context, const std::string& value,
                    const CRect& bounds, CColor textColor,
                    CHoriTxtAlign align = kCenterText, bool title = false)
    {
        foundation::drawTextInRect(context, value, bounds, textColor,
            title ? titleFont : font, align);
    }

    void drawPanel(CDrawContext& context, const char* name,
                   const gui_layout::Panel& panel)
    {
        const auto bounds = rect(panel.frame);
        foundation::drawPanel(context, bounds, name, font,
            gui_layout::kStandardMetrics.headerHeight,
            gui_layout::kStandardMetrics.headerLabelInset);
    }

    void drawSlider(CDrawContext& context, const char* name,
                    const std::string& value, double norm, double y,
                    const gui_layout::Panel& panel)
    {
        const double labelX = gui_layout::processorLabelX(panel.frame.x);
        const double trackX = gui_layout::processorControlX(panel.frame.x);
        const double valueX = gui_layout::processorValueX(
            panel.frame.x, panel.frame.width);
        const double trackWidth = gui_layout::processorTrackWidth(panel.frame.width);
        constexpr double valueWidth = gui_layout::kStandardMetrics.processorValueWidth;
        text(context, name, labelX, y - 2.0, trackX - labelX - 4.0, style.label);

        const auto track = rect(trackX, y + 1.0, trackWidth, 9.0);
        foundation::drawHorizontalSlider(context, track, norm, y - 1.0, 13.0);
        text(context, value, valueX, y - 2.0, valueWidth, style.value, kRightText);
    }

    void drawTitleBand(CDrawContext& context, float peak)
    {
        const auto& family = gui_layout::kMacroFamilyLayout;
        const auto band = gui_layout::macroTitleBand(family.canvas);
        const std::string title = "s3g MACRO DELAY "
            + std::to_string(config.channelCount) + "CH";
        foundation::drawPluginTitle(context, title,
            rect(band.titleX, band.titleY - 2.0,
                band.presetLabelX - band.titleX - 8.0, 15.0), titleFont);
        text(context, "PRESET", band.presetLabelX, band.controlY + 1.0,
            band.presetMenu.x - band.presetLabelX - 4.0, style.label);

        const auto presetBounds = rect(band.presetMenu);
        foundation::drawMenuBox(context, presetBounds, presetName, font);
        drawButton(context, band.loadButton, "LOAD");
        drawButton(context, band.saveButton, "SAVE");

        const std::string status = format("PK %+.1f",
            20.0 * std::log10(std::max(0.000001f, peak)));
        context.setFont(font);
        const double statusWidth = context.getStringWidth(status.c_str());
        text(context, status, family.canvas.width - statusWidth - band.statusRightInset,
            band.titleY, statusWidth + 1.0, style.value);
    }

    void drawButton(CDrawContext& context, const gui_layout::Rect& button,
                    const char* label)
    {
        const auto buttonBounds = rect(button);
        foundation::drawButton(context, buttonBounds, label, font, false,
            color(0x2b2b2b));
    }

    void drawRelationshipPreview(CDrawContext& context,
                                 const s3g::MacroDelayParams& params,
                                 const CRect& bounds)
    {
        context.setFillColor(color(0x151515));
        context.drawRect(bounds, kDrawFilled);
        const double baseY = bounds.top + 32.0;
        const double rowHeight = (bounds.getHeight() - 46.0)
            / static_cast<double>(std::max(1u, config.channelCount - 1u));
        const double labelX = bounds.left + 10.0;
        const double barX = bounds.left + 48.0;
        const double barWidth = bounds.getWidth() - 66.0;
        std::array<float, s3g::kMacroDelayChannels> ratios {};
        float maxRatio = 0.001f;
        const uint32_t channels = std::min(config.channelCount, s3g::kMacroDelayChannels);
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            const float u = static_cast<float>(channel)
                / static_cast<float>(std::max(1u, channels - 1u));
            const float centered = std::clamp((u - params.center) * 2.0f, -1.0f, 1.0f);
            uint32_t x = channel * 747796405u + 2891336453u;
            x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
            x = (x >> 22u) ^ x;
            const float deviation = static_cast<float>(x & 0xffffu) / 32767.5f - 1.0f;
            ratios[channel] = std::pow(2.0f, centered * params.spread)
                * std::pow(2.0f, deviation * params.deviation * 0.5f)
                * std::pow(2.0f, params.skew * u);
            maxRatio = std::max(maxRatio, ratios[channel]);
        }
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            const double y = baseY + static_cast<double>(channel) * rowHeight;
            text(context, "L" + std::to_string(channel + 1u), labelX, y - 4.0,
                34.0, style.value);
            context.setFillColor(color(0x1e1e1e));
            context.drawRect(rect(barX, y, barWidth, 6.0), kDrawFilled);
            context.setFillColor(style.accent);
            context.drawRect(rect(barX + 1.0, y + 1.0,
                std::max(1.0, (barWidth - 2.0) * ratios[channel] / maxRatio), 4.0),
                kDrawFilled);
        }
    }

    void beginSlider(uint32_t paramId, const CPoint& point, uint32_t clickCount)
    {
        if (clickCount >= 2u && config.callbacks.getDefaultValue
            && config.callbacks.setParam) {
            double defaultValue = 0.0;
            if (config.callbacks.getDefaultValue(
                    config.callbacks.context, paramId, &defaultValue)) {
                parameterEdit.perform(paramId, defaultValue);
                dragParam = 0u;
                invalid();
                return;
            }
        }
        dragParam = paramId;
        parameterEdit.begin(paramId);
        updateSlider(point);
    }

    void updateSlider(const CPoint& point)
    {
        if (!config.callbacks.setParam || dragParam == 0u) return;
        const auto& family = gui_layout::kMacroFamilyLayout;
        const bool outputSlider = dragParam == kOutputParamId || dragParam == kMixParamId;
        const bool engineSlider = dragParam >= kTimeParamId && dragParam <= kSmearParamId;
        const auto& panel = outputSlider ? family.output
            : (engineSlider ? family.delayEngine : family.delayRelationships);
        const double controlX = gui_layout::processorControlX(panel.frame.x);
        const double trackWidth = gui_layout::processorTrackWidth(panel.frame.width);
        const double norm = std::clamp((point.x - controlX) / trackWidth, 0.0, 1.0);
        double value = norm;
        switch (dragParam) {
        case kTimeParamId: value = 5.0 + norm * 1995.0; break;
        case kFeedbackParamId: value = norm * 0.78; break;
        case kSkewParamId: value = -1.0 + norm * 2.0; break;
        case kGlideParamId: value = 10.0 + norm * 1990.0; break;
        case kOutputParamId: value = -60.0 + norm * 72.0; break;
        default: break;
        }
        parameterEdit.set(value);
        invalid();
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
        invalid();
    }

    MacroDelayEditorConfig config;
    const foundation::Palette& style = foundation::palette();
    SharedPointer<CFontDesc> font;
    SharedPointer<CFontDesc> titleFont;
    SharedPointer<CVSTGUITimer> refreshTimer;
    foundation::ParameterEditSession parameterEdit;
    std::string presetName { "INIT" };
    uint32_t dragParam = 0u;
};

} // namespace

class MacroDelayEditor final : public foundation::EditorHost {
public:
    MacroDelayEditor(const MacroDelayEditorConfig& editorConfig,
                     uint32_t requestedWidth, uint32_t requestedHeight)
        : EditorHost(editorConfig.nativeWidth, editorConfig.nativeHeight,
            requestedWidth, requestedHeight)
        , config(editorConfig)
    {
    }

    bool build()
    {
        return ready() && attach(new MacroDelayView(config));
    }

private:
    MacroDelayEditorConfig config;
};

MacroDelayEditor* createMacroDelayEditor(
    const MacroDelayEditorConfig& config, uint32_t width, uint32_t height)
{
    auto* editor = new (std::nothrow) MacroDelayEditor(config, width, height);
    if (!editor) return nullptr;
    if (!editor->build()) {
        delete editor;
        return nullptr;
    }
    return editor;
}

void destroyMacroDelayEditor(MacroDelayEditor* editor) { delete editor; }

bool setMacroDelayEditorParent(MacroDelayEditor* editor, void* nativeParent)
{
    return editor && editor->setParent(nativeParent);
}

bool setMacroDelayEditorSize(
    MacroDelayEditor* editor, uint32_t width, uint32_t height)
{
    return editor && editor->setSize(width, height);
}

bool setMacroDelayEditorVisible(MacroDelayEditor* editor, bool visible)
{
    return editor && editor->setVisible(visible);
}

} // namespace s3g::portable_gui
