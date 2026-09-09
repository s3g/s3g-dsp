#include "s3g_ambi_point_encoder_vstgui.h"

#include "s3g_gui_layout.h"
#include "s3g_vstgui_foundation.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/clinestyle.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <new>
#include <string>

namespace s3g::portable_gui {
namespace {

using namespace VSTGUI;

constexpr uint32_t kPointCount = s3g::kAmbiPointEncoderMaxPoints;
constexpr uint32_t kDefaultPointCount =
    s3g::kAmbiPointEncoderPrototypePoints;
constexpr uint32_t kMixerBankSize = 16u;

constexpr uint32_t kPointParamId = 1u;
constexpr uint32_t kAzimuthParamId = 2u;
constexpr uint32_t kElevationParamId = 3u;
constexpr uint32_t kDistanceParamId = 4u;
constexpr uint32_t kPointGainParamId = 5u;
constexpr uint32_t kMotionModeParamId = 6u;
constexpr uint32_t kMotionAmountParamId = 7u;
constexpr uint32_t kRateParamId = 8u;
constexpr uint32_t kDragParamId = 11u;
constexpr uint32_t kSwirlParamId = 12u;
constexpr uint32_t kOutputParamId = 14u;
constexpr uint32_t kPointMuteParamId = 15u;
constexpr uint32_t kUpperHemisphereParamId = 16u;
constexpr uint32_t kMotionSceneParamId = 17u;
constexpr uint32_t kCollisionParamId = 18u;
constexpr uint32_t kImpactParamId = 19u;
constexpr uint32_t kPhysicsScaleParamId = 20u;
constexpr uint32_t kPoltergeistParamId = 21u;
constexpr uint32_t kPoltergeistRateParamId = 22u;
constexpr uint32_t kPoltergeistReachParamId = 23u;
constexpr uint32_t kPoltergeistChaosParamId = 24u;
constexpr uint32_t kPoltergeistRadiusParamId = 25u;
constexpr uint32_t kDopplerParamId = 26u;
constexpr uint32_t kAirParamId = 27u;
constexpr uint32_t kOrderParamId = 28u;
constexpr uint32_t kActivePointsParamId = 29u;

constexpr uint32_t kPerPointParamBase = 1000u;
constexpr uint32_t kPerPointParamStride = 8u;
constexpr uint32_t kPerPointAzimuth = 0u;
constexpr uint32_t kPerPointElevation = 1u;
constexpr uint32_t kPerPointDistance = 2u;
constexpr uint32_t kPerPointGain = 3u;
constexpr uint32_t kPerPointMute = 4u;
constexpr uint32_t kPerPointSolo = 5u;

constexpr CColor color(uint32_t rgb, uint8_t alpha = 255u)
{
    const auto base = foundation::color(rgb);
    return CColor(base.red, base.green, base.blue, alpha);
}

uint8_t alphaByte(double value)
{
    return static_cast<uint8_t>(std::clamp(value, 0.0, 1.0) * 255.0);
}

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

const CRect kPrimary = rect(18.0, 42.0, 596.0, 656.0);
const CRect kField = rect(34.0, 76.0, 564.0, 606.0);
constexpr double kToolboxX = 630.0;
constexpr double kToolboxWidth = 250.0;
constexpr double kToolboxLabelX = 646.0;
constexpr double kToolboxControlX = 738.0;
constexpr double kToolboxValueX = 826.0;
constexpr double kToolboxTrackWidth = 82.0;
constexpr double kToolboxMenuWidth = 124.0;

struct SliderSpec {
    uint32_t id;
    const char* label;
    double y;
    double minimum;
    double maximum;
};

constexpr std::array<SliderSpec, 21u> kSliders {{
    { kOutputParamId, "OUT", 78.0, -60.0, 12.0 },
    { kActivePointsParamId, "INPUTS", 128.0, 1.0, 64.0 },
    { kPointParamId, "POINT", 153.0, 1.0, 64.0 },
    { kAzimuthParamId, "AZIMUTH", 178.0, -180.0, 180.0 },
    { kElevationParamId, "ELEV", 203.0, -90.0, 90.0 },
    { kDistanceParamId, "DISTANCE", 228.0, 0.15, 2.0 },
    { kPointGainParamId, "GAIN", 250.0, 0.0, 2.0 },
    { kMotionAmountParamId, "AMOUNT", 404.0, 0.0, 1.0 },
    { kPhysicsScaleParamId, "SCALE", 426.0, 0.25, 2.0 },
    { kRateParamId, "RATE", 448.0, 0.005, 0.5 },
    { kCollisionParamId, "COLLISION", 470.0, 0.0, 1.0 },
    { kImpactParamId, "IMPACT", 492.0, 0.0, 1.0 },
    { kDragParamId, "DRAG", 514.0, 0.45, 0.995 },
    { kSwirlParamId, "SWIRL", 536.0, -0.24, 0.24 },
    { kPoltergeistParamId, "POLTER", 558.0, 0.0, 1.0 },
    { kPoltergeistRateParamId, "G-RATE", 580.0, 0.05, 4.0 },
    { kPoltergeistReachParamId, "G-REACH", 602.0, 0.0, 1.0 },
    { kPoltergeistRadiusParamId, "G-RAD", 624.0, 0.04, 1.0 },
    { kPoltergeistChaosParamId, "G-CHAOS", 646.0, 0.0, 1.0 },
    { kDopplerParamId, "DOPPLER", 668.0, 0.0, 1.0 },
    { kAirParamId, "AIR", 690.0, 0.0, 1.0 },
}};

enum class MenuKind : int { None = -1, Order, Mute, Motion, Style, Hemisphere };

struct MenuSpec { MenuKind kind; const char* label; double y; };
constexpr std::array<MenuSpec, 5u> kMenus {{
    { MenuKind::Order, "ORDER", 103.0 },
    { MenuKind::Mute, "MUTE", 272.0 },
    { MenuKind::Motion, "MOTION", 338.0 },
    { MenuKind::Style, "STYLE", 360.0 },
    { MenuKind::Hemisphere, "HEMISPHERE", 382.0 },
}};

constexpr std::array<const char*, 7u> kOrderItems {{
    "1OA", "2OA", "3OA", "4OA", "5OA", "6OA", "7OA",
}};
constexpr std::array<const char*, 2u> kMuteItems {{ "ON", "MUTED" }};
constexpr std::array<const char*, 6u> kMotionItems {{
    "OFF", "DRIFT", "ORBIT", "SWARM", "PULSE", "PHYS",
}};
constexpr std::array<const char*, 2u> kHemisphereItems {{
    "FULL SPHERE", "UPPER HEMI",
}};
constexpr std::array<const char*, 18u> kStyleNames {{
    "MANUAL", "BREEZE", "GLIDE", "BOUNCE", "COLLIDE", "SCATTER",
    "CUSTOM", "ELASTIC", "MEANDER", "CROSSWIND", "WIDE", "TIGHT",
    "FLOCK", "CLOUD", "FRENZY", "BREATHE", "RIPPLE", "THROB",
}};

struct StyleList { const uint32_t* scenes; uint32_t count; };

StyleList styleList(s3g::AmbiPointMotionMode mode)
{
    static constexpr std::array<uint32_t, 1u> off {{ 0u }};
    static constexpr std::array<uint32_t, 4u> drift {{ 1u, 8u, 9u, 6u }};
    static constexpr std::array<uint32_t, 4u> orbit {{ 2u, 10u, 11u, 6u }};
    static constexpr std::array<uint32_t, 4u> swarm {{ 12u, 13u, 14u, 6u }};
    static constexpr std::array<uint32_t, 4u> pulse {{ 15u, 16u, 17u, 6u }};
    static constexpr std::array<uint32_t, 5u> phys {{ 3u, 4u, 5u, 7u, 6u }};
    switch (mode) {
    case s3g::AmbiPointMotionMode::Drift: return { drift.data(), 4u };
    case s3g::AmbiPointMotionMode::Orbit: return { orbit.data(), 4u };
    case s3g::AmbiPointMotionMode::Swarm: return { swarm.data(), 4u };
    case s3g::AmbiPointMotionMode::Pulse: return { pulse.data(), 4u };
    case s3g::AmbiPointMotionMode::Phys: return { phys.data(), 5u };
    default: return { off.data(), 1u };
    }
}

int styleIndex(s3g::AmbiPointMotionMode mode, uint32_t scene)
{
    const auto list = styleList(mode);
    for (uint32_t i = 0u; i < list.count; ++i)
        if (list.scenes[i] == scene) return static_cast<int>(i);
    return -1;
}

uint32_t perPointParamId(uint32_t point, uint32_t kind)
{
    return kPerPointParamBase + point * kPerPointParamStride + kind;
}

float linearToSrgb(float value)
{
    const float x = std::clamp(value, 0.0f, 1.0f);
    return x <= 0.0031308f ? x * 12.92f
        : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

CColor pointColor(const s3g::AmbiPoint& point, bool selected, bool audible)
{
    if (!audible) return color(0x404848, alphaByte(0.45));
    const float hue = std::fmod((point.azimuthDeg / 360.0f) + 1.0f, 1.0f);
    const float light = std::clamp((std::clamp(point.elevationDeg,
        -90.0f, 90.0f) + 90.0f) / 180.0f, 0.28f, 0.88f);
    const float chroma = std::clamp(point.distance / 2.4f,
        0.08f, 1.0f) * 0.37f;
    constexpr float pi = 3.14159265358979323846f;
    const float a = std::cos(hue * 2.0f * pi) * chroma;
    const float b = std::sin(hue * 2.0f * pi) * chroma;
    const float l3 = light + 0.3963377774f * a + 0.2158037573f * b;
    const float m3 = light - 0.1055613458f * a - 0.0638541728f * b;
    const float s3 = light - 0.0894841775f * a - 1.2914855480f * b;
    const float l = l3 * l3 * l3;
    const float m = m3 * m3 * m3;
    const float s = s3 * s3 * s3;
    float r = linearToSrgb(4.0767416621f * l - 3.3077115913f * m
        + 0.2309699292f * s);
    float g = linearToSrgb(-1.2684380046f * l + 2.6097574011f * m
        - 0.3413193965f * s);
    float blue = linearToSrgb(-0.0041960863f * l - 0.7034186147f * m
        + 1.7076147010f * s);
    const float grayMix = selected ? 0.08f : 0.18f;
    r = r * (1.0f - grayMix) + 0.74f * grayMix;
    g = g * (1.0f - grayMix) + 0.74f * grayMix;
    blue = blue * (1.0f - grayMix) + 0.74f * grayMix;
    return CColor(static_cast<uint8_t>(r * 255.0f),
        static_cast<uint8_t>(g * 255.0f),
        static_cast<uint8_t>(blue * 255.0f),
        selected ? 255u : alphaByte(0.88));
}

class AmbiPointView final : public foundation::ContentView {
public:
    explicit AmbiPointView(const AmbiPointEditorConfig& editorConfig)
        : ContentView(rect(0.0, 0.0, editorConfig.nativeWidth,
            editorConfig.nativeHeight)), config(editorConfig)
    {
        const auto& metrics = foundation::fontMetrics();
        font = foundation::makeUiFont(metrics.body);
        titleFont = foundation::makeUiFont(metrics.title);
        tinyFont = foundation::makeUiFont(metrics.tiny);
        const foundation::ParameterEditCallbacks callbacks {
            config.callbacks.context, config.callbacks.beginParamEdit,
            config.callbacks.setParam, config.callbacks.endParamEdit
        };
        parameterEdit.setCallbacks(callbacks);
        for (auto& edit : pointEdits) edit.setCallbacks(callbacks);
        refreshSnapshot();
        viewMode = snapshot.viewMode;
        viewAzimuth = snapshot.viewAzimuthDeg;
        viewElevation = snapshot.viewElevationDeg;
        viewZoom = snapshot.viewZoom;
        normalizePresetCamera();
        mixerBank = snapshot.params.selectedPoint / kMixerBankSize;
    }

    void startRefresh() override
    {
        if (refreshTimer) return;
        refreshTimer = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) {
            refreshSnapshot();
            if (isVisible()) invalid();
        }, 33u);
    }

    void stopRefresh() override { refreshTimer = nullptr; }

    void draw(CDrawContext* context) override
    {
        if (!context) return;
        refreshSnapshot();
        context->setDrawMode(kAntiAliasing);
        context->setFillColor(style.background);
        context->drawRect(getViewSize(), kDrawFilled);
        drawTitleBand(*context);
        if (leftPage == 0) drawField(*context);
        else drawMixer(*context);
        drawPanel(*context, rect(kToolboxX, 42.0, kToolboxWidth, 248.0),
            "OUTPUT / INPUT");
        drawPanel(*context, rect(kToolboxX, 302.0, kToolboxWidth, 406.0),
            "MOTION");
        for (const auto& slider : kSliders) drawSlider(*context, slider);
        for (const auto& menu : kMenus) drawMenu(*context, menu);
        if (activeMenu != MenuKind::None) drawDropdown(*context);
        setDirty(false);
    }

    void onMouseDownEvent(MouseDownEvent& event) override
    {
        if (!event.buttonState.isLeft()) return;
        const CPoint point = event.mousePosition;
        if (activeMenu != MenuKind::None) {
            const int hit = dropdownHit(point);
            const MenuKind menu = activeMenu;
            closeMenu();
            if (hit >= 0) applyMenu(menu, static_cast<uint32_t>(hit));
            event.consumed = true;
            return;
        }

        const auto band = gui_layout::encoderTitleBand(
            { static_cast<double>(config.nativeWidth),
              static_cast<double>(config.nativeHeight) });
        if (contains(rect(band.presetMenu), point)) {
            if (config.callbacks.resetToDefaults)
                config.callbacks.resetToDefaults(config.callbacks.context);
            presetName = "INIT";
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
        if (contains(rect(band.randomButton), point)) {
            if (config.callbacks.randomize)
                config.callbacks.randomize(config.callbacks.context);
            presetName = "RANDOM";
            invalid();
            event.consumed = true;
            return;
        }

        for (int page = 0; page < 2; ++page) {
            if (contains(pageButton(page), point)) {
                leftPage = page;
                if (page == 1)
                    mixerBank = snapshot.params.selectedPoint / kMixerBankSize;
                stopDragging();
                invalid();
                event.consumed = true;
                return;
            }
        }

        if (leftPage == 0) {
            for (int i = 0; i < 2; ++i) {
                if (contains(zoomButton(i), point)) {
                    viewZoom = std::clamp(viewZoom
                        + (i == 0 ? -0.15 : 0.15), 0.55, 2.20);
                    storeViewState();
                    invalid();
                    event.consumed = true;
                    return;
                }
            }
            for (int i = 0; i < 3; ++i) {
                if (contains(viewButton(i), point)) {
                    setViewPreset(i);
                    event.consumed = true;
                    return;
                }
            }
            if (contains(kField, point)) {
                const int hit = hitPoint(point);
                if (hit >= 0) {
                    setParam(kPointParamId, static_cast<double>(hit + 1));
                    mixerBank = static_cast<uint32_t>(hit) / kMixerBankSize;
                    hasPointSelection = true;
                    if (viewMode == 0 || viewMode == 1) {
                        dragPoint = hit;
                        beginPointPositionEdit(static_cast<uint32_t>(hit));
                        updateDraggedPoint(point);
                    }
                } else {
                    hasPointSelection = false;
                    dragView = true;
                    lastDrag = point;
                }
                invalid();
                event.consumed = true;
                return;
            }
        } else if (handleMixerMouseDown(point, event.clickCount)) {
            event.consumed = true;
            return;
        }

        for (const auto& menu : kMenus) {
            if (contains(menuRect(menu), point)) {
                activeMenu = menu.kind;
                menuHover = -1;
                updateMenuBounds(menu.y);
                invalid();
                event.consumed = true;
                return;
            }
        }
        for (const auto& slider : kSliders) {
            if (!contains(sliderHitRect(slider), point)) continue;
            if (event.clickCount >= 2u && config.callbacks.getDefaultValue) {
                double value = 0.0;
                uint32_t lookupId = slider.id;
                if (slider.id == kAzimuthParamId)
                    lookupId = perPointParamId(snapshot.params.selectedPoint,
                        kPerPointAzimuth);
                else if (slider.id == kElevationParamId)
                    lookupId = perPointParamId(snapshot.params.selectedPoint,
                        kPerPointElevation);
                else if (slider.id == kDistanceParamId)
                    lookupId = perPointParamId(snapshot.params.selectedPoint,
                        kPerPointDistance);
                else if (slider.id == kPointGainParamId)
                    lookupId = perPointParamId(snapshot.params.selectedPoint,
                        kPerPointGain);
                if (config.callbacks.getDefaultValue(config.callbacks.context,
                        lookupId, &value)) setParam(slider.id, value);
            } else {
                dragParam = slider.id;
                parameterEdit.begin(slider.id);
                updateSlider(slider, point);
            }
            if (slider.id == kPointParamId
                || slider.id == kActivePointsParamId)
                mixerBank = snapshot.params.selectedPoint / kMixerBankSize;
            hasPointSelection = true;
            invalid();
            event.consumed = true;
            return;
        }
    }

    void onMouseMoveEvent(MouseMoveEvent& event) override
    {
        if (event.buttonState.has(MouseButton::Left)) {
            if (dragPoint >= 0) updateDraggedPoint(event.mousePosition);
            else if (dragMixerPoint >= 0)
                updateMixerGain(event.mousePosition);
            else if (dragMixerOutput)
                updateMixerOutput(event.mousePosition);
            else if (dragView) {
                viewAzimuth += (event.mousePosition.x - lastDrag.x) * 0.35;
                viewElevation = std::clamp(viewElevation
                    + (event.mousePosition.y - lastDrag.y) * 0.35,
                    -85.0, 85.0);
                viewMode = -1;
                lastDrag = event.mousePosition;
                storeViewState();
                invalid();
            } else if (dragParam != 0u) {
                if (const auto* slider = sliderForId(dragParam))
                    updateSlider(*slider, event.mousePosition);
            }
            event.consumed = true;
            return;
        }
        if (activeMenu != MenuKind::None) {
            const int hover = dropdownHit(event.mousePosition);
            if (hover != menuHover) {
                menuHover = hover;
                invalid();
            }
        }
    }

    void onMouseUpEvent(MouseUpEvent& event) override
    {
        if (!isDragging()) return;
        stopDragging();
        event.consumed = true;
    }

    void onMouseCancelEvent(MouseCancelEvent& event) override
    {
        if (!isDragging()) return;
        stopDragging();
        event.consumed = true;
    }

private:
    void refreshSnapshot()
    {
        if (config.callbacks.getSnapshot)
            config.callbacks.getSnapshot(config.callbacks.context, &snapshot);
    }

    double param(uint32_t id) const
    {
        return config.callbacks.getParam
            ? config.callbacks.getParam(config.callbacks.context, id) : 0.0;
    }

    void setParam(uint32_t id, double value)
    {
        if (parameterEdit.active(id)) parameterEdit.set(value);
        else parameterEdit.perform(id, value);
        refreshSnapshot();
    }

    void setPointParam(uint32_t point, uint32_t kind, double value)
    {
        if (kind >= pointEdits.size()) return;
        const uint32_t id = perPointParamId(point, kind);
        auto& edit = pointEdits[kind];
        if (edit.active(id)) edit.set(value);
        else edit.perform(id, value);
        refreshSnapshot();
    }

    void text(CDrawContext& context, const std::string& value,
              double x, double y, double width, CColor textColor,
              CHoriTxtAlign align = kLeftText, CFontRef useFont = nullptr)
    {
        if (!useFont)
            useFont = font.get ();
        foundation::drawTextLine(context, value, x, y, width, textColor,
            useFont, align);
    }

    void textInRect(CDrawContext& context, const std::string& value,
                    CRect bounds, CColor textColor,
                    CHoriTxtAlign align = kCenterText,
                    CFontRef useFont = nullptr)
    {
        if (!useFont)
            useFont = font.get ();
        foundation::drawTextInRect(context, value, bounds, textColor,
            useFont, align);
    }

    void drawPanel(CDrawContext& context, const CRect& bounds,
                   const char* title)
    {
        foundation::drawPanel(context, bounds, title, font);
    }

    void drawButton(CDrawContext& context, const CRect& bounds,
                    const std::string& label, bool active)
    {
        foundation::drawButton(context, bounds, label, font, active);
    }

    void drawTitleBand(CDrawContext& context)
    {
        const auto band = gui_layout::encoderTitleBand(
            { static_cast<double>(config.nativeWidth),
              static_cast<double>(config.nativeHeight) });
        foundation::drawPluginTitle(context, config.pluginName ? config.pluginName : "",
            rect(band.titleX, band.titleY - 2.0,
                band.presetLabelX - band.titleX - 8.0, 15.0), titleFont);
        text(context, "PRESET", band.presetLabelX, band.controlY + 1.0,
            band.presetMenu.x - band.presetLabelX - 4.0, style.label);
        const auto preset = rect(band.presetMenu);
        foundation::drawMenuBox(context, preset, presetName, font);
        drawButton(context, rect(band.loadButton), "LOAD", false);
        drawButton(context, rect(band.saveButton), "SAVE", false);
        drawButton(context, rect(band.randomButton), "RANDOM", false);

        char status[64] {};
        const float peak = snapshot.outputPeak;
        std::snprintf(status, sizeof(status), "%uPT > %uOA  ·  PK %+.1f",
            snapshot.params.activePoints, snapshot.params.order,
            20.0 * std::log10(std::max(0.000001f, peak)));
        context.setFont(font);
        const double width = context.getStringWidth(status);
        text(context, status, config.nativeWidth - width
            - band.statusRightInset, band.titleY, width + 1.0, style.value);
    }

    CRect pageButton(int index) const
    {
        return rect(kPrimary.left + 104.0 + index * 53.0,
            kPrimary.top + 4.0, 48.0, 13.0);
    }

    CRect viewButton(int index) const
    {
        constexpr double width = 38.0;
        constexpr double gap = 5.0;
        const double x = kPrimary.right - 10.0
            - (3.0 - index) * width - (2.0 - index) * gap;
        return rect(x, kPrimary.top + 4.0, width, 13.0);
    }

    CRect zoomButton(int index) const
    {
        const double start = viewButton(0).left;
        return rect(start - 12.0 - (2.0 - index) * 18.0
            - (1.0 - index) * 4.0, kPrimary.top + 4.0, 18.0, 13.0);
    }

    CRect mixerBankButton(uint32_t index) const
    {
        constexpr double width = 42.0;
        constexpr double gap = 4.0;
        constexpr double total = 4.0 * width + 3.0 * gap;
        return rect(kPrimary.right - 10.0 - total + index * (width + gap),
            kPrimary.top + 4.0, width, 13.0);
    }

    void drawHeaderButtons(CDrawContext& context, bool field)
    {
        drawButton(context, pageButton(0), "FIELD", leftPage == 0);
        drawButton(context, pageButton(1), "MIXER", leftPage == 1);
        if (field) {
            drawButton(context, zoomButton(0), "-", false);
            drawButton(context, zoomButton(1), "+", false);
            drawButton(context, viewButton(0), "TOP", viewMode == 0);
            drawButton(context, viewButton(1), "SIDE", viewMode == 1);
            drawButton(context, viewButton(2), "3/4", viewMode == 2);
        }
    }

    void drawField(CDrawContext& context)
    {
        drawPanel(context, kPrimary, "POINT FIELD");
        drawHeaderButtons(context, true);
        // Calibrated to the measured sRGB output of Cocoa's 0x111111 fill.
        context.setFillColor(color(0x151515));
        context.drawRect(kField, kDrawFilled);
        context.setFrameColor(style.grid);
        context.setLineWidth(1.0);
        context.drawRect(kField, kDrawStroked);

        const double cx = kField.left + kField.getWidth() * 0.5;
        const double cy = kField.top + kField.getHeight() * 0.54;
        const double scale = viewScale();
        context.setFillColor(color(0x101010));
        context.setFrameColor(color(0x454545));
        context.drawEllipse(rect(cx - scale, cy - scale,
            scale * 2.0, scale * 2.0), kDrawFilledAndStroked);
        drawOrientationGuides(context);
        drawPoltergeist(context);

        const uint32_t active = std::clamp<uint32_t>(
            snapshot.params.activePoints, 1u, kPointCount);
        std::array<CPoint, kPointCount> projected {};
        std::array<double, kPointCount> depths {};
        std::array<s3g::Vec3, kPointCount> positions {};
        std::array<uint32_t, kPointCount> order {};
        std::array<std::array<bool, kPointCount>, kPointCount> edges {};
        for (uint32_t i = 0u; i < active; ++i) {
            order[i] = i;
            const auto& point = snapshot.animatedPoints[i];
            const auto direction = s3g::directionFromAed(
                point.azimuthDeg, point.elevationDeg);
            positions[i] = { direction.x * point.distance,
                direction.y * point.distance, direction.z * point.distance };
            projected[i] = project(positions[i], &depths[i]);
        }
        std::sort(order.begin(), order.begin() + active,
            [&](uint32_t a, uint32_t b) { return depths[a] < depths[b]; });

        for (uint32_t i = 0u; i < active; ++i) {
            if (!snapshot.animatedPoints[i].enabled) continue;
            std::array<std::pair<float, int>, 2u> nearest {{
                { 999999.0f, -1 }, { 999999.0f, -1 },
            }};
            for (uint32_t j = 0u; j < active; ++j) {
                if (i == j || !snapshot.animatedPoints[j].enabled) continue;
                const float dx = positions[i].x - positions[j].x;
                const float dy = positions[i].y - positions[j].y;
                const float dz = positions[i].z - positions[j].z;
                const float distance = dx * dx + dy * dy + dz * dz;
                if (distance < nearest[0].first) {
                    nearest[1] = nearest[0];
                    nearest[0] = { distance, static_cast<int>(j) };
                } else if (distance < nearest[1].first) {
                    nearest[1] = { distance, static_cast<int>(j) };
                }
            }
            for (const auto& neighbor : nearest) {
                if (neighbor.second < 0) continue;
                const uint32_t a = std::min(i,
                    static_cast<uint32_t>(neighbor.second));
                const uint32_t b = std::max(i,
                    static_cast<uint32_t>(neighbor.second));
                edges[a][b] = true;
            }
        }
        for (uint32_t a = 0u; a < active; ++a) {
            for (uint32_t b = a + 1u; b < active; ++b) {
                if (!edges[a][b]) continue;
                const double release = std::clamp(std::max(
                    snapshot.bondRelease[a], snapshot.bondRelease[b])
                    / 2.0f, 0.0f, 1.0f);
                if (release > 0.64) continue;
                const double energy = std::clamp(std::max(
                    snapshot.collisionEnergy[a],
                    snapshot.collisionEnergy[b]), 0.0f, 1.0f);
                const double heal = 1.0 - release;
                context.setFrameColor(color(energy > 0.02
                    ? 0xb8b8b8 : 0x6a6a6a,
                    alphaByte((0.48 + energy * 0.32) * heal)));
                const bool selected = hasPointSelection
                    && (a == snapshot.params.selectedPoint
                        || b == snapshot.params.selectedPoint);
                context.setLineWidth(((selected ? 1.35 : 0.85)
                    + energy * 0.85) * std::max(0.45, heal));
                context.drawLine(projected[a], projected[b]);
            }
        }

        for (uint32_t sorted = 0u; sorted < active; ++sorted) {
            const uint32_t i = order[sorted];
            const auto& point = snapshot.animatedPoints[i];
            if (!point.enabled) continue;
            const bool selected = hasPointSelection
                && i == snapshot.params.selectedPoint;
            const double depthNorm = std::clamp((depths[i] + 2.0) / 4.0,
                0.0, 1.0);
            const double energy = std::clamp(
                static_cast<double>(snapshot.collisionEnergy[i]),
                0.0, 1.0);
            const double release = std::clamp(
                static_cast<double>(snapshot.bondRelease[i]) / 2.0,
                0.0, 1.0);
            const double radius = selected ? 6.0 : 3.0 + depthNorm * 1.2;
            context.setFillColor(pointColor(point, selected, true));
            context.drawRect(rect(projected[i].x - radius,
                projected[i].y - radius, radius * 2.0, radius * 2.0),
                kDrawFilled);
            if (energy > 0.02) {
                context.setFrameColor(color(0xf0f0f0, alphaByte(
                    0.25 + energy * 0.45)));
                context.setLineStyle(kLineSolid);
                context.setLineWidth(1.0);
                context.drawRect(rect(projected[i].x - radius - 3.0,
                    projected[i].y - radius - 3.0,
                    radius * 2.0 + 6.0, radius * 2.0 + 6.0),
                    kDrawStroked);
            }
            if (release > 0.02) {
                context.setFrameColor(color(0xf0f0f0, alphaByte(
                    0.12 + release * 0.36)));
                context.setLineStyle(kLineOnOffDash);
                context.setLineWidth(1.0);
                context.drawRect(rect(projected[i].x - radius - 5.0,
                    projected[i].y - radius - 5.0,
                    radius * 2.0 + 10.0, radius * 2.0 + 10.0),
                    kDrawStroked);
                context.setLineStyle(kLineSolid);
            }
            if (selected) {
                context.setFrameColor(color(0xf2f2f2));
                context.setLineWidth(1.0);
                context.drawRect(rect(projected[i].x - 10.0,
                    projected[i].y - 10.0, 20.0, 20.0), kDrawStroked);
            }
            char label[8] {};
            std::snprintf(label, sizeof(label), "%u", i + 1u);
            textInRect(context, label,
                rect(projected[i].x - 8.0, projected[i].y - 7.0,
                    16.0, 14.0), selected ? color(0xc8c8c8)
                        : color(0x151515), kCenterText, tinyFont);
        }

        char readout[128] {};
        if (hasPointSelection) {
            std::snprintf(readout, sizeof(readout),
                "P%u  AZ %+.1f  EL %+.1f  DST %.2f  %s",
                snapshot.params.selectedPoint + 1u,
                snapshot.params.selectedAzimuthDeg,
                snapshot.params.selectedElevationDeg,
                snapshot.params.selectedDistance,
                snapshot.params.selectedEnabled ? "ON" : "MUT");
        } else {
            std::snprintf(readout, sizeof(readout), "%s", "NO POINT");
        }
        text(context, readout, kField.left + 10.0,
            kField.bottom - 24.0, kField.getWidth() - 20.0, style.value);
    }

    void drawOrientationGuides(CDrawContext& context)
    {
        constexpr float tau = 6.28318530717958647692f;
        auto circle = owned(context.createGraphicsPath());
        if (circle) {
            for (uint32_t segment = 0u; segment <= 96u; ++segment) {
                const float phase = static_cast<float>(segment) / 96.0f
                    * tau;
                const auto point = project({ std::cos(phase),
                    std::sin(phase), 0.0f }, nullptr);
                if (segment == 0u) circle->beginSubpath(point);
                else circle->addLine(point);
            }
            context.setFrameColor(CColor(26u, 235u, 77u,
                alphaByte(0.24)));
            context.setLineWidth(0.70);
            context.drawGraphicsPath(circle, CDrawContext::kPathStroked);
        }
        struct Axis { s3g::Vec3 from; s3g::Vec3 to; double alpha; };
        constexpr std::array<Axis, 3u> axes {{
            { { -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 0.44 },
            { { 0.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 0.44 },
            { { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 1.0f }, 0.52 },
        }};
        for (const auto& axis : axes) {
            const auto from = project(axis.from, nullptr);
            const auto to = project(axis.to, nullptr);
            if (std::hypot(to.x - from.x, to.y - from.y) < 0.5) continue;
            context.setFrameColor(CColor(26u, 235u, 77u,
                alphaByte(axis.alpha)));
            context.setLineWidth(0.80);
            context.drawLine(from, to);
        }
    }

    void drawPoltergeist(CDrawContext& context)
    {
        const auto& params = snapshot.params;
        if (params.motionMode != s3g::AmbiPointMotionMode::Phys
            || params.motionAmount <= 0.0001f
            || params.poltergeist <= 0.0001f) return;

        const auto& source = snapshot.perturbationSource;
        const auto& previous = snapshot.previousPerturbationSource;
        const float sourceDistance = std::sqrt(source.x * source.x
            + source.y * source.y + source.z * source.z);
        if (sourceDistance <= 0.0001f) return;

        const CPoint sourcePoint = project(source, nullptr);
        const float previousDistance = std::sqrt(previous.x * previous.x
            + previous.y * previous.y + previous.z * previous.z);
        if (previousDistance > 0.0001f) {
            const CPoint previousPoint = project(previous, nullptr);
            context.setFrameColor(color(0xd8d8d8, alphaByte(
                0.14 + params.poltergeist * 0.32)));
            context.setLineWidth(1.5);
            context.drawLine(previousPoint, sourcePoint);
        }

        const float influenceRadius = 0.16f
            + params.poltergeistRadius * 0.78f;
        context.setFrameColor(color(0xd8d8d8, alphaByte(
            0.12 + params.poltergeist * 0.18)));
        context.setLineWidth(1.05);
        constexpr float tau = 6.28318530717958647692f;
        for (int plane = 0; plane < 3; ++plane) {
            auto ring = owned(context.createGraphicsPath());
            if (!ring) continue;
            bool drawing = false;
            for (uint32_t segment = 0u; segment <= 72u; ++segment) {
                const float phase = static_cast<float>(segment) / 72.0f
                    * tau;
                const float cosine = std::cos(phase) * influenceRadius;
                const float sine = std::sin(phase) * influenceRadius;
                s3g::Vec3 point = source;
                if (plane == 0) {
                    point.x += cosine;
                    point.y += sine;
                } else if (plane == 1) {
                    point.x += cosine;
                    point.z += sine;
                } else {
                    point.y += cosine;
                    point.z += sine;
                }
                if (params.upperHemisphereOnly && point.z < 0.0f) {
                    drawing = false;
                    continue;
                }
                const CPoint projected = project(point, nullptr);
                if (!drawing) {
                    ring->beginSubpath(projected);
                    drawing = true;
                } else {
                    ring->addLine(projected);
                }
            }
            context.drawGraphicsPath(ring, CDrawContext::kPathStroked);
        }

        context.setFrameColor(color(0xd8d8d8, alphaByte(
            0.18 + params.poltergeist * 0.40)));
        context.setLineWidth(1.0);
        context.drawLine(CPoint(sourcePoint.x - 6.0, sourcePoint.y),
            CPoint(sourcePoint.x + 6.0, sourcePoint.y));
        context.drawLine(CPoint(sourcePoint.x, sourcePoint.y - 6.0),
            CPoint(sourcePoint.x, sourcePoint.y + 6.0));
        context.drawRect(rect(sourcePoint.x - 4.0, sourcePoint.y - 4.0,
            8.0, 8.0), kDrawStroked);
    }

    CRect mixerGainRect(uint32_t index) const
    {
        const double x = kPrimary.left + 12.0
            + static_cast<double>(index % kMixerBankSize) * 34.0;
        return rect(x + 8.0, kPrimary.top + 128.0, 12.0,
            kPrimary.getHeight() - 196.0);
    }

    CRect mixerMuteRect(uint32_t index) const
    {
        const double x = kPrimary.left + 12.0
            + static_cast<double>(index % kMixerBankSize) * 34.0;
        return rect(x + 1.0, kPrimary.bottom - 42.0, 14.0, 14.0);
    }

    CRect mixerSoloRect(uint32_t index) const
    {
        const double x = kPrimary.left + 12.0
            + static_cast<double>(index % kMixerBankSize) * 34.0;
        return rect(x + 17.0, kPrimary.bottom - 42.0, 14.0, 14.0);
    }

    CRect mixerOutputTrack() const
    {
        return rect(kPrimary.left + 92.0, kPrimary.top + 75.0,
            kPrimary.getWidth() - 182.0, 9.0);
    }

    void drawMixer(CDrawContext& context)
    {
        drawPanel(context, kPrimary, "POINT MIXER");
        drawHeaderButtons(context, false);
        const uint32_t active = std::clamp<uint32_t>(
            snapshot.params.activePoints, 1u, kPointCount);
        const uint32_t bankCount = std::max<uint32_t>(1u,
            (active + kMixerBankSize - 1u) / kMixerBankSize);
        mixerBank = std::min(mixerBank, bankCount - 1u);
        for (uint32_t bank = 0u; bank < bankCount; ++bank) {
            char label[24] {};
            std::snprintf(label, sizeof(label), "%u-%u",
                bank * kMixerBankSize + 1u,
                std::min((bank + 1u) * kMixerBankSize, active));
            drawButton(context, mixerBankButton(bank), label,
                bank == mixerBank);
        }

        const auto outputTrack = mixerOutputTrack();
        text(context, "OUT", kPrimary.left + 12.0,
            kPrimary.top + 70.0, 70.0, style.label);
        drawTrack(context, outputTrack,
            (snapshot.params.outputGainDb + 60.0) / 72.0);
        char output[32] {};
        std::snprintf(output, sizeof(output), "%+.1f dB",
            snapshot.params.outputGainDb);
        text(context, output, kPrimary.right - 70.0,
            kPrimary.top + 70.0, 58.0, style.value, kRightText);

        bool anySolo = false;
        for (uint32_t i = 0u; i < active; ++i)
            anySolo = anySolo || snapshot.editPoints[i].solo;
        const uint32_t first = mixerBank * kMixerBankSize;
        const uint32_t end = std::min(first + kMixerBankSize, active);
        for (uint32_t i = first; i < end; ++i) {
            const auto& point = snapshot.editPoints[i];
            const double laneX = kPrimary.left + 12.0
                + static_cast<double>(i - first) * 34.0;
            const bool selected = hasPointSelection
                && i == snapshot.params.selectedPoint;
            const bool audible = point.enabled && (!anySolo || point.solo);
            if (selected) {
                context.setFillColor(color(0x242424));
                context.drawRect(rect(laneX - 2.0, kPrimary.top + 96.0,
                    31.0, kPrimary.getHeight() - 102.0), kDrawFilled);
            }
            char label[8] {};
            std::snprintf(label, sizeof(label), "%u", i + 1u);
            textInRect(context, label,
                rect(laneX, kPrimary.top + 96.0, 28.0, 18.0), style.value);
            const auto track = mixerGainRect(i);
            context.setFillColor(style.strip);
            context.drawRect(track, kDrawFilled);
            const double normalized = std::clamp(
                static_cast<double>(point.gain) / 2.0, 0.0, 1.0);
            auto fill = rect(track.left + 2.0,
                track.top + 2.0 + (track.getHeight() - 4.0)
                    * (1.0 - normalized),
                track.getWidth() - 4.0,
                std::max(1.0, (track.getHeight() - 4.0) * normalized));
            context.setFillColor(pointColor(point, selected, audible));
            context.drawRect(fill, kDrawFilled);
            context.setFillColor(selected ? color(0xf2f2f2) : style.fill);
            context.drawRect(rect(track.left - 2.0,
                track.top + track.getHeight() * (1.0 - normalized) - 1.0,
                track.getWidth() + 4.0, 3.0), kDrawFilled);
            drawButton(context, mixerMuteRect(i), "M", !point.enabled);
            drawButton(context, mixerSoloRect(i), "S", point.solo);
        }
    }

    CRect menuRect(const MenuSpec& menu) const
    {
        return rect(kToolboxControlX, menu.y - 1.0,
            kToolboxMenuWidth, 15.0);
    }

    CRect sliderTrackRect(const SliderSpec& slider) const
    {
        return rect(kToolboxControlX, slider.y + 1.0,
            kToolboxTrackWidth, 9.0);
    }

    CRect sliderHitRect(const SliderSpec& slider) const
    {
        return rect(kToolboxX + 8.0, slider.y - 8.0,
            kToolboxWidth - 16.0, 24.0);
    }

    double sliderNormalized(const SliderSpec& slider) const
    {
        double minimum = slider.minimum;
        if (slider.id == kElevationParamId
            && snapshot.params.upperHemisphereOnly) minimum = 0.0;
        double maximum = slider.maximum;
        if (slider.id == kPointParamId)
            maximum = static_cast<double>(std::max<uint32_t>(1u,
                snapshot.params.activePoints));
        return std::clamp((param(slider.id) - minimum)
            / std::max(0.000001, maximum - minimum), 0.0, 1.0);
    }

    std::string paramText(uint32_t id) const
    {
        char buffer[64] {};
        const double value = param(id);
        switch (id) {
        case kOutputParamId:
            if (std::abs(value - std::round(value)) < 0.05)
                std::snprintf(buffer, sizeof(buffer), "%+.0f dB", value);
            else std::snprintf(buffer, sizeof(buffer), "%+.1f dB", value);
            break;
        case kActivePointsParamId:
        case kPointParamId:
            std::snprintf(buffer, sizeof(buffer), "%.0f", value); break;
        case kAzimuthParamId:
        case kElevationParamId:
            std::snprintf(buffer, sizeof(buffer), "%+.0f", value); break;
        case kDistanceParamId:
        case kPointGainParamId:
        case kPhysicsScaleParamId:
        case kDragParamId:
        case kPoltergeistRadiusParamId:
            std::snprintf(buffer, sizeof(buffer), "%.2f", value); break;
        case kRateParamId:
            std::snprintf(buffer, sizeof(buffer), "%.3f", value); break;
        case kSwirlParamId:
            std::snprintf(buffer, sizeof(buffer), "%+.2f", value); break;
        case kPoltergeistRateParamId:
            std::snprintf(buffer, sizeof(buffer), "%.2fx", value); break;
        case kMotionAmountParamId:
        case kCollisionParamId:
        case kImpactParamId:
        case kPoltergeistParamId:
        case kPoltergeistReachParamId:
        case kPoltergeistChaosParamId:
        case kDopplerParamId:
        case kAirParamId:
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", value * 100.0);
            break;
        default:
            if (config.callbacks.getParamText
                && config.callbacks.getParamText(config.callbacks.context,
                    id, value, buffer,
                    static_cast<uint32_t>(sizeof(buffer)))) break;
            std::snprintf(buffer, sizeof(buffer), "%.3g", value);
            break;
        }
        return buffer;
    }

    void drawTrack(CDrawContext& context, const CRect& track,
                   double normalized)
    {
        foundation::drawHorizontalSlider(context, track, normalized,
            track.top - 2.0, track.getHeight() + 4.0);
    }

    void drawSlider(CDrawContext& context, const SliderSpec& slider)
    {
        text(context, slider.label, kToolboxLabelX, slider.y - 2.0,
            kToolboxControlX - kToolboxLabelX - 4.0, style.label);
        drawTrack(context, sliderTrackRect(slider),
            sliderNormalized(slider));
        text(context, paramText(slider.id), kToolboxValueX,
            slider.y - 2.0, 38.0, style.value, kRightText);
    }

    std::string menuValue(MenuKind kind) const
    {
        switch (kind) {
        case MenuKind::Order:
            return kOrderItems[std::clamp<uint32_t>(snapshot.params.order,
                1u, 7u) - 1u];
        case MenuKind::Mute:
            return snapshot.params.selectedEnabled ? "ON" : "MUTED";
        case MenuKind::Motion:
            return kMotionItems[std::min<uint32_t>(5u,
                static_cast<uint32_t>(snapshot.params.motionMode))];
        case MenuKind::Style: {
            const int index = styleIndex(snapshot.params.motionMode,
                snapshot.params.motionScene);
            return index >= 0 ? kStyleNames[std::min<uint32_t>(17u,
                snapshot.params.motionScene)] : "CUSTOM";
        }
        case MenuKind::Hemisphere:
            return snapshot.params.upperHemisphereOnly
                ? "UPPER HEMI" : "FULL SPHERE";
        default: return {};
        }
    }

    void drawMenu(CDrawContext& context, const MenuSpec& menu)
    {
        text(context, menu.label, kToolboxLabelX, menu.y - 2.0,
            kToolboxControlX - kToolboxLabelX - 4.0, style.label);
        const auto bounds = menuRect(menu);
        foundation::drawMenuBox(
            context, bounds, menuValue(menu.kind), font);
    }

    std::pair<const char* const*, uint32_t> menuItems(MenuKind kind) const
    {
        switch (kind) {
        case MenuKind::Order: return { kOrderItems.data(), 7u };
        case MenuKind::Mute: return { kMuteItems.data(), 2u };
        case MenuKind::Motion: return { kMotionItems.data(), 6u };
        case MenuKind::Hemisphere: return { kHemisphereItems.data(), 2u };
        case MenuKind::Style: {
            styleMenuItems.fill(nullptr);
            const auto list = styleList(snapshot.params.motionMode);
            for (uint32_t i = 0u; i < list.count; ++i)
                styleMenuItems[i] = kStyleNames[list.scenes[i]];
            return { styleMenuItems.data(), list.count };
        }
        default: return { nullptr, 0u };
        }
    }

    int menuSelected(MenuKind kind) const
    {
        switch (kind) {
        case MenuKind::Order: return static_cast<int>(snapshot.params.order) - 1;
        case MenuKind::Mute: return snapshot.params.selectedEnabled ? 0 : 1;
        case MenuKind::Motion:
            return static_cast<int>(snapshot.params.motionMode);
        case MenuKind::Style:
            return styleIndex(snapshot.params.motionMode,
                snapshot.params.motionScene);
        case MenuKind::Hemisphere:
            return snapshot.params.upperHemisphereOnly ? 1 : 0;
        default: return -1;
        }
    }

    void updateMenuBounds(double rowY)
    {
        const auto items = menuItems(activeMenu);
        const double height = static_cast<double>(items.second) * 18.0;
        const double y = std::max(28.0,
            std::min(rowY + 18.0, 654.0 - height));
        dropdownBounds = rect(kToolboxControlX, y,
            kToolboxMenuWidth, height);
    }

    void drawDropdown(CDrawContext& context)
    {
        const auto items = menuItems(activeMenu);
        const int selected = menuSelected(activeMenu);
        context.setFillColor(color(0x090909));
        context.drawRect(rect(dropdownBounds.left - 2.0,
            dropdownBounds.top - 2.0, dropdownBounds.getWidth() + 4.0,
            dropdownBounds.getHeight() + 4.0), kDrawFilled);
        context.setFillColor(color(0x1b1b1b));
        context.drawRect(dropdownBounds, kDrawFilled);
        for (uint32_t i = 0u; i < items.second; ++i) {
            const auto row = rect(dropdownBounds.left,
                dropdownBounds.top + i * 18.0,
                dropdownBounds.getWidth(), 18.0);
            if (static_cast<int>(i) == menuHover) {
                context.setFillColor(color(0x444444));
                context.drawRect(row, kDrawFilled);
            } else if (static_cast<int>(i) == selected) {
                context.setFillColor(color(0x373737));
                context.drawRect(row, kDrawFilled);
            } else if ((i % 2u) != 0u) {
                context.setFillColor(style.strip);
                context.drawRect(row, kDrawFilled);
            }
            if (static_cast<int>(i) == selected
                || static_cast<int>(i) == menuHover) {
                context.setFillColor(style.fill);
                context.drawRect(rect(row.left + 2.0, row.top + 2.0,
                    3.0, row.getHeight() - 4.0), kDrawFilled);
            }
            text(context, items.first[i], row.left + 9.0,
                row.top + 3.0, row.getWidth() - 18.0, style.value);
        }
    }

    int dropdownHit(const CPoint& point) const
    {
        if (!contains(dropdownBounds, point)) return -1;
        const auto items = menuItems(activeMenu);
        if (items.second == 0u) return -1;
        return static_cast<int>(std::min<uint32_t>(items.second - 1u,
            static_cast<uint32_t>((point.y - dropdownBounds.top) / 18.0)));
    }

    void applyMenu(MenuKind kind, uint32_t index)
    {
        switch (kind) {
        case MenuKind::Order: setParam(kOrderParamId, index + 1.0); break;
        case MenuKind::Mute: setParam(kPointMuteParamId,
            index == 0u ? 0.0 : 1.0); break;
        case MenuKind::Motion: setParam(kMotionModeParamId, index); break;
        case MenuKind::Style: {
            const auto list = styleList(snapshot.params.motionMode);
            if (index < list.count)
                setParam(kMotionSceneParamId, list.scenes[index]);
            break;
        }
        case MenuKind::Hemisphere: setParam(kUpperHemisphereParamId,
            index == 0u ? 0.0 : 1.0); break;
        default: break;
        }
        invalid();
    }

    void closeMenu()
    {
        activeMenu = MenuKind::None;
        menuHover = -1;
        invalid();
    }

    const SliderSpec* sliderForId(uint32_t id) const
    {
        for (const auto& slider : kSliders)
            if (slider.id == id) return &slider;
        return nullptr;
    }

    void updateSlider(const SliderSpec& slider, const CPoint& point)
    {
        const double normalized = std::clamp(
            (point.x - kToolboxControlX) / kToolboxTrackWidth,
            0.0, 1.0);
        double minimum = slider.minimum;
        double maximum = slider.maximum;
        if (slider.id == kElevationParamId
            && snapshot.params.upperHemisphereOnly) minimum = 0.0;
        if (slider.id == kPointParamId)
            maximum = static_cast<double>(std::max<uint32_t>(1u,
                snapshot.params.activePoints));
        setParam(slider.id, minimum + normalized * (maximum - minimum));
        invalid();
    }

    double viewScale() const
    {
        return std::min(kField.getWidth(), kField.getHeight()) * 0.34
            * std::clamp(viewZoom, 0.55, 2.20);
    }

    CPoint project(const s3g::Vec3& point, double* depth) const
    {
        const double cx = kField.left + kField.getWidth() * 0.5;
        const double cy = kField.top + kField.getHeight() * 0.54;
        const double scale = viewScale();
        if (viewMode == 0) {
            if (depth) *depth = point.z;
            return { cx - point.y * scale, cy - point.x * scale };
        }
        if (viewMode == 1) {
            if (depth) *depth = point.x;
            return { cx - point.y * scale, cy - point.z * scale };
        }
        constexpr double pi = 3.14159265358979323846;
        const float az = static_cast<float>(viewAzimuth * pi / 180.0);
        const float el = static_cast<float>(viewElevation * pi / 180.0);
        const float ca = std::cos(az);
        const float sa = std::sin(az);
        const float ce = std::cos(el);
        const float se = std::sin(el);
        const float x1 = ca * point.x - sa * point.y;
        const float y1 = sa * point.x + ca * point.y;
        const float y2 = ce * y1 + se * point.z;
        const float z2 = -se * y1 + ce * point.z;
        if (depth) *depth = z2;
        return { cx + x1 * scale, cy - y2 * scale };
    }

    int hitPoint(const CPoint& point) const
    {
        const uint32_t active = std::clamp<uint32_t>(
            snapshot.params.activePoints, 1u, kPointCount);
        int hit = -1;
        double best = 999999.0;
        for (uint32_t i = 0u; i < active; ++i) {
            const auto& source = snapshot.animatedPoints[i];
            if (!source.enabled) continue;
            const auto direction = s3g::directionFromAed(
                source.azimuthDeg, source.elevationDeg);
            const auto projected = project({ direction.x * source.distance,
                direction.y * source.distance,
                direction.z * source.distance }, nullptr);
            const double dx = point.x - projected.x;
            const double dy = point.y - projected.y;
            const double squared = dx * dx + dy * dy;
            if (squared < best && squared <= 144.0) {
                best = squared;
                hit = static_cast<int>(i);
            }
        }
        return hit;
    }

    void updateDraggedPoint(const CPoint& point)
    {
        if (dragPoint < 0 || dragPoint >= static_cast<int>(kPointCount))
            return;
        const uint32_t index = static_cast<uint32_t>(dragPoint);
        const auto current = snapshot.editPoints[index];
        const double cx = kField.left + kField.getWidth() * 0.5;
        const double cy = kField.top + kField.getHeight() * 0.54;
        const double scale = viewScale();
        constexpr float pi = 3.14159265358979323846f;
        if (viewMode == 0) {
            const float px = static_cast<float>(std::clamp(
                (cy - point.y) / scale, -2.0, 2.0));
            const float py = static_cast<float>(std::clamp(
                (cx - point.x) / scale, -2.0, 2.0));
            const float elevation = snapshot.params.upperHemisphereOnly
                ? std::max(0.0f, current.elevationDeg)
                : current.elevationDeg;
            const float planar = std::sqrt(px * px + py * py);
            const float distance = std::clamp(planar / std::max(0.05f,
                std::cos(elevation * pi / 180.0f)), 0.15f, 2.0f);
            setPointParam(index, kPerPointAzimuth,
                std::atan2(py, px) * 180.0f / pi);
            setPointParam(index, kPerPointElevation, elevation);
            setPointParam(index, kPerPointDistance, distance);
        } else if (viewMode == 1) {
            const float az = current.azimuthDeg * pi / 180.0f;
            const float el = current.elevationDeg * pi / 180.0f;
            const float x = std::cos(el) * std::cos(az) * current.distance;
            const float y = static_cast<float>(std::clamp(
                (cx - point.x) / scale, -2.0, 2.0));
            float z = static_cast<float>(std::clamp(
                (cy - point.y) / scale, -2.0, 2.0));
            if (snapshot.params.upperHemisphereOnly) z = std::max(0.0f, z);
            const float distance = std::clamp(
                std::sqrt(x * x + y * y + z * z), 0.15f, 2.0f);
            setPointParam(index, kPerPointAzimuth,
                std::atan2(y, x) * 180.0f / pi);
            setPointParam(index, kPerPointElevation,
                std::asin(std::clamp(z / std::max(0.15f, distance),
                    -1.0f, 1.0f)) * 180.0f / pi);
            setPointParam(index, kPerPointDistance, distance);
        }
        if (snapshot.params.selectedPoint != index)
            setParam(kPointParamId, index + 1.0);
        hasPointSelection = true;
        invalid();
    }

    bool handleMixerMouseDown(const CPoint& point, uint32_t clickCount)
    {
        const uint32_t active = std::clamp<uint32_t>(
            snapshot.params.activePoints, 1u, kPointCount);
        const uint32_t bankCount = std::max<uint32_t>(1u,
            (active + kMixerBankSize - 1u) / kMixerBankSize);
        for (uint32_t bank = 0u; bank < bankCount; ++bank) {
            if (contains(mixerBankButton(bank), point)) {
                mixerBank = bank;
                invalid();
                return true;
            }
        }
        if (contains(rect(mixerOutputTrack().left - 8.0,
                mixerOutputTrack().top - 8.0,
                mixerOutputTrack().getWidth() + 16.0,
                mixerOutputTrack().getHeight() + 16.0), point)) {
            if (clickCount >= 2u) {
                double value = 0.0;
                if (config.callbacks.getDefaultValue
                    && config.callbacks.getDefaultValue(
                        config.callbacks.context, kOutputParamId, &value))
                    setParam(kOutputParamId, value);
            } else {
                dragMixerOutput = true;
                parameterEdit.begin(kOutputParamId);
                updateMixerOutput(point);
            }
            return true;
        }
        mixerBank = std::min(mixerBank, bankCount - 1u);
        const uint32_t first = mixerBank * kMixerBankSize;
        const uint32_t end = std::min(first + kMixerBankSize, active);
        for (uint32_t i = first; i < end; ++i) {
            if (contains(mixerMuteRect(i), point)) {
                setPointParam(i, kPerPointMute,
                    snapshot.editPoints[i].enabled ? 1.0 : 0.0);
                selectMixerPoint(i);
                return true;
            }
            if (contains(mixerSoloRect(i), point)) {
                setPointParam(i, kPerPointSolo,
                    snapshot.editPoints[i].solo ? 0.0 : 1.0);
                selectMixerPoint(i);
                return true;
            }
            const auto gain = mixerGainRect(i);
            if (contains(rect(gain.left - 10.0, gain.top - 10.0,
                    gain.getWidth() + 20.0, gain.getHeight() + 20.0),
                    point)) {
                if (clickCount >= 2u) {
                    double value = 1.0;
                    if (config.callbacks.getDefaultValue)
                        config.callbacks.getDefaultValue(
                            config.callbacks.context,
                            perPointParamId(i, kPerPointGain), &value);
                    setPointParam(i, kPerPointGain, value);
                    selectMixerPoint(i);
                } else {
                    dragMixerPoint = static_cast<int>(i);
                    pointEdits[kPerPointGain].begin(
                        perPointParamId(i, kPerPointGain));
                    updateMixerGain(point);
                }
                return true;
            }
        }
        return false;
    }

    void selectMixerPoint(uint32_t point)
    {
        if (snapshot.params.selectedPoint != point)
            setParam(kPointParamId, point + 1.0);
        hasPointSelection = true;
        invalid();
    }

    void updateMixerGain(const CPoint& point)
    {
        if (dragMixerPoint < 0) return;
        const auto track = mixerGainRect(
            static_cast<uint32_t>(dragMixerPoint));
        const double normalized = std::clamp(
            (track.bottom - point.y) / track.getHeight(), 0.0, 1.0);
        setPointParam(static_cast<uint32_t>(dragMixerPoint),
            kPerPointGain, normalized * 2.0);
        selectMixerPoint(static_cast<uint32_t>(dragMixerPoint));
    }

    void updateMixerOutput(const CPoint& point)
    {
        const auto track = mixerOutputTrack();
        const double normalized = std::clamp(
            (point.x - track.left) / track.getWidth(), 0.0, 1.0);
        setParam(kOutputParamId, -60.0 + normalized * 72.0);
        invalid();
    }

    void normalizePresetCamera()
    {
        if (viewMode == 0) {
            viewAzimuth = 90.0;
            viewElevation = 0.0;
        } else if (viewMode == 1) {
            viewAzimuth = 90.0;
            viewElevation = 90.0;
        } else if (viewMode == 2) {
            viewAzimuth = -35.0;
            viewElevation = 34.0;
        }
    }

    void setViewPreset(int mode)
    {
        viewMode = mode;
        normalizePresetCamera();
        storeViewState();
        invalid();
    }

    void storeViewState()
    {
        if (config.callbacks.setViewState)
            config.callbacks.setViewState(config.callbacks.context,
                viewMode, viewAzimuth, viewElevation, viewZoom);
    }

    bool isDragging() const
    {
        return dragParam != 0u || dragMixerPoint >= 0 || dragPoint >= 0
            || dragMixerOutput || dragView;
    }

    void stopDragging()
    {
        parameterEdit.end();
        for (auto& edit : pointEdits) edit.end();
        dragParam = 0u;
        dragMixerPoint = -1;
        dragPoint = -1;
        dragMixerOutput = false;
        dragView = false;
    }

    void beginPointPositionEdit(uint32_t point)
    {
        pointEdits[kPerPointAzimuth].begin(
            perPointParamId(point, kPerPointAzimuth));
        pointEdits[kPerPointElevation].begin(
            perPointParamId(point, kPerPointElevation));
        pointEdits[kPerPointDistance].begin(
            perPointParamId(point, kPerPointDistance));
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
            "Ambi Point Encoder");
        const std::string path = foundation::runFileDialog(getFrame(), options);
        if (path.empty()) return;
        const bool succeeded = save
            ? config.callbacks.savePreset(config.callbacks.context, path.c_str())
            : config.callbacks.loadPreset(config.callbacks.context, path.c_str());
        if (!succeeded) return;
        const auto stem = foundation::pathToUtf8(
            foundation::pathFromUtf8(path.c_str()).stem());
        presetName = stem.empty() ? "CUSTOM" : stem;
        refreshSnapshot();
        viewMode = snapshot.viewMode;
        viewAzimuth = snapshot.viewAzimuthDeg;
        viewElevation = snapshot.viewElevationDeg;
        viewZoom = snapshot.viewZoom;
        normalizePresetCamera();
        invalid();
    }

    AmbiPointEditorConfig config;
    AmbiPointEditorSnapshot snapshot {};
    const foundation::Palette& style = foundation::palette();
    SharedPointer<CFontDesc> font;
    SharedPointer<CFontDesc> titleFont;
    SharedPointer<CFontDesc> tinyFont;
    SharedPointer<CVSTGUITimer> refreshTimer;
    foundation::ParameterEditSession parameterEdit;
    std::array<foundation::ParameterEditSession, 6u> pointEdits {};
    std::string presetName { "CURRENT" };
    int leftPage = 0;
    uint32_t mixerBank = 0u;
    bool hasPointSelection = false;
    int viewMode = 0;
    double viewAzimuth = 90.0;
    double viewElevation = 0.0;
    double viewZoom = 1.0;
    uint32_t dragParam = 0u;
    int dragMixerPoint = -1;
    int dragPoint = -1;
    bool dragMixerOutput = false;
    bool dragView = false;
    CPoint lastDrag {};
    MenuKind activeMenu = MenuKind::None;
    int menuHover = -1;
    CRect dropdownBounds {};
    mutable std::array<const char*, 5u> styleMenuItems {};
};

} // namespace

class AmbiPointEditor final : public foundation::EditorHost {
public:
    AmbiPointEditor(const AmbiPointEditorConfig& editorConfig,
                    uint32_t requestedWidth, uint32_t requestedHeight)
        : EditorHost(editorConfig.nativeWidth, editorConfig.nativeHeight,
            requestedWidth, requestedHeight)
        , config(editorConfig)
    {
    }

    bool build()
    {
        return ready() && attach(new AmbiPointView(config));
    }

private:
    AmbiPointEditorConfig config;
};

AmbiPointEditor* createAmbiPointEditor(
    const AmbiPointEditorConfig& config, uint32_t width, uint32_t height)
{
    auto* editor = new (std::nothrow) AmbiPointEditor(config, width, height);
    if (!editor) return nullptr;
    if (!editor->build()) {
        delete editor;
        return nullptr;
    }
    return editor;
}

void destroyAmbiPointEditor(AmbiPointEditor* editor) { delete editor; }

bool setAmbiPointEditorParent(AmbiPointEditor* editor, void* nativeParent)
{
    return editor && editor->setParent(nativeParent);
}

bool setAmbiPointEditorSize(AmbiPointEditor* editor,
                            uint32_t width, uint32_t height)
{
    return editor && editor->setSize(width, height);
}

bool setAmbiPointEditorVisible(AmbiPointEditor* editor, bool visible)
{
    return editor && editor->setVisible(visible);
}

} // namespace s3g::portable_gui
