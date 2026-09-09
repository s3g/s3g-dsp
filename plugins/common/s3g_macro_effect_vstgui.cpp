#include "s3g_macro_effect_vstgui.h"

#include "s3g_gui_layout.h"
#include "s3g_macro_fracture.h"
#include "s3g_macro_shred.h"
#include "s3g_vstgui_foundation.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace s3g::portable_gui {
namespace {

using namespace VSTGUI;
using foundation::color;

constexpr uint32_t kNoParam = 0u;

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

uint32_t hashLane(uint32_t lane)
{
    uint32_t value = lane * 747796405u + 2891336453u;
    value = ((value >> ((value >> 28u) + 4u)) ^ value) * 277803737u;
    return (value >> 22u) ^ value;
}

class MacroEffectView final : public foundation::ContentView {
public:
    explicit MacroEffectView(const MacroEffectEditorConfig& editorConfig)
        : ContentView(rect(0.0, 0.0,
            editorConfig.nativeWidth, editorConfig.nativeHeight))
        , config(editorConfig)
    {
        font = foundation::makeUiFont(foundation::fontMetrics().body);
        titleFont = foundation::makeUiFont(foundation::fontMetrics().title);
        parameterEdit.setCallbacks({ config.callbacks.context,
            config.callbacks.beginParamEdit, config.callbacks.setParam,
            config.callbacks.endParamEdit });
        loopHistory.fill(0.0f);
        reductionHistory.fill(0.0f);
    }

    void startRefresh() override
    {
        if (refreshTimer) return;
        refreshTimer = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) {
            captureHistory();
            if (isVisible()) invalid();
        }, 50u);
    }

    void stopRefresh() override { refreshTimer = nullptr; }

    void draw(CDrawContext* context) override
    {
        if (!context) return;
        context->setDrawMode(kAntiAliasing);
        context->setFillColor(style.background);
        context->drawRect(getViewSize(), kDrawFilled);
        if (config.kind == MacroEffectKind::Pitch) drawPitch(*context);
        else drawShredLike(*context);
        if (menuOpen) drawDropdown(*context);
        setDirty(false);
    }

    void onMouseDownEvent(MouseDownEvent& event) override
    {
        if (!event.buttonState.isLeft()) return;
        const CPoint point = event.mousePosition;
        if (menuOpen) {
            const int hit = dropdownHit(point);
            menuOpen = false;
            menuHover = -1;
            if (hit >= 0) parameterEdit.perform(menuParamId(), hit);
            invalid();
            event.consumed = true;
            return;
        }

        const auto canvas = config.kind == MacroEffectKind::Pitch
            ? gui_layout::kMacroFamilyLayout.canvas
            : selectedShredLayout().canvas;
        const auto band = gui_layout::macroTitleBand(canvas);
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

        if (config.kind != MacroEffectKind::Pitch) {
            const auto& family = selectedShredLayout();
            if (contains(family.panicButton, point)) {
                if (config.callbacks.requestPanic)
                    config.callbacks.requestPanic(config.callbacks.context);
                event.consumed = true;
                return;
            }
            if (foundation::contains(processorMenuRect(), point)) {
                menuOpen = true;
                invalid();
                event.consumed = true;
                return;
            }
        }

        if (hitSlider(point, event.clickCount)) event.consumed = true;
    }

    void onMouseMoveEvent(MouseMoveEvent& event) override
    {
        if (menuOpen) {
            const int next = dropdownHit(event.mousePosition);
            if (next != menuHover) {
                menuHover = next;
                invalid();
            }
            event.consumed = true;
            return;
        }
        if (dragParam == kNoParam
            || !event.buttonState.has(MouseButton::Left)) return;
        updateSlider(event.mousePosition);
        event.consumed = true;
    }

    void onMouseUpEvent(MouseUpEvent& event) override
    {
        if (dragParam == kNoParam) return;
        parameterEdit.end();
        dragParam = kNoParam;
        event.consumed = true;
    }

    void onMouseCancelEvent(MouseCancelEvent& event) override
    {
        menuOpen = false;
        menuHover = -1;
        if (dragParam != kNoParam) parameterEdit.end();
        dragParam = kNoParam;
        event.consumed = true;
    }

private:
    const gui_layout::MacroShredFamilyLayout& selectedShredLayout() const
    {
        return config.channelCount == 1u
            ? gui_layout::kMacroShredMonoFamilyLayout
            : gui_layout::kMacroShredFamilyLayout;
    }

    double param(uint32_t id) const
    {
        double value = 0.0;
        if (config.callbacks.getParamValue)
            config.callbacks.getParamValue(config.callbacks.context, id, &value);
        return value;
    }

    float meter(float (*callback)(void*)) const
    {
        return callback ? callback(config.callbacks.context) : 0.0f;
    }

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
        CHoriTxtAlign align = kCenterText)
    {
        foundation::drawTextInRect(
            context, value, bounds, textColor, font, align);
    }

    void drawPanel(CDrawContext& context, const char* name,
        const gui_layout::Panel& panel)
    {
        foundation::drawPanel(context, rect(panel.frame), name, font,
            gui_layout::kStandardMetrics.headerHeight,
            gui_layout::kStandardMetrics.headerLabelInset);
    }

    void drawButton(CDrawContext& context,
        const gui_layout::Rect& button, const char* label)
    {
        foundation::drawButton(context, rect(button), label, font, false,
            color(0x2b2b2b));
    }

    void drawTitleBand(CDrawContext& context,
        const gui_layout::Canvas& canvas)
    {
        const auto band = gui_layout::macroTitleBand(canvas);
        std::string effect;
        if (config.kind == MacroEffectKind::Pitch) effect = "PITCH";
        else if (config.kind == MacroEffectKind::Shred) effect = "SHRED";
        else effect = "FRACTURE";
        std::string title = "s3g MACRO " + effect + " ";
        title += config.channelCount == 1u
            ? "MONO" : std::to_string(config.channelCount) + "CH";
        foundation::drawPluginTitle(context, title,
            rect(band.titleX, band.titleY - 2.0,
                band.presetLabelX - band.titleX - 8.0, 15.0), titleFont);
        text(context, "PRESET", band.presetLabelX, band.controlY + 1.0,
            band.presetMenu.x - band.presetLabelX - 4.0, style.label);
        foundation::drawMenuBox(context, rect(band.presetMenu),
            presetName, font);
        drawButton(context, band.loadButton, "LOAD");
        drawButton(context, band.saveButton, "SAVE");

        const float peak = meter(config.callbacks.getOutputPeak);
        const std::string status = format("PK %+.1f",
            20.0 * std::log10(std::max(0.000001f, peak)));
        context.setFont(font);
        const double width = context.getStringWidth(status.c_str());
        text(context, status, canvas.width - width - band.statusRightInset,
            band.titleY, width + 1.0, style.value);
    }

    void drawSlider(CDrawContext& context, const char* label,
        const std::string& value, double normalized, double y,
        const gui_layout::Panel& panel)
    {
        const double labelX = gui_layout::processorLabelX(panel.frame.x);
        const double trackX = gui_layout::processorControlX(panel.frame.x);
        const double valueX = gui_layout::processorValueX(
            panel.frame.x, panel.frame.width);
        const double trackWidth =
            gui_layout::processorTrackWidth(panel.frame.width);
        text(context, label, labelX, y - 2.0,
            trackX - labelX - 4.0, style.label);
        foundation::drawHorizontalSlider(context,
            rect(trackX, y + 1.0, trackWidth, 9.0), normalized,
            y - 1.0, 13.0);
        text(context, value, valueX, y - 2.0,
            gui_layout::kStandardMetrics.processorValueWidth,
            style.value, kRightText);
    }

    void drawPitch(CDrawContext& context)
    {
        const auto& family = gui_layout::kMacroFamilyLayout;
        drawTitleBand(context, family.canvas);
        drawPanel(context, "OUTPUT", family.output);
        drawPanel(context, "ENGINE", family.pitchEngine);
        drawPanel(context, "RELATIONSHIPS", family.pitchRelationships);
        drawPanel(context, "LANE PITCH REL", family.preview);

        const double pitch = param(1u);
        const double fine = param(2u);
        const double window = param(3u);
        const double spread = param(4u);
        const double deviation = param(5u);
        const double skew = param(6u);
        const double center = param(7u);
        const double glide = param(8u);
        const double mix = param(9u);
        const double output = param(10u);
        drawSlider(context, "OUT", format("%+.1f dB", output),
            (output + 60.0) / 72.0,
            gui_layout::rowY(family.output, 0u), family.output);
        drawSlider(context, "MIX", format("%.0f%%", mix * 100.0), mix,
            gui_layout::rowY(family.output, 1u), family.output);
        drawSlider(context, "PCH", format("%+.1f st", pitch),
            (pitch + 24.0) / 48.0,
            gui_layout::rowY(family.pitchEngine, 0u), family.pitchEngine);
        drawSlider(context, "FNE", format("%+.0f ct", fine),
            (fine + 100.0) / 200.0,
            gui_layout::rowY(family.pitchEngine, 1u), family.pitchEngine);
        drawSlider(context, "WIN", format("%.0f ms", window),
            (window - 20.0) / 160.0,
            gui_layout::rowY(family.pitchEngine, 2u), family.pitchEngine);
        drawRelationshipSliders(context, family.pitchRelationships,
            spread, deviation, skew, center, glide);
        drawPitchPreview(context, pitch, fine, spread, deviation, skew,
            center, rect(family.preview.frame.x + 12.0,
                family.preview.frame.y + 32.0,
                family.preview.frame.width - 24.0,
                family.preview.frame.height - 44.0));
    }

    void drawRelationshipSliders(CDrawContext& context,
        const gui_layout::Panel& panel, double spread, double deviation,
        double skew, double center, double glide)
    {
        drawSlider(context, "SPRD", format("%.0f%%", spread * 100.0),
            spread, gui_layout::rowY(panel, 0u), panel);
        drawSlider(context, "DEV", format("%.0f%%", deviation * 100.0),
            deviation, gui_layout::rowY(panel, 1u), panel);
        drawSlider(context, "SKW", format("%+.2f", skew),
            (skew + 1.0) * 0.5, gui_layout::rowY(panel, 2u), panel);
        drawSlider(context, "CTR", format("%.0f%%", center * 100.0),
            center, gui_layout::rowY(panel, 3u), panel);
        drawSlider(context, "GLD", format("%.0f ms", glide),
            (glide - 10.0) / 1990.0,
            gui_layout::rowY(panel, 4u), panel);
    }

    void drawPitchPreview(CDrawContext& context, double pitch, double fine,
        double spread, double deviation, double skew, double center,
        const CRect& bounds)
    {
        context.setFillColor(color(0x111111));
        context.drawRect(bounds, kDrawFilled);
        context.setFrameColor(color(0x444444));
        context.drawRect(bounds, kDrawStroked);
        const double baseY = bounds.top + 32.0;
        const double rowHeight = (bounds.getHeight() - 46.0)
            / static_cast<double>(std::max(1u, config.channelCount - 1u));
        const double labelX = bounds.left + 10.0;
        const double barX = bounds.left + 48.0;
        const double barWidth = bounds.getWidth() - 66.0;
        std::array<double, 24u> ratios {};
        double maxRatio = 0.001;
        const uint32_t channels = std::min(config.channelCount, 24u);
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            const double u = static_cast<double>(channel)
                / static_cast<double>(std::max(1u, channels - 1u));
            const double centered = std::clamp((u - center) * 2.0, -1.0, 1.0);
            const double random = static_cast<double>(hashLane(channel) & 0xffffu)
                / 32767.5 - 1.0;
            const double semis = pitch + fine * 0.01
                + centered * spread * 12.0 + random * deviation * 3.0
                + skew * u * 6.0;
            ratios[channel] = std::pow(2.0,
                std::clamp(semis, -24.0, 24.0) / 12.0);
            maxRatio = std::max(maxRatio, ratios[channel]);
        }
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            const double y = baseY + channel * rowHeight;
            text(context, "L" + std::to_string(channel + 1u), labelX,
                y - 4.0, 34.0, style.value);
            context.setFillColor(color(0x171717));
            context.drawRect(rect(barX, y, barWidth, 6.0), kDrawFilled);
            context.setFillColor(style.accent);
            context.drawRect(rect(barX + 1.0, y + 1.0,
                std::max(1.0, (barWidth - 2.0)
                    * ratios[channel] / maxRatio), 4.0), kDrawFilled);
        }
    }

    void drawShredLike(CDrawContext& context)
    {
        const auto& family = selectedShredLayout();
        const bool shred = config.kind == MacroEffectKind::Shred;
        drawTitleBand(context, family.canvas);
        drawPanel(context, "OUTPUT", family.output);
        drawPanel(context, "ENGINE", family.engine);
        drawPanel(context, shred
            ? (config.channelCount > 1u ? "FEEDBACK HISTORY" : "CONTAINMENT")
            : "FRACTURE ACTIVITY", family.containment);
        if (config.channelCount > 1u) {
            drawPanel(context, "RELATIONSHIPS", family.relationships);
            drawPanel(context, shred ? "LANE COLOR REL" : "LANE COLOR / BIAS",
                family.preview);
        }

        const double output = param(14u);
        const double mix = param(13u);
        drawSlider(context, "OUT", format("%+.1f dB", output),
            (output + 60.0) / 66.0,
            gui_layout::rowY(family.output, 0u), family.output);
        drawSlider(context, "MIX", format("%.0f%%", mix * 100.0), mix,
            gui_layout::rowY(family.output, 1u), family.output);

        drawProcessorMenu(context, family);
        const double input = param(1u);
        drawSlider(context, "INPUT", format("%+.1f dB", input),
            (input + 24.0) / 60.0,
            gui_layout::rowY(family.engine, 1u), family.engine);
        if (shred) drawShredEngine(context, family);
        else drawFractureEngine(context, family);

        if (config.channelCount > 1u) {
            const double spread = param(8u);
            const double deviation = param(9u);
            const double skew = param(10u);
            const double center = param(11u);
            const double glide = param(12u);
            drawRelationshipSliders(context, family.relationships,
                spread, deviation, skew, center, glide);
            drawShredLikePreview(context, spread, deviation, skew, center,
                rect(family.preview.frame.x + 12.0,
                    family.preview.frame.y + 32.0,
                    family.preview.frame.width - 24.0,
                    family.preview.frame.height - 44.0));
            if (shred) drawFeedbackHistory(context, family);
            else drawFractureField(context, family);
        }
        drawContainmentControls(context, family);
    }

    void drawProcessorMenu(CDrawContext& context,
        const gui_layout::MacroShredFamilyLayout& family)
    {
        const uint32_t selected = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::lround(param(menuParamId()))),
            0u, menuItemCount() - 1u);
        const auto bounds = processorMenuRect();
        const double y = gui_layout::rowY(family.engine, 0u);
        const double labelX = gui_layout::processorLabelX(family.engine.frame.x);
        text(context, config.kind == MacroEffectKind::Shred
                ? "CIRCUIT" : "PROCESSOR",
            labelX, y - 2.0, bounds.left - labelX - 4.0, style.label);
        foundation::drawMenuBox(context, bounds, menuItem(selected), font);
    }

    void drawShredEngine(CDrawContext& context,
        const gui_layout::MacroShredFamilyLayout& family)
    {
        const char* labels[] = { "PRS", "SHRED", "FDBK", "COLOR",
            "REACT", "TUNE", "BODY" };
        const uint32_t ids[] = { 2u, 3u, 4u, 5u, 6u, 15u, 7u };
        for (uint32_t index = 0u; index < 7u; ++index) {
            const double value = param(ids[index]);
            drawSlider(context, labels[index], format("%.0f%%", value * 100.0),
                value, gui_layout::rowY(family.engine, index + 2u),
                family.engine);
        }
    }

    void drawFractureEngine(CDrawContext& context,
        const gui_layout::MacroShredFamilyLayout& family)
    {
        const auto processor = static_cast<s3g::FractureProcessor>(
            std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(param(2u))),
                0u, s3g::kFractureProcessorCount - 1u));
        const char* labels[] = {
            s3g::fractureAmountLabel(processor),
            s3g::fractureColorLabel(processor),
            s3g::fractureBiasLabel(processor), "REACT", "MEMORY"
        };
        for (uint32_t index = 0u; index < 5u; ++index) {
            const uint32_t id = index + 3u;
            const double value = param(id);
            const bool bipolar = id == 5u;
            drawSlider(context, labels[index], bipolar
                    ? format("%+.2f", value) : format("%.0f%%", value * 100.0),
                bipolar ? (value + 1.0) * 0.5 : value,
                gui_layout::rowY(family.engine, index + 2u), family.engine);
        }
    }

    void drawShredLikePreview(CDrawContext& context, double spread,
        double deviation, double skew, double center, const CRect& bounds)
    {
        context.setFillColor(color(0x111111));
        context.drawRect(bounds, kDrawFilled);
        context.setFrameColor(color(0x444444));
        context.drawRect(bounds, kDrawStroked);
        const double baseY = bounds.top + 28.0;
        const double rowHeight = (bounds.getHeight() - 38.0)
            / static_cast<double>(std::max(1u, config.channelCount));
        const double labelX = bounds.left + 10.0;
        const double barX = bounds.left + 48.0;
        const double barWidth = bounds.getWidth() - 64.0;
        const double colorValue = param(config.kind == MacroEffectKind::Shred
            ? 5u : 4u);
        const double biasValue = config.kind == MacroEffectKind::Fracture
            ? param(5u) : 0.0;
        const uint32_t stride = std::max(1u, (config.channelCount + 7u) / 8u);
        for (uint32_t channel = 0u; channel < config.channelCount; ++channel) {
            const double u = config.channelCount > 1u
                ? static_cast<double>(channel) / (config.channelCount - 1u)
                : 0.5;
            const double centered = std::clamp((u - center) * 2.0, -1.0, 1.0);
            const double random = static_cast<double>(hashLane(channel) & 0xffffu)
                / 32767.5 - 1.0;
            double marker = 0.5;
            double secondary = 0.5;
            if (config.kind == MacroEffectKind::Shred) {
                const double shift = centered * spread * 1.5
                    + random * deviation * 0.75
                    + skew * (u - 0.5) * 0.75;
                marker = std::clamp(0.5 + shift * 0.22, 0.0, 1.0);
            } else {
                marker = std::clamp(colorValue + centered * spread * 0.42
                    + random * deviation * 0.18, 0.0, 1.0);
                const double laneBias = std::clamp(biasValue
                    + skew * (u - 0.5) * 0.65
                    + random * deviation * 0.18, -1.0, 1.0);
                secondary = (laneBias + 1.0) * 0.5;
            }
            const double y = baseY + channel * rowHeight;
            if (channel % stride == 0u || channel + 1u == config.channelCount)
                text(context, "L" + std::to_string(channel + 1u),
                    labelX, y - 4.0, 34.0, style.value);
            const auto track = rect(barX, y, barWidth, 6.0);
            context.setFillColor(color(0x171717));
            context.drawRect(track, kDrawFilled);
            context.setFrameColor(color(0x333333));
            context.drawRect(track, kDrawStroked);
            context.setFillColor(style.accent);
            context.drawRect(rect(track.left + 2.0
                    + (track.getWidth() - 4.0) * marker - 2.0,
                track.top - 2.0, 4.0, 10.0), kDrawFilled);
            if (config.kind == MacroEffectKind::Fracture) {
                context.setFillColor(color(0x6f6f6f));
                context.drawRect(rect(track.left + 2.0
                        + (track.getWidth() - 4.0) * secondary - 1.0,
                    track.top, 2.0, 6.0), kDrawFilled);
            }
        }
    }

    void drawContainmentControls(CDrawContext& context,
        const gui_layout::MacroShredFamilyLayout& family)
    {
        const float activity = std::clamp(
            meter(config.callbacks.getActivity), 0.0f, 1.0f);
        const auto meterBounds = rect(family.containmentMeter);
        context.setFillColor(color(0x111111));
        context.drawRect(meterBounds, kDrawFilled);
        context.setFrameColor(color(0x444444));
        context.drawRect(meterBounds, kDrawStroked);
        context.setFillColor(style.accent);
        context.drawRect(rect(meterBounds.left + 1.0, meterBounds.top + 1.0,
            std::max(0.0, (meterBounds.getWidth() - 2.0) * activity),
            std::max(0.0, meterBounds.getHeight() - 2.0)), kDrawFilled);
        const double labelX = family.containment.frame.x + 16.0;
        if (config.kind == MacroEffectKind::Shred) {
            const float frequency = meter(config.callbacks.getFrequencyHz);
            text(context, frequency >= 1000.0f
                    ? format("LOOP %.1fK", frequency * 0.001)
                    : format("LOOP %.0f", frequency),
                labelX, family.containmentMeter.y - 2.0,
                family.containmentMeter.x - labelX - 5.0, style.label);
        } else {
            text(context, "ACTIVITY", labelX,
                family.containmentMeter.y - 2.0,
                family.containmentMeter.x - labelX - 5.0, style.label);
        }
        foundation::drawButton(context, rect(family.panicButton),
            "PANIC", font, false, color(0x161616));
    }

    void drawFeedbackHistory(CDrawContext& context,
        const gui_layout::MacroShredFamilyLayout& family)
    {
        const auto field = rect(family.containmentField);
        context.setFillColor(style.strip);
        context.drawRect(field, kDrawFilled);
        context.setFrameColor(style.grid);
        context.drawRect(field, kDrawStroked);
        const float activity = std::clamp(
            meter(config.callbacks.getActivity), 0.0f, 1.0f);
        const float activityDb = 20.0f
            * std::log10(std::max(activity, 0.000001f));
        const float reduction = std::clamp(
            meter(config.callbacks.getSecondaryActivity), 0.0f, 1.0f);
        const std::string summary = activity > 0.000001f
            ? format("LOOP %+.0f DB", activityDb)
                + "   " + format("CUT %.0f%%", reduction * 100.0)
            : "LOOP -INF DB   " + format("CUT %.0f%%", reduction * 100.0);
        text(context, summary, field.left + 10.0, field.top + 7.0,
            field.getWidth() - 20.0, style.value);
        const auto loopPlot = rect(field.left + 38.0, field.top + 29.0,
            field.getWidth() - 48.0, 55.0);
        const auto reductionPlot = rect(loopPlot.left, field.top + 95.0,
            loopPlot.getWidth(), 55.0);
        drawHistoryPlot(context, loopPlot, loopHistory, style.text, 1.5);
        drawHistoryPlot(context, reductionPlot, reductionHistory,
            style.fill, 2.0);
        text(context, "LOOP", field.left + 7.0,
            loopPlot.top + 20.0, 31.0, style.value);
        text(context, "CUT", field.left + 10.0,
            reductionPlot.top + 20.0, 28.0, style.value);
        const char* stateText = activityDb < -54.0f && reduction < 0.01f
            ? "QUIET" : (reduction >= 0.20f ? "GOVERNING"
                : (reduction >= 0.01f ? "TRIMMING" : "OPEN"));
        const float frequency = meter(config.callbacks.getFrequencyHz);
        text(context, std::string("8S HISTORY   ") + stateText
                + "   " + format("LOOP %.0f HZ", frequency),
            field.left + 10.0, field.bottom - 24.0,
            field.getWidth() - 20.0, style.value);
    }

    void drawHistoryPlot(CDrawContext& context, const CRect& plot,
        const std::array<float, 160u>& history, CColor lineColor,
        double lineWidth)
    {
        context.setFillColor(style.background);
        context.drawRect(plot, kDrawFilled);
        context.setFrameColor(style.grid);
        context.drawRect(plot, kDrawStroked);
        context.setFrameColor(style.grid);
        context.drawLine(CPoint(plot.left, std::floor(plot.getCenter().y)),
            CPoint(plot.right, std::floor(plot.getCenter().y)));
        for (uint32_t division = 1u; division < 4u; ++division) {
            const double x = plot.left + plot.getWidth() * division / 4.0;
            context.drawLine(CPoint(std::floor(x), plot.top),
                CPoint(std::floor(x), plot.bottom));
        }
        CGraphicsPath* path = context.createGraphicsPath();
        if (!path) return;
        for (uint32_t sample = 0u; sample < history.size(); ++sample) {
            const uint32_t index = (historyWrite + sample) % history.size();
            const double x = plot.left + 2.0 + (plot.getWidth() - 4.0)
                * sample / static_cast<double>(history.size() - 1u);
            const double y = plot.bottom - 2.0
                - std::clamp<double>(history[index], 0.0, 1.0)
                    * (plot.getHeight() - 4.0);
            if (sample == 0u) path->beginSubpath(CPoint(x, y));
            else path->addLine(CPoint(x, y));
        }
        context.setFrameColor(lineColor);
        context.setLineWidth(lineWidth);
        context.drawGraphicsPath(path, CDrawContext::kPathStroked);
        path->forget();
        context.setLineWidth(1.0);
    }

    void drawFractureField(CDrawContext& context,
        const gui_layout::MacroShredFamilyLayout& family)
    {
        const auto field = rect(family.containmentField);
        context.setFillColor(color(0x111111));
        context.drawRect(field, kDrawFilled);
        context.setFrameColor(color(0x444444));
        context.drawRect(field, kDrawStroked);
        const double activity = std::clamp<double>(
            meter(config.callbacks.getActivity), 0.0, 1.0);
        CGraphicsPath* crack = context.createGraphicsPath();
        if (crack) {
            crack->beginSubpath(CPoint(field.getCenter().x, field.top + 12.0));
            for (uint32_t segment = 1u; segment <= 8u; ++segment) {
                const double y = field.top + 12.0 + (field.getHeight() - 24.0)
                    * segment / 8.0;
                const double side = segment % 2u == 0u ? -1.0 : 1.0;
                const double x = field.getCenter().x + side
                    * (12.0 + activity * 58.0)
                    * (0.35 + 0.65 * segment / 8.0);
                crack->addLine(CPoint(x, y));
            }
            context.setFrameColor(style.accent);
            context.setLineWidth(1.0 + activity * 2.0);
            context.drawGraphicsPath(crack, CDrawContext::kPathStroked);
            crack->forget();
            context.setLineWidth(1.0);
        }
        text(context, "BOUNDED RECURRENCE", field.left + 12.0,
            field.bottom - 26.0, field.getWidth() - 24.0, style.value);
    }

    void captureHistory()
    {
        if (config.kind != MacroEffectKind::Shred) return;
        const float activity = std::clamp(
            meter(config.callbacks.getActivity), 0.0f, 1.0f);
        const float db = 20.0f
            * std::log10(std::max(activity, 0.000001f));
        loopHistory[historyWrite] = std::clamp(
            (db + 60.0f) / 60.0f, 0.0f, 1.0f);
        reductionHistory[historyWrite] = std::clamp(
            meter(config.callbacks.getSecondaryActivity), 0.0f, 1.0f);
        historyWrite = (historyWrite + 1u) % loopHistory.size();
    }

    uint32_t menuParamId() const
    {
        return config.kind == MacroEffectKind::Shred ? 16u : 2u;
    }

    uint32_t menuItemCount() const
    {
        return config.kind == MacroEffectKind::Shred
            ? s3g::kMacroShredCircuitCount : s3g::kFractureProcessorCount;
    }

    std::string menuItem(uint32_t index) const
    {
        if (config.kind == MacroEffectKind::Shred) {
            return s3g::macroShredCircuitName(
                static_cast<s3g::MacroShredCircuit>(index));
        }
        return s3g::fractureProcessorName(
            static_cast<s3g::FractureProcessor>(index));
    }

    CRect processorMenuRect() const
    {
        const auto& panel = selectedShredLayout().engine;
        return rect(gui_layout::processorControlX(panel.frame.x),
            gui_layout::rowY(panel, 0u) - 5.0,
            gui_layout::processorMenuWidth(panel.frame.width), 24.0);
    }

    CRect dropdownRect() const
    {
        const auto menu = processorMenuRect();
        const double rowHeight = config.kind == MacroEffectKind::Shred
            ? 18.0 : 17.0;
        return rect(menu.left, menu.top + 22.0, menu.getWidth(),
            rowHeight * menuItemCount());
    }

    void drawDropdown(CDrawContext& context)
    {
        const auto bounds = dropdownRect();
        context.setFillColor(color(0x090909));
        context.drawRect(rect(bounds.left - 2.0, bounds.top - 2.0,
            bounds.getWidth() + 4.0, bounds.getHeight() + 4.0), kDrawFilled);
        context.setFillColor(color(0x1b1b1b));
        context.drawRect(bounds, kDrawFilled);
        context.setFrameColor(color(0x7e7e7e));
        context.drawRect(bounds, kDrawStroked);
        const double rowHeight = bounds.getHeight() / menuItemCount();
        const int selected = std::clamp(static_cast<int>(
            std::lround(param(menuParamId()))), 0,
            static_cast<int>(menuItemCount()) - 1);
        for (uint32_t index = 0u; index < menuItemCount(); ++index) {
            const auto row = rect(bounds.left,
                bounds.top + rowHeight * index, bounds.getWidth(), rowHeight);
            if (static_cast<int>(index) == menuHover) {
                context.setFillColor(color(0x444444));
                context.drawRect(row, kDrawFilled);
            } else if (static_cast<int>(index) == selected) {
                context.setFillColor(color(0x373737));
                context.drawRect(row, kDrawFilled);
            } else if (index % 2u == 1u) {
                context.setFillColor(style.strip);
                context.drawRect(row, kDrawFilled);
            }
            if (static_cast<int>(index) == selected
                || static_cast<int>(index) == menuHover) {
                context.setFillColor(style.fill);
                context.drawRect(rect(row.left + 2.0, row.top + 2.0,
                    3.0, row.getHeight() - 4.0), kDrawFilled);
            }
            text(context, menuItem(index), row.left + 9.0,
                row.top + 3.0, row.getWidth() - 18.0, style.value);
        }
    }

    int dropdownHit(const CPoint& point) const
    {
        const auto bounds = dropdownRect();
        if (!foundation::contains(bounds, point)) return -1;
        const double rowHeight = bounds.getHeight() / menuItemCount();
        return static_cast<int>(std::min<uint32_t>(menuItemCount() - 1u,
            static_cast<uint32_t>((point.y - bounds.top) / rowHeight)));
    }

    bool hitSlider(const CPoint& point, uint32_t clickCount)
    {
        const auto testRows = [&](const gui_layout::Panel& panel,
            const uint32_t* ids, uint32_t count, uint32_t firstRow = 0u) {
            for (uint32_t row = 0u; row < count; ++row) {
                if (contains(gui_layout::sliderHitRect(
                        panel, row + firstRow), point)) {
                    beginSlider(ids[row], point, clickCount);
                    return true;
                }
            }
            return false;
        };
        if (config.kind == MacroEffectKind::Pitch) {
            const auto& family = gui_layout::kMacroFamilyLayout;
            const uint32_t output[] { 10u, 9u };
            const uint32_t engine[] { 1u, 2u, 3u };
            const uint32_t relationships[] { 4u, 5u, 6u, 7u, 8u };
            return testRows(family.output, output, 2u)
                || testRows(family.pitchEngine, engine, 3u)
                || testRows(family.pitchRelationships, relationships, 5u);
        }
        const auto& family = selectedShredLayout();
        const uint32_t output[] { 14u, 13u };
        const uint32_t shredEngine[] { 1u, 2u, 3u, 4u, 5u, 6u, 15u, 7u };
        const uint32_t fractureEngine[] { 1u, 3u, 4u, 5u, 6u, 7u };
        const uint32_t relationships[] { 8u, 9u, 10u, 11u, 12u };
        if (testRows(family.output, output, 2u)) return true;
        if (config.kind == MacroEffectKind::Shred) {
            if (testRows(family.engine, shredEngine, 8u, 1u)) return true;
        } else if (testRows(family.engine, fractureEngine, 6u, 1u)) {
            return true;
        }
        return config.channelCount > 1u
            && testRows(family.relationships, relationships, 5u);
    }

    void beginSlider(uint32_t id, const CPoint& point, uint32_t clickCount)
    {
        if (clickCount >= 2u && config.callbacks.getDefaultValue
            && config.callbacks.setParam) {
            double defaultValue = 0.0;
            if (config.callbacks.getDefaultValue(
                    config.callbacks.context, id, &defaultValue)) {
                parameterEdit.perform(id, defaultValue);
                dragParam = kNoParam;
                invalid();
                return;
            }
        }
        dragParam = id;
        parameterEdit.begin(id);
        updateSlider(point);
    }

    void updateSlider(const CPoint& point)
    {
        if (dragParam == kNoParam || !config.callbacks.setParam) return;
        const gui_layout::Panel* panel = nullptr;
        if (config.kind == MacroEffectKind::Pitch) {
            const auto& family = gui_layout::kMacroFamilyLayout;
            panel = dragParam == 9u || dragParam == 10u ? &family.output
                : (dragParam <= 3u ? &family.pitchEngine
                    : &family.pitchRelationships);
        } else {
            const auto& family = selectedShredLayout();
            panel = dragParam == 13u || dragParam == 14u ? &family.output
                : ((dragParam >= 8u && dragParam <= 12u)
                    ? &family.relationships : &family.engine);
        }
        const double x = gui_layout::processorControlX(panel->frame.x);
        const double width = gui_layout::processorTrackWidth(panel->frame.width);
        const double norm = std::clamp((point.x - x) / width, 0.0, 1.0);
        double value = norm;
        if (config.kind == MacroEffectKind::Pitch) {
            switch (dragParam) {
            case 1u: value = -24.0 + norm * 48.0; break;
            case 2u: value = -100.0 + norm * 200.0; break;
            case 3u: value = 20.0 + norm * 160.0; break;
            case 6u: value = -1.0 + norm * 2.0; break;
            case 8u: value = 10.0 + norm * 1990.0; break;
            case 10u: value = -60.0 + norm * 72.0; break;
            default: break;
            }
        } else {
            switch (dragParam) {
            case 1u: value = -24.0 + norm * 60.0; break;
            case 5u:
                if (config.kind == MacroEffectKind::Fracture)
                    value = -1.0 + norm * 2.0;
                break;
            case 10u: value = -1.0 + norm * 2.0; break;
            case 12u: value = 10.0 + norm * 1990.0; break;
            case 14u: value = -60.0 + norm * 66.0; break;
            default: break;
            }
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

    MacroEffectEditorConfig config;
    const foundation::Palette& style = foundation::palette();
    SharedPointer<CFontDesc> font;
    SharedPointer<CFontDesc> titleFont;
    SharedPointer<CVSTGUITimer> refreshTimer;
    foundation::ParameterEditSession parameterEdit;
    std::string presetName { "INIT" };
    std::array<float, 160u> loopHistory {};
    std::array<float, 160u> reductionHistory {};
    uint32_t historyWrite = 0u;
    uint32_t dragParam = kNoParam;
    bool menuOpen = false;
    int menuHover = -1;
};

} // namespace

class MacroEffectEditor final : public foundation::EditorHost {
public:
    MacroEffectEditor(const MacroEffectEditorConfig& editorConfig,
        uint32_t requestedWidth, uint32_t requestedHeight)
        : EditorHost(editorConfig.nativeWidth, editorConfig.nativeHeight,
            requestedWidth, requestedHeight)
        , config(editorConfig)
    {
    }

    bool build() { return ready() && attach(new MacroEffectView(config)); }

private:
    MacroEffectEditorConfig config;
};

MacroEffectEditor* createMacroEffectEditor(
    const MacroEffectEditorConfig& config, uint32_t width, uint32_t height)
{
    auto* editor = new (std::nothrow) MacroEffectEditor(config, width, height);
    if (!editor) return nullptr;
    if (!editor->build()) {
        delete editor;
        return nullptr;
    }
    return editor;
}

void destroyMacroEffectEditor(MacroEffectEditor* editor) { delete editor; }

bool setMacroEffectEditorParent(MacroEffectEditor* editor, void* nativeParent)
{
    return editor && editor->setParent(nativeParent);
}

bool setMacroEffectEditorSize(
    MacroEffectEditor* editor, uint32_t width, uint32_t height)
{
    return editor && editor->setSize(width, height);
}

bool setMacroEffectEditorVisible(MacroEffectEditor* editor, bool visible)
{
    return editor && editor->setVisible(visible);
}

} // namespace s3g::portable_gui
