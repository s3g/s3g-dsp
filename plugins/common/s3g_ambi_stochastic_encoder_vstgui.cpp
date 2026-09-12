#include "s3g_ambi_stochastic_encoder_vstgui.h"

#include "s3g_gui_layout.h"
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
#include <filesystem>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace s3g::portable_gui {
namespace {

using namespace VSTGUI;
namespace layout = s3g::gui_layout;

constexpr uint32_t kNativeWidth = 1160u;
constexpr uint32_t kNativeHeight = 860u;
constexpr uint32_t kVoiceCount = s3g::kAmbiStochasticMaxVoices;

constexpr uint32_t kOrderParamId = 1u;
constexpr uint32_t kVoicesParamId = 2u;
constexpr uint32_t kModeParamId = 3u;
constexpr uint32_t kSelectionParamId = 4u;
constexpr uint32_t kTransitionParamId = 5u;
constexpr uint32_t kAmplitudeDistributionParamId = 6u;
constexpr uint32_t kDurationDistributionParamId = 7u;
constexpr uint32_t kBaseNoteParamId = 8u;
constexpr uint32_t kSeedSpreadParamId = 9u;
constexpr uint32_t kDetuneParamId = 10u;
constexpr uint32_t kBreakpointsParamId = 11u;
constexpr uint32_t kAmplitudeStepParamId = 12u;
constexpr uint32_t kDurationStepParamId = 13u;
constexpr uint32_t kAmplitudeRangeParamId = 14u;
constexpr uint32_t kDurationRangeParamId = 15u;
constexpr uint32_t kFieldDensityParamId = 16u;
constexpr uint32_t kNeighborTransferParamId = 17u;
constexpr uint32_t kSelectionMemoryParamId = 18u;
constexpr uint32_t kFieldDurationParamId = 19u;
constexpr uint32_t kFieldContrastParamId = 20u;
constexpr uint32_t kAttackParamId = 21u;
constexpr uint32_t kDecayParamId = 22u;
constexpr uint32_t kSustainParamId = 23u;
constexpr uint32_t kReleaseParamId = 24u;
constexpr uint32_t kTopologyShapeParamId = 25u;
constexpr uint32_t kTopologyMotionParamId = 26u;
constexpr uint32_t kTopologyRateParamId = 27u;
constexpr uint32_t kTopologyAmountParamId = 28u;
constexpr uint32_t kTopologyDepthParamId = 29u;
constexpr uint32_t kTopologyScaleParamId = 30u;
constexpr uint32_t kTopologyCollapseParamId = 31u;
constexpr uint32_t kTopologyTwistParamId = 32u;
constexpr uint32_t kAzimuthParamId = 33u;
constexpr uint32_t kElevationParamId = 34u;
constexpr uint32_t kDistanceParamId = 35u;
constexpr uint32_t kSpatialFollowParamId = 36u;
constexpr uint32_t kOutputParamId = 37u;
constexpr uint32_t kFrequencyFloorParamId = 38u;
constexpr uint32_t kFieldRestParamId = 39u;
constexpr uint32_t kMacroDurationParamId = 40u;
constexpr uint32_t kFieldListenModeParamId = 41u;
constexpr uint32_t kFieldListenAmountParamId = 42u;
constexpr uint32_t kSurfaceXParamId = 43u;
constexpr uint32_t kSurfaceYParamId = 44u;

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

CRect rect(const layout::Rect& value)
{
    return rect(value.x, value.y, value.width, value.height);
}

bool contains(const CRect& bounds, const CPoint& point)
{
    return foundation::contains(bounds, point);
}

constexpr layout::Canvas kGuiCanvas {
    static_cast<double>(kNativeWidth), static_cast<double>(kNativeHeight)
};
constexpr auto kOutputPanel = layout::makePanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Output,
    layout::kLargeEncoderFirstColumn, 42.0,
    layout::toolboxHeightForRows(2u), 2u);
constexpr auto kEnginePanel = layout::stackPanel(
    layout::PanelRole::Engine, kOutputPanel,
    layout::toolboxHeightForRows(6u), 6u);
constexpr auto kWalkPanel = layout::stackPanel(
    layout::PanelRole::EventTiming, kEnginePanel,
    layout::toolboxHeightForRows(7u), 7u);
constexpr auto kEnvelopePanel = layout::stackPanel(
    layout::PanelRole::Envelope, kWalkPanel,
    layout::toolboxHeightForRows(4u), 4u);
constexpr auto kListenerPanel = layout::stackPanel(
    layout::PanelRole::Listener, kEnvelopePanel,
    layout::toolboxHeightForRows(2u), 2u);
constexpr auto kTopologyPanel = layout::makePanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Topology,
    layout::kLargeEncoderSecondColumn, 42.0,
    layout::toolboxHeightForRows(8u), 8u);
constexpr auto kProjectionPanel = layout::stackPanel(
    layout::PanelRole::Projection, kTopologyPanel,
    layout::toolboxHeightForRows(4u), 4u);
constexpr auto kSelectionPanel = layout::stackPanel(
    layout::PanelRole::Utility, kProjectionPanel,
    layout::toolboxHeightForRows(4u), 4u);
constexpr auto kTimeFieldsPanel = layout::stackPanel(
    layout::PanelRole::EventTiming, kSelectionPanel,
    layout::toolboxHeightForRows(5u), 5u);

const CRect kFieldPanel = rect(18.0, 42.0, 596.0, 500.0);
const CRect kField = rect(34.0, 72.0, 564.0, 454.0);
const CRect kWavePanel = rect(18.0, 554.0, 596.0, 288.0);
const CRect kWave = rect(34.0, 584.0, 564.0, 174.0);
const CRect kHistory = rect(34.0, 774.0, 564.0, 50.0);

struct SliderSpec {
    uint32_t id;
    const char* label;
    layout::Panel panel;
    uint32_t row;
    double minimum;
    double maximum;
    bool logarithmic;
};

constexpr std::array<SliderSpec, 33u> kSliders {{
    { kOutputParamId, "OUT", kOutputPanel, 0u, -60.0, 6.0, false },
    { kVoicesParamId, "VOICES", kEnginePanel, 0u, 1.0, 64.0, false },
    { kBaseNoteParamId, "BASE", kEnginePanel, 2u, 12.0, 96.0, false },
    { kSeedSpreadParamId, "SPREAD", kEnginePanel, 3u, 0.0, 48.0, false },
    { kDetuneParamId, "DEV", kEnginePanel, 4u, 0.0, 100.0, false },
    { kFrequencyFloorParamId, "LOW", kEnginePanel, 5u, 2.0, 240.0, true },
    { kBreakpointsParamId, "PTS", kWalkPanel, 2u, 4.0, 32.0, false },
    { kAmplitudeStepParamId, "A-STEP", kWalkPanel, 3u, 0.0, 1.0, false },
    { kDurationStepParamId, "T-STEP", kWalkPanel, 4u, 0.0, 1.0, false },
    { kAmplitudeRangeParamId, "A-RANGE", kWalkPanel, 5u, 0.0, 1.0, false },
    { kDurationRangeParamId, "T-RANGE", kWalkPanel, 6u, 0.0, 1.0, false },
    { kAttackParamId, "ATTACK", kEnvelopePanel, 0u, 1.0, 4000.0, true },
    { kDecayParamId, "DECAY", kEnvelopePanel, 1u, 5.0, 8000.0, true },
    { kSustainParamId, "SUSTAIN", kEnvelopePanel, 2u, 0.0, 1.0, false },
    { kReleaseParamId, "RELEASE", kEnvelopePanel, 3u, 5.0, 12000.0, true },
    { kFieldListenAmountParamId, "CAPTURE", kListenerPanel, 1u, 0.0, 1.0, false },
    { kNeighborTransferParamId, "XFER", kSelectionPanel, 2u, 0.0, 1.0, false },
    { kSelectionMemoryParamId, "MEM", kSelectionPanel, 3u, 0.0, 1.0, false },
    { kFieldDensityParamId, "DENS", kTimeFieldsPanel, 0u, 0.0, 1.0, false },
    { kFieldDurationParamId, "DUR", kTimeFieldsPanel, 1u, 0.05, 30.0, true },
    { kFieldContrastParamId, "CONT", kTimeFieldsPanel, 2u, 0.0, 1.0, false },
    { kFieldRestParamId, "REST", kTimeFieldsPanel, 3u, 0.02, 8.0, true },
    { kMacroDurationParamId, "MACRO", kTimeFieldsPanel, 4u, 2.0, 300.0, true },
    { kTopologyRateParamId, "RATE", kTopologyPanel, 2u, 0.001, 1.0, true },
    { kTopologyAmountParamId, "AMT", kTopologyPanel, 3u, 0.0, 1.0, false },
    { kTopologyDepthParamId, "DEPTH", kTopologyPanel, 4u, 0.0, 1.0, false },
    { kTopologyScaleParamId, "SCALE", kTopologyPanel, 5u, 0.25, 2.0, false },
    { kTopologyCollapseParamId, "COLL", kTopologyPanel, 6u, 0.0, 1.0, false },
    { kTopologyTwistParamId, "TWIST", kTopologyPanel, 7u, -1.0, 1.0, false },
    { kAzimuthParamId, "AZIM", kProjectionPanel, 0u, -180.0, 180.0, false },
    { kElevationParamId, "ELEV", kProjectionPanel, 1u, -90.0, 90.0, false },
    { kDistanceParamId, "DIST", kProjectionPanel, 2u, 0.15, 2.0, false },
    { kSpatialFollowParamId, "FOLLOW", kProjectionPanel, 3u, 0.0, 1.0, false },
}};

enum class MenuKind {
    None,
    Mode,
    Order,
    Selection,
    Transition,
    AmplitudeDistribution,
    DurationDistribution,
    Shape,
    Motion,
    Preset,
    Listener,
    SurfacePreset,
    SurfaceCurve,
};

struct MenuSpec {
    MenuKind kind;
    const char* label;
    layout::Panel panel;
    uint32_t row;
};

constexpr std::array<MenuSpec, 8u> kMenus {{
    { MenuKind::Mode, "MODE", kEnginePanel, 1u },
    { MenuKind::Order, "ORDER", kOutputPanel, 1u },
    { MenuKind::AmplitudeDistribution, "A-DIST", kWalkPanel, 0u },
    { MenuKind::DurationDistribution, "T-DIST", kWalkPanel, 1u },
    { MenuKind::Shape, "SHAPE", kTopologyPanel, 0u },
    { MenuKind::Motion, "ANIM", kTopologyPanel, 1u },
    { MenuKind::Selection, "LAW", kSelectionPanel, 0u },
    { MenuKind::Transition, "JOIN", kSelectionPanel, 1u },
}};

constexpr std::array<const char*, 3u> kModeItems {{ "FREE", "MIDI", "BOTH" }};
constexpr std::array<const char*, 7u> kOrderItems {{
    "1OA", "2OA", "3OA", "4OA", "5OA", "6OA", "7OA" }};
constexpr std::array<const char*, 6u> kSelectionItems {{
    "RANDOM", "SERIES", "WEIGHT", "TENDENCY", "MARKOV", "WALK" }};
constexpr std::array<const char*, 3u> kTransitionItems {{ "LINK", "MERGE", "VARY" }};
constexpr std::array<const char*, 7u> kDistributionItems {{
    "UNIFORM", "GAUSS", "CAUCHY", "LOGISTIC", "ARCSINE", "EXPON", "BINARY" }};
constexpr std::array<const char*, 12u> kShapeItems {{
    "AED", "SHEAR", "FOLD", "VORTEX", "PINCH", "RUPTURE",
    "SCATTER", "MIRROR", "WAVE", "LINE", "PLANE", "FORSY" }};
constexpr std::array<const char*, 18u> kMotionItems {{
    "OFF", "FREE", "DRIFT", "PULSE", "ORBIT", "FOLD", "WEAVE",
    "GRID", "TRACE", "HOVER", "LEAP", "FIELD", "PAIR", "FLOW",
    "GROUP", "MARCH", "PATH", "SCAT" }};
constexpr std::array<const char*, 13u> kPresetItems {{
    "FREE PERIOD", "MICRO POINTS", "PRESSURE BLOOM", "HARSH POLYGONS",
    "SPARSE SIGNALS", "MARKOV CLUSTERS", "METALLIC FLIGHT",
    "SUBHARMONIC TIDES", "BINARY FRACTURE", "SLOW CONSTELLATION",
    "MIDI PRESSURE", "HYBRID FIELD", "FULL 7OA FIELD" }};
constexpr std::array<const char*, 5u> kListenerItems {{
    "OFF", "LOCAL", "CROSS", "DIFFUSE", "ROAMING" }};
constexpr std::array<const char*, 4u> kCurveItems {{
    "SOFT", "LINEAR", "SMOOTH", "TIGHT" }};

double sliderCurvePower(uint32_t id)
{
    switch (id) {
    case kVoicesParamId: return 1.45;
    case kSeedSpreadParamId: return 1.35;
    case kDetuneParamId: return 1.40;
    case kBreakpointsParamId: return 1.30;
    case kAmplitudeStepParamId:
    case kDurationStepParamId: return 1.75;
    case kAmplitudeRangeParamId:
    case kDurationRangeParamId: return 1.50;
    case kNeighborTransferParamId: return 1.35;
    case kFieldDensityParamId: return 1.90;
    case kFieldContrastParamId: return 1.35;
    case kSustainParamId: return 1.50;
    case kTopologyAmountParamId: return 1.25;
    case kTopologyDepthParamId: return 1.35;
    case kTopologyCollapseParamId: return 1.40;
    case kTopologyTwistParamId: return 1.35;
    default: return 1.0;
    }
}

double sliderNorm(const SliderSpec& spec, double value)
{
    value = std::clamp(value, spec.minimum, spec.maximum);
    if (spec.logarithmic)
        return std::log(value / spec.minimum)
            / std::log(spec.maximum / spec.minimum);
    const double linear = (value - spec.minimum)
        / (spec.maximum - spec.minimum);
    const double power = sliderCurvePower(spec.id);
    if (spec.id == kTopologyTwistParamId) {
        const double centered = linear * 2.0 - 1.0;
        const double curved = std::copysign(
            std::pow(std::abs(centered), 1.0 / power), centered);
        return (curved + 1.0) * 0.5;
    }
    return std::pow(linear, 1.0 / power);
}

double sliderValue(const SliderSpec& spec, double normalized)
{
    normalized = std::clamp(normalized, 0.0, 1.0);
    if (spec.logarithmic)
        return spec.minimum * std::pow(
            spec.maximum / spec.minimum, normalized);
    const double power = sliderCurvePower(spec.id);
    double linear = std::pow(normalized, power);
    if (spec.id == kTopologyTwistParamId) {
        const double centered = normalized * 2.0 - 1.0;
        const double curved = std::copysign(
            std::pow(std::abs(centered), power), centered);
        linear = (curved + 1.0) * 0.5;
    }
    return spec.minimum + (spec.maximum - spec.minimum) * linear;
}

float linearToSrgb(float value)
{
    const float x = std::clamp(value, 0.0f, 1.0f);
    return x <= 0.0031308f ? x * 12.92f
        : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

CColor pointColor(const s3g::AmbiStochasticPoint& point, bool selected,
                  uint8_t alpha = 255u)
{
    const float hue = std::fmod(point.azimuthDeg / 360.0f + 1.0f, 1.0f);
    const float light = std::clamp(
        (std::clamp(point.elevationDeg, -90.0f, 90.0f) + 90.0f)
            / 180.0f,
        0.30f, 0.86f);
    const float chroma = std::clamp(point.distance / 2.0f, 0.10f, 1.0f)
        * 0.34f;
    const float a = std::cos(hue * 2.0f * s3g::kPi) * chroma;
    const float b = std::sin(hue * 2.0f * s3g::kPi) * chroma;
    const float l3 = light + 0.3963377774f * a + 0.2158037573f * b;
    const float m3 = light - 0.1055613458f * a - 0.0638541728f * b;
    const float s3 = light - 0.0894841775f * a - 1.2914855480f * b;
    const float l = l3 * l3 * l3;
    const float m = m3 * m3 * m3;
    const float s = s3 * s3 * s3;
    float red = linearToSrgb(4.0767416621f * l - 3.3077115913f * m
        + 0.2309699292f * s);
    float green = linearToSrgb(-1.2684380046f * l + 2.6097574011f * m
        - 0.3413193965f * s);
    float blue = linearToSrgb(-0.0041960863f * l - 0.7034186147f * m
        + 1.7076147010f * s);
    const float grayMix = selected ? 0.06f : 0.17f;
    red = red * (1.0f - grayMix) + 0.72f * grayMix;
    green = green * (1.0f - grayMix) + 0.72f * grayMix;
    blue = blue * (1.0f - grayMix) + 0.72f * grayMix;
    return CColor(static_cast<uint8_t>(std::clamp(red, 0.0f, 1.0f) * 255.0f),
        static_cast<uint8_t>(std::clamp(green, 0.0f, 1.0f) * 255.0f),
        static_cast<uint8_t>(std::clamp(blue, 0.0f, 1.0f) * 255.0f), alpha);
}

std::vector<CPoint> clipSurfacePolygon(const std::vector<CPoint>& polygon,
                                       CPoint site, CPoint other)
{
    if (polygon.empty()) return {};
    const double a = 2.0 * (other.x - site.x);
    const double b = 2.0 * (other.y - site.y);
    const double c = other.x * other.x + other.y * other.y
        - site.x * site.x - site.y * site.y;
    const auto inside = [=](CPoint point) {
        return a * point.x + b * point.y <= c + 0.0001;
    };
    const auto intersect = [=](CPoint first, CPoint second) {
        const double denominator = a * (second.x - first.x)
            + b * (second.y - first.y);
        if (std::abs(denominator) < 0.000001) return first;
        const double amount = std::clamp(
            (c - a * first.x - b * first.y) / denominator, 0.0, 1.0);
        return CPoint(first.x + (second.x - first.x) * amount,
            first.y + (second.y - first.y) * amount);
    };
    std::vector<CPoint> result;
    result.reserve(polygon.size() + 2u);
    CPoint previous = polygon.back();
    bool previousInside = inside(previous);
    for (const auto current : polygon) {
        const bool currentInside = inside(current);
        if (currentInside) {
            if (!previousInside) result.push_back(intersect(previous, current));
            result.push_back(current);
        } else if (previousInside) {
            result.push_back(intersect(previous, current));
        }
        previous = current;
        previousInside = currentInside;
    }
    return result;
}

} // namespace

namespace {

class AmbiStochasticView final : public foundation::ContentView {
public:
    explicit AmbiStochasticView(const AmbiStochasticEditorConfig& editorConfig)
        : ContentView(rect(0.0, 0.0, editorConfig.nativeWidth,
            editorConfig.nativeHeight)), config(editorConfig)
    {
        const auto& metrics = foundation::fontMetrics();
        font = foundation::makeUiFont(metrics.body);
        titleFont = foundation::makeUiFont(metrics.title);
        tinyFont = foundation::makeUiFont(metrics.tiny);
        parameterEdit.setCallbacks({ config.callbacks.context,
            config.callbacks.beginParamEdit, config.callbacks.setParam,
            config.callbacks.endParamEdit });
        surfaceXEdit.setCallbacks({ config.callbacks.context,
            config.callbacks.beginParamEdit, config.callbacks.setParam,
            config.callbacks.endParamEdit });
        surfaceYEdit.setCallbacks({ config.callbacks.context,
            config.callbacks.beginParamEdit, config.callbacks.setParam,
            config.callbacks.endParamEdit });
        refreshSnapshot();
        viewMode = snapshot.viewMode;
        viewAzimuth = snapshot.viewAzimuthDeg;
        viewElevation = snapshot.viewElevationDeg;
        viewZoom = snapshot.viewZoom;
        selectedSurfaceCell = snapshot.surface.cellCount > 0u ? 0 : -1;
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
        drawFieldPanel(*context);
        drawPressure(*context);
        drawPanels(*context);
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

        const auto band = layout::encoderTitleBand(kGuiCanvas);
        if (contains(rect(band.presetMenu), point)) {
            openMenu(MenuKind::Preset, rect(band.presetMenu));
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
            refreshSnapshot();
            event.consumed = true;
            return;
        }

        for (int page = 0; page < 3; ++page) {
            if (contains(pageButton(page), point)) {
                fieldPage = page;
                stopDragging();
                invalid();
                event.consumed = true;
                return;
            }
        }

        if (fieldPage == 2 && handleSurfaceMouseDown(point)) {
            event.consumed = true;
            return;
        }
        if (fieldPage == 1) {
            if (contains(listenerVoiceRect(), point)) {
                const uint32_t voices = std::clamp<uint32_t>(
                    snapshot.params.voices, 1u, kVoiceCount);
                selectedVoice = point.x < listenerVoiceRect().getCenter().x
                    ? (selectedVoice + voices - 1u) % voices
                    : (selectedVoice + 1u) % voices;
                setSelectedVoice();
                event.consumed = true;
                return;
            }
            if (contains(kField, point)) {
                dragView = true;
                lastDrag = point;
                event.consumed = true;
                return;
            }
        } else if (fieldPage == 0) {
            for (int index = 0; index < 2; ++index) {
                if (contains(zoomButton(index), point)) {
                    viewZoom = std::clamp(viewZoom
                        * (index == 0 ? 0.88 : 1.14), 0.55, 2.20);
                    storeViewState();
                    event.consumed = true;
                    return;
                }
            }
            for (int index = 0; index < 3; ++index) {
                if (contains(viewButton(index), point)) {
                    setViewPreset(index);
                    event.consumed = true;
                    return;
                }
            }
            if (contains(kField, point)) {
                const int voice = hitVoice(point);
                if (voice >= 0) {
                    selectedVoice = static_cast<uint32_t>(voice);
                    setSelectedVoice();
                } else {
                    dragView = true;
                    lastDrag = point;
                }
                event.consumed = true;
                return;
            }
        }

        if (contains(listenerMenuRect(), point)) {
            openMenu(MenuKind::Listener, listenerMenuRect());
            event.consumed = true;
            return;
        }
        for (const auto& menu : kMenus) {
            const auto bounds = menuRect(menu);
            if (!contains(bounds, point)) continue;
            openMenu(menu.kind, bounds);
            event.consumed = true;
            return;
        }
        for (const auto& slider : kSliders) {
            if (!contains(sliderHitRect(slider), point)) continue;
            if (event.clickCount >= 2u && config.callbacks.getDefaultValue) {
                double value = 0.0;
                if (config.callbacks.getDefaultValue(config.callbacks.context,
                        slider.id, &value)) setParam(slider.id, value);
            } else {
                dragParam = slider.id;
                parameterEdit.begin(slider.id);
                updateSlider(slider, point);
            }
            event.consumed = true;
            return;
        }
    }

    void onMouseMoveEvent(MouseMoveEvent& event) override
    {
        if (event.buttonState.has(MouseButton::Left)) {
            if (dragSurfaceCell >= 0)
                updateSurfacePosition(event.mousePosition, false);
            else if (dragSurfaceCursor)
                updateSurfacePosition(event.mousePosition, true);
            else if (dragView) {
                viewMode = -1;
                viewAzimuth = std::fmod(viewAzimuth
                    + (event.mousePosition.x - lastDrag.x) * 0.55
                    + 540.0, 360.0) - 180.0;
                viewElevation = std::clamp(viewElevation
                    - (event.mousePosition.y - lastDrag.y) * 0.45,
                    -85.0, 85.0);
                lastDrag = event.mousePosition;
                storeViewState();
            } else if (dragParam != 0u) {
                if (const auto* spec = sliderForId(dragParam))
                    updateSlider(*spec, event.mousePosition);
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
        selectedVoice = std::min<uint32_t>(selectedVoice,
            std::clamp<uint32_t>(snapshot.params.voices, 1u,
                kVoiceCount) - 1u);
        if (selectedSurfaceCell >= static_cast<int>(snapshot.surface.cellCount))
            selectedSurfaceCell = snapshot.surface.cellCount > 0u
                ? static_cast<int>(snapshot.surface.cellCount) - 1 : -1;
    }

    double param(uint32_t id) const
    {
        if (!surfaceEdit && config.callbacks.getEffectiveParam)
            return config.callbacks.getEffectiveParam(
                config.callbacks.context, id);
        return config.callbacks.getParam
            ? config.callbacks.getParam(config.callbacks.context, id) : 0.0;
    }

    void setParam(uint32_t id, double value)
    {
        if (parameterEdit.active(id)) parameterEdit.set(value);
        else parameterEdit.perform(id, value);
        refreshSnapshot();
        invalid();
    }

    bool surfaceAction(AmbiStochasticSurfaceAction action, int32_t index,
                       double first = 0.0, double second = 0.0)
    {
        const bool result = config.callbacks.surfaceAction
            && config.callbacks.surfaceAction(config.callbacks.context,
                action, index, first, second);
        refreshSnapshot();
        invalid();
        return result;
    }

    void setSelectedVoice()
    {
        if (config.callbacks.setSelectedVoice)
            config.callbacks.setSelectedVoice(
                config.callbacks.context, selectedVoice);
        refreshSnapshot();
        invalid();
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
        const auto band = layout::encoderTitleBand(kGuiCanvas);
        foundation::drawPluginTitle(context, config.pluginName ? config.pluginName : "",
            rect(band.titleX, band.titleY - 2.0,
                band.presetLabelX - band.titleX - 8.0, 15.0), titleFont);
        text(context, "PRESET", band.presetLabelX, band.controlY + 1.0,
            band.presetMenu.x - band.presetLabelX - 4.0, style.label);
        const auto preset = rect(band.presetMenu);
        foundation::drawMenuBox(context, preset,
            snapshot.presetName[0] ? snapshot.presetName : "CUSTOM", font);
        drawButton(context, rect(band.loadButton), "LOAD", false);
        drawButton(context, rect(band.saveButton), "SAVE", false);
        drawButton(context, rect(band.randomButton), "RANDOM", false);

        char status[48] {};
        std::snprintf(status, sizeof(status), "PK %+.1f",
            20.0 * std::log10(std::max(0.000001f, snapshot.outputPeak)));
        context.setFont(font);
        const double width = context.getStringWidth(status);
        text(context, status, config.nativeWidth - width
            - band.statusRightInset, band.titleY, width + 1.0, style.value);
    }

    CRect pageButton(int index) const
    {
        return rect(kFieldPanel.left + 98.0 + index * 74.0,
            kFieldPanel.top + 4.0, 70.0, 13.0);
    }

    CRect viewButton(int index) const
    {
        return rect(430.0 + static_cast<double>(index) * 49.0,
            46.0, 43.0, 13.0);
    }

    CRect zoomButton(int index) const
    {
        return rect(378.0 + static_cast<double>(index) * 23.0,
            46.0, 18.0, 13.0);
    }

    CRect listenerVoiceRect() const
    {
        return rect(kField.left + 10.0, kField.bottom - 42.0,
            92.0, 15.0);
    }

    void drawFieldPanelHeader(CDrawContext& context)
    {
        drawPanel(context, kFieldPanel, "VOICE FIELD");
        drawButton(context, pageButton(0), "FIELD", fieldPage == 0);
        drawButton(context, pageButton(1), "LISTEN", fieldPage == 1);
        drawButton(context, pageButton(2), "SURF", fieldPage == 2);
        if (fieldPage != 0) return;
        drawButton(context, zoomButton(0), "-", false);
        drawButton(context, zoomButton(1), "+", false);
        static const char* labels[] { "TOP", "SIDE", "3/4" };
        for (int index = 0; index < 3; ++index)
            drawButton(context, viewButton(index), labels[index],
                viewMode == index);
    }

    void drawPanels(CDrawContext& context)
    {
        drawPanel(context, rect(kOutputPanel.frame), "OUTPUT");
        drawPanel(context, rect(kEnginePanel.frame), "SOURCE / ENGINE");
        drawPanel(context, rect(kWalkPanel.frame), "SECOND-ORDER WALK");
        drawPanel(context, rect(kEnvelopePanel.frame), "ENVELOPE");
        drawPanel(context, rect(kListenerPanel.frame), "FIELD LISTENER");
        drawPanel(context, rect(kTopologyPanel.frame), "TOPOLOGY");
        drawPanel(context, rect(kProjectionPanel.frame), "PROJECTION");
        drawPanel(context, rect(kSelectionPanel.frame), "SELECTION");
        drawPanel(context, rect(kTimeFieldsPanel.frame), "TIME FIELDS");
        for (const auto& slider : kSliders) drawSlider(context, slider);
        for (const auto& menu : kMenus) drawMenu(context, menu);
        drawListenerMenu(context);
    }

    double rowY(const layout::Panel& panel, uint32_t row) const
    {
        return layout::rowY(panel, row);
    }

    CRect sliderTrackRect(const SliderSpec& slider) const
    {
        return rect(slider.panel.frame.x + 108.0,
            rowY(slider.panel, slider.row) + 1.0, 82.0, 9.0);
    }

    CRect sliderHitRect(const SliderSpec& slider) const
    {
        return rect(slider.panel.frame.x + 8.0,
            rowY(slider.panel, slider.row) - 8.0, 232.0, 24.0);
    }

    const SliderSpec* sliderForId(uint32_t id) const
    {
        for (const auto& slider : kSliders)
            if (slider.id == id) return &slider;
        return nullptr;
    }

    void drawTrack(CDrawContext& context, const CRect& track,
                   double normalized)
    {
        foundation::drawHorizontalSlider(context, track, normalized,
            track.top - 2.0, track.getHeight() + 4.0);
    }

    std::string paramText(uint32_t id) const
    {
        char buffer[64] {};
        const double value = param(id);
        if (config.callbacks.getParamText
            && config.callbacks.getParamText(config.callbacks.context,
                id, value, buffer,
                static_cast<uint32_t>(sizeof(buffer)))) return buffer;
        std::snprintf(buffer, sizeof(buffer), "%.3g", value);
        return buffer;
    }

    void drawSlider(CDrawContext& context, const SliderSpec& slider)
    {
        const double y = rowY(slider.panel, slider.row);
        text(context, slider.label, slider.panel.frame.x + 16.0, y - 2.0,
            88.0, style.label);
        drawTrack(context, sliderTrackRect(slider),
            sliderNorm(slider, param(slider.id)));
        text(context, paramText(slider.id), slider.panel.frame.x + 196.0,
            y - 2.0, 38.0, style.value, kRightText);
    }

    CRect menuRect(const MenuSpec& menu) const
    {
        return rect(menu.panel.frame.x + 108.0,
            rowY(menu.panel, menu.row) - 1.0, 124.0, 15.0);
    }

    CRect listenerMenuRect() const
    {
        return rect(kListenerPanel.frame.x + 108.0,
            rowY(kListenerPanel, 0u) - 1.0, 124.0, 15.0);
    }

    std::string menuValue(MenuKind kind) const
    {
        switch (kind) {
        case MenuKind::Mode:
            return kModeItems[std::clamp<uint32_t>(
                static_cast<uint32_t>(param(kModeParamId)), 0u, 2u)];
        case MenuKind::Order:
            return kOrderItems[std::clamp<uint32_t>(
                static_cast<uint32_t>(param(kOrderParamId)), 1u, 7u) - 1u];
        case MenuKind::Selection:
            return kSelectionItems[std::clamp<uint32_t>(
                static_cast<uint32_t>(param(kSelectionParamId)), 0u, 5u)];
        case MenuKind::Transition:
            return kTransitionItems[std::clamp<uint32_t>(
                static_cast<uint32_t>(param(kTransitionParamId)), 0u, 2u)];
        case MenuKind::AmplitudeDistribution:
            return kDistributionItems[std::clamp<uint32_t>(
                static_cast<uint32_t>(param(kAmplitudeDistributionParamId)),
                0u, 6u)];
        case MenuKind::DurationDistribution:
            return kDistributionItems[std::clamp<uint32_t>(
                static_cast<uint32_t>(param(kDurationDistributionParamId)),
                0u, 6u)];
        case MenuKind::Shape:
            return kShapeItems[std::clamp<uint32_t>(
                static_cast<uint32_t>(param(kTopologyShapeParamId)), 0u, 11u)];
        case MenuKind::Motion:
            return kMotionItems[std::clamp<uint32_t>(
                static_cast<uint32_t>(param(kTopologyMotionParamId)), 0u, 17u)];
        case MenuKind::Listener:
            return kListenerItems[std::clamp<uint32_t>(
                static_cast<uint32_t>(param(kFieldListenModeParamId)), 0u, 4u)];
        case MenuKind::SurfaceCurve:
            return kCurveItems[std::clamp<uint32_t>(
                static_cast<uint32_t>(snapshot.surface.curve), 0u, 3u)];
        case MenuKind::Preset:
            return snapshot.presetName[0] ? snapshot.presetName : "CUSTOM";
        case MenuKind::SurfacePreset:
            if (selectedSurfaceCell >= 0
                && static_cast<uint32_t>(selectedSurfaceCell)
                    < snapshot.surface.cellCount) {
                const auto& cell = snapshot.surface.cells[
                    static_cast<uint32_t>(selectedSurfaceCell)];
                return cell.name[0] ? cell.name : "SELECT CELL";
            }
            return "SELECT CELL";
        default: return {};
        }
    }

    void drawMenuBox(CDrawContext& context, const CRect& bounds,
                     const std::string& value)
    {
        foundation::drawMenuBox(context, bounds, value, font);
    }

    void drawMenu(CDrawContext& context, const MenuSpec& menu)
    {
        const double y = rowY(menu.panel, menu.row);
        text(context, menu.label, menu.panel.frame.x + 16.0, y - 2.0,
            88.0, style.label);
        drawMenuBox(context, menuRect(menu), menuValue(menu.kind));
    }

    void drawListenerMenu(CDrawContext& context)
    {
        const double y = rowY(kListenerPanel, 0u);
        text(context, "LISTEN", kListenerPanel.frame.x + 16.0, y - 2.0,
            88.0, style.label);
        drawMenuBox(context, listenerMenuRect(),
            menuValue(MenuKind::Listener));
    }

    std::pair<const char* const*, uint32_t> menuItems(MenuKind kind) const
    {
        switch (kind) {
        case MenuKind::Mode: return { kModeItems.data(), kModeItems.size() };
        case MenuKind::Order: return { kOrderItems.data(), kOrderItems.size() };
        case MenuKind::Selection:
            return { kSelectionItems.data(), kSelectionItems.size() };
        case MenuKind::Transition:
            return { kTransitionItems.data(), kTransitionItems.size() };
        case MenuKind::AmplitudeDistribution:
        case MenuKind::DurationDistribution:
            return { kDistributionItems.data(), kDistributionItems.size() };
        case MenuKind::Shape: return { kShapeItems.data(), kShapeItems.size() };
        case MenuKind::Motion: return { kMotionItems.data(), kMotionItems.size() };
        case MenuKind::Preset:
        case MenuKind::SurfacePreset:
            return { kPresetItems.data(), kPresetItems.size() };
        case MenuKind::Listener:
            return { kListenerItems.data(), kListenerItems.size() };
        case MenuKind::SurfaceCurve:
            return { kCurveItems.data(), kCurveItems.size() };
        default: return { nullptr, 0u };
        }
    }

    int menuSelected(MenuKind kind) const
    {
        switch (kind) {
        case MenuKind::Mode: return static_cast<int>(param(kModeParamId));
        case MenuKind::Order: return static_cast<int>(param(kOrderParamId)) - 1;
        case MenuKind::Selection:
            return static_cast<int>(param(kSelectionParamId));
        case MenuKind::Transition:
            return static_cast<int>(param(kTransitionParamId));
        case MenuKind::AmplitudeDistribution:
            return static_cast<int>(param(kAmplitudeDistributionParamId));
        case MenuKind::DurationDistribution:
            return static_cast<int>(param(kDurationDistributionParamId));
        case MenuKind::Shape:
            return static_cast<int>(param(kTopologyShapeParamId));
        case MenuKind::Motion:
            return static_cast<int>(param(kTopologyMotionParamId));
        case MenuKind::Preset: return snapshot.factoryPresetIndex;
        case MenuKind::Listener:
            return static_cast<int>(param(kFieldListenModeParamId));
        case MenuKind::SurfaceCurve:
            return static_cast<int>(snapshot.surface.curve);
        case MenuKind::SurfacePreset:
            if (selectedSurfaceCell >= 0
                && static_cast<uint32_t>(selectedSurfaceCell)
                    < snapshot.surface.cellCount)
                return snapshot.surface.cells[
                    static_cast<uint32_t>(selectedSurfaceCell)].presetIndex;
            return -1;
        default: return -1;
        }
    }

    void openMenu(MenuKind kind, const CRect& source)
    {
        activeMenu = kind;
        menuHover = -1;
        const auto items = menuItems(kind);
        const double height = static_cast<double>(items.second) * 18.0;
        double y = source.bottom + 2.0;
        if (y + height > kNativeHeight - 8.0)
            y = source.top - height - 2.0;
        dropdownBounds = rect(source.left, y, source.getWidth(), height);
        invalid();
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
        for (uint32_t index = 0u; index < items.second; ++index) {
            const auto row = rect(dropdownBounds.left,
                dropdownBounds.top + index * 18.0,
                dropdownBounds.getWidth(), 18.0);
            if (static_cast<int>(index) == menuHover) {
                context.setFillColor(color(0x4c4c4c));
                context.drawRect(row, kDrawFilled);
            } else if (static_cast<int>(index) == selected) {
                context.setFillColor(color(0x303030));
                context.drawRect(row, kDrawFilled);
            }
            foundation::drawDropdownItemSeparator(context, row, index,
                color(0x4b4b4b));
            textInRect(context, items.first[index],
                rect(row.left + 7.0, row.top, row.getWidth() - 14.0,
                    row.getHeight()), style.text, kLeftText);
        }
    }

    int dropdownHit(const CPoint& point) const
    {
        if (!contains(dropdownBounds, point)) return -1;
        const int index = static_cast<int>(
            (point.y - dropdownBounds.top) / 18.0);
        const auto items = menuItems(activeMenu);
        return index >= 0 && static_cast<uint32_t>(index) < items.second
            ? index : -1;
    }

    void closeMenu()
    {
        activeMenu = MenuKind::None;
        menuHover = -1;
        dropdownBounds = {};
        invalid();
    }

    void applyMenu(MenuKind kind, uint32_t index)
    {
        switch (kind) {
        case MenuKind::Mode: setParam(kModeParamId, index); break;
        case MenuKind::Order: setParam(kOrderParamId, index + 1u); break;
        case MenuKind::Selection: setParam(kSelectionParamId, index); break;
        case MenuKind::Transition: setParam(kTransitionParamId, index); break;
        case MenuKind::AmplitudeDistribution:
            setParam(kAmplitudeDistributionParamId, index); break;
        case MenuKind::DurationDistribution:
            setParam(kDurationDistributionParamId, index); break;
        case MenuKind::Shape: setParam(kTopologyShapeParamId, index); break;
        case MenuKind::Motion: setParam(kTopologyMotionParamId, index); break;
        case MenuKind::Listener:
            setParam(kFieldListenModeParamId, index); break;
        case MenuKind::Preset:
            if (config.callbacks.applyFactoryPreset)
                config.callbacks.applyFactoryPreset(
                    config.callbacks.context, index);
            refreshSnapshot();
            invalid();
            break;
        case MenuKind::SurfacePreset:
            surfaceAction(AmbiStochasticSurfaceAction::SetCellPreset,
                selectedSurfaceCell, index); break;
        case MenuKind::SurfaceCurve:
            surfaceAction(AmbiStochasticSurfaceAction::SetCurve,
                -1, index); break;
        default: break;
        }
    }

    void updateSlider(const SliderSpec& slider, const CPoint& point)
    {
        const auto track = sliderTrackRect(slider);
        double value = sliderValue(slider,
            (point.x - track.left) / track.getWidth());
        if (slider.id == kVoicesParamId
            || slider.id == kBreakpointsParamId) value = std::lround(value);
        setParam(slider.id, value);
    }

    void drawFieldPanel(CDrawContext& context)
    {
        drawFieldPanelHeader(context);
        if (fieldPage == 1) drawListenerPage(context);
        else if (fieldPage == 2) drawSurfacePage(context);
        else drawVoiceField(context);
    }

    double viewScale() const
    {
        return std::min(kField.getWidth(), kField.getHeight()) * 0.36
            * std::clamp(viewZoom, 0.55, 2.20);
    }

    CPoint project(const s3g::Vec3& point, double* depth = nullptr) const
    {
        const double centerX = kField.getCenter().x;
        const double centerY = kField.getCenter().y;
        const double scale = viewScale();
        if (viewMode == 0) {
            if (depth) *depth = point.y;
            return CPoint(centerX - point.x * scale,
                centerY - point.z * scale);
        }
        if (viewMode == 1) {
            if (depth) *depth = point.z;
            return CPoint(centerX - point.x * scale,
                centerY - point.y * scale);
        }
        const float azimuth = static_cast<float>(
            viewAzimuth * s3g::kPi / 180.0);
        const float elevation = static_cast<float>(
            viewElevation * s3g::kPi / 180.0);
        const float ca = std::cos(azimuth);
        const float sa = std::sin(azimuth);
        const float ce = std::cos(elevation);
        const float se = std::sin(elevation);
        const s3g::Vec3 world { point.z, point.x, point.y };
        const float x1 = ca * world.x - sa * world.y;
        const float y1 = sa * world.x + ca * world.y;
        const float y2 = ce * y1 + se * world.z;
        const float z2 = -se * y1 + ce * world.z;
        if (depth) *depth = z2;
        return CPoint(centerX + x1 * scale, centerY - y2 * scale);
    }

    void drawVoiceField(CDrawContext& context)
    {
        context.setFillColor(color(0x090909));
        context.drawRect(kField, kDrawFilled);
        context.setFrameColor(color(0x555555));
        context.setLineWidth(1.0);
        context.drawRect(kField, kDrawStroked);

        const double radius = viewScale();
        context.setFrameColor(color(0x303030));
        context.setLineWidth(0.8);
        context.drawEllipse(rect(kField.getCenter().x - radius,
            kField.getCenter().y - radius, radius * 2.0, radius * 2.0),
            kDrawStroked);

        const uint32_t voices = std::clamp<uint32_t>(surfaceEdit
                ? snapshot.params.voices : snapshot.voiceCount,
            1u, kVoiceCount);
        selectedVoice = std::min<uint32_t>(selectedVoice, voices - 1u);
        std::array<CPoint, kVoiceCount> projected {};
        for (uint32_t voice = 0u; voice < voices; ++voice)
            projected[voice] = project(snapshot.topology[voice]);

        for (uint32_t voice = 0u; voice < voices; ++voice) {
            const float membership = surfaceEdit ? 1.0f
                : std::clamp(snapshot.renderGain[voice], 0.0f, 1.0f);
            const uint32_t neighbors[] {
                std::min<uint32_t>(snapshot.neighbor[voice], voices - 1u),
                std::min<uint32_t>(snapshot.secondaryNeighbor[voice],
                    voices - 1u),
            };
            const float influence = snapshot.neighborInfluence[voice];
            const float pulse = snapshot.selectionPulse[voice];
            for (uint32_t edge = 0u; edge < 2u; ++edge) {
                if (neighbors[edge] == voice || neighbors[edge] < voice)
                    continue;
                context.setFrameColor(color(edge == 0u
                    ? 0xa0a0a0 : 0x686868, alphaByte(((edge == 0u
                        ? 0.12 : 0.06) + influence * 0.32
                        + pulse * 0.22) * membership)));
                context.setLineWidth(0.55 + influence * 0.75);
                context.drawLine(projected[voice], projected[neighbors[edge]]);
            }
        }

        for (uint32_t voice = 0u; voice < voices; ++voice) {
            const bool selected = voice == selectedVoice;
            const bool active = snapshot.fieldActive[voice] != 0u;
            const float membership = surfaceEdit ? 1.0f
                : std::clamp(snapshot.renderGain[voice], 0.0f, 1.0f);
            const double baseSize = voices > 32u ? 7.0 : 9.0;
            const double size = ((selected ? baseSize + 5.0 : baseSize)
                + std::clamp(snapshot.energy[voice] * 9.0f
                    + snapshot.kinetic[voice] * 2.0f, 0.0f, 6.0f))
                * (0.45f + membership * 0.55f);
            const auto marker = rect(projected[voice].x - size * 0.5,
                projected[voice].y - size * 0.5, size, size);
            const uint8_t alpha = alphaByte((active
                ? (selected ? 1.0 : 0.82) : 0.22) * membership);
            context.setFillColor(pointColor(
                snapshot.points[voice], selected, alpha));
            context.drawRect(marker, kDrawFilled);
            context.setFrameColor(color(selected ? 0xe0e0e0
                : (active ? 0x565656 : 0x2a2a2a), alphaByte(membership)));
            context.setLineWidth(1.0);
            context.drawRect(marker, kDrawStroked);
            if (membership > 0.45f) {
                char label[8] {};
                std::snprintf(label, sizeof(label), "%u", voice + 1u);
                textInRect(context, label,
                    rect(marker.left - 2.0, marker.top - 1.0,
                        marker.getWidth() + 4.0, marker.getHeight() + 2.0),
                    color(0x080808), kCenterText, tinyFont);
            }
        }

        const auto& topology = snapshot.topology[selectedVoice];
        const uint32_t neighbor = std::min<uint32_t>(
            snapshot.neighbor[selectedVoice], voices - 1u);
        char readout[128] {};
        std::snprintf(readout, sizeof(readout),
            "V%02u  G%u>G%u  %.1fHZ  %s  N%02u",
            selectedVoice + 1u,
            snapshot.currentGenerator[selectedVoice] + 1u,
            snapshot.nextGenerator[selectedVoice] + 1u,
            snapshot.frequency[selectedVoice],
            snapshot.fieldActive[selectedVoice] ? "ON" : "REST",
            neighbor + 1u);
        context.setFont(font);
        const double width = context.getStringWidth(readout);
        text(context, readout, kField.right - width - 9.0,
            kField.top + 7.0, width + 1.0, style.value);
        text(context, "X PRESSURE     Y EVENTS     Z PERIOD     R DRIVE",
            kField.left + 9.0, kField.bottom - 19.0,
            kField.getWidth() - 18.0, style.value);
        char coordinates[80] {};
        std::snprintf(coordinates, sizeof(coordinates), "%+.2f  %+.2f  %+.2f",
            topology.x, topology.y, topology.z);
        context.setFont(font);
        const double coordinateWidth = context.getStringWidth(coordinates);
        text(context, coordinates, kField.right - coordinateWidth - 9.0,
            kField.bottom - 22.0, coordinateWidth + 1.0, style.value);
    }

    int hitVoice(const CPoint& point) const
    {
        if (!contains(kField, point)) return -1;
        const uint32_t voices = std::clamp<uint32_t>(surfaceEdit
                ? snapshot.params.voices : snapshot.voiceCount,
            1u, kVoiceCount);
        int best = -1;
        double bestDistance = 14.0;
        for (uint32_t voice = 0u; voice < voices; ++voice) {
            const auto projected = project(snapshot.topology[voice]);
            const double distance = std::hypot(
                point.x - projected.x, point.y - projected.y);
            if (distance < bestDistance) {
                best = static_cast<int>(voice);
                bestDistance = distance;
            }
        }
        return best;
    }

    CRect surfaceEditRect() const
    {
        return rect(kField.left + 10.0, kField.top + 10.0, 50.0, 18.0);
    }

    CRect surfaceEnableRect() const
    {
        return rect(kField.left + 64.0, kField.top + 10.0, 48.0, 18.0);
    }

    CRect surfaceAddRect() const
    {
        return rect(kField.left + 116.0, kField.top + 10.0, 42.0, 18.0);
    }

    CRect surfaceDeleteRect() const
    {
        return rect(kField.left + 162.0, kField.top + 10.0, 42.0, 18.0);
    }

    CRect surfaceCaptureRect() const
    {
        return rect(kField.left + 208.0, kField.top + 10.0, 52.0, 18.0);
    }

    CRect surfacePresetRect() const
    {
        return rect(kField.left + 270.0, kField.top + 10.0, 174.0, 18.0);
    }

    CRect surfacePopRect() const
    {
        return rect(kField.left + 448.0, kField.top + 10.0, 26.0, 18.0);
    }

    CRect surfaceFocusMinusRect() const
    {
        return rect(kField.left + 178.0, kField.top + 38.0, 20.0, 18.0);
    }

    CRect surfaceFocusPlusRect() const
    {
        return rect(kField.left + 238.0, kField.top + 38.0, 20.0, 18.0);
    }

    CRect surfaceCurveRect() const
    {
        return rect(kField.left + 10.0, kField.top + 38.0, 112.0, 18.0);
    }

    CRect surfaceGlideMinusRect() const
    {
        return rect(kField.left + 326.0, kField.top + 38.0, 20.0, 18.0);
    }

    CRect surfaceGlidePlusRect() const
    {
        return rect(kField.left + 420.0, kField.top + 38.0, 20.0, 18.0);
    }

    CRect surfacePlotRect() const
    {
        return rect(kField.left + 10.0, kField.top + 68.0,
            kField.getWidth() - 20.0, kField.getHeight() - 80.0);
    }

    CColor surfaceCellColor(uint32_t index, double alpha) const
    {
        static constexpr std::array<uint32_t, 12u> colors {{
            0x6c3230, 0x68612a, 0x356b4f, 0x32636c,
            0x4c4a78, 0x72416d, 0x655238, 0x436a34,
            0x30665e, 0x3c5872, 0x5a4670, 0x704044,
        }};
        return color(colors[index % colors.size()], alphaByte(alpha));
    }

    CPoint surfacePoint(uint32_t index) const
    {
        const auto plot = surfacePlotRect();
        if (index >= snapshot.surface.cellCount) return {};
        const auto& cell = snapshot.surface.cells[index];
        return CPoint(plot.left + cell.x * plot.getWidth(),
            plot.bottom - cell.y * plot.getHeight());
    }

    int hitSurfaceCell(const CPoint& point) const
    {
        if (!contains(surfacePlotRect(), point)) return -1;
        int best = -1;
        double bestDistance = 14.0;
        for (uint32_t index = 0u; index < snapshot.surface.cellCount;
             ++index) {
            const auto site = surfacePoint(index);
            const double distance = std::hypot(
                point.x - site.x, point.y - site.y);
            if (distance < bestDistance) {
                best = static_cast<int>(index);
                bestDistance = distance;
            }
        }
        return best;
    }

    void drawSurfaceVoronoi(CDrawContext& context)
    {
        const auto plot = surfacePlotRect();
        const uint32_t count = std::min<uint32_t>(
            snapshot.surface.cellCount, s3g::kParameterSurfaceMaxCells);
        const CPoint cursor(plot.left
                + std::clamp(snapshot.effectiveSurfaceX, 0.0f, 1.0f)
                    * plot.getWidth(),
            plot.bottom
                - std::clamp(snapshot.effectiveSurfaceY, 0.0f, 1.0f)
                    * plot.getHeight());
        const CPoint target(plot.left
                + std::clamp(snapshot.params.surfaceX, 0.0f, 1.0f)
                    * plot.getWidth(),
            plot.bottom
                - std::clamp(snapshot.params.surfaceY, 0.0f, 1.0f)
                    * plot.getHeight());
        std::vector<CPoint> sites;
        sites.reserve(count);
        for (uint32_t index = 0u; index < count; ++index)
            sites.push_back(surfacePoint(index));

        context.setFillColor(color(0x090a0a));
        context.drawRect(plot, kDrawFilled);
        const std::vector<CPoint> bounds {
            { plot.left, plot.top }, { plot.right, plot.top },
            { plot.right, plot.bottom }, { plot.left, plot.bottom },
        };
        for (uint32_t index = 0u; index < count; ++index) {
            auto polygon = bounds;
            for (uint32_t other = 0u; other < count && !polygon.empty();
                 ++other) {
                if (other != index)
                    polygon = clipSurfacePolygon(
                        polygon, sites[index], sites[other]);
            }
            if (polygon.empty()) continue;
            auto path = owned(context.createGraphicsPath());
            if (!path) continue;
            path->beginSubpath(polygon.front());
            for (size_t point = 1u; point < polygon.size(); ++point)
                path->addLine(polygon[point]);
            path->closeSubpath();
            context.setFillColor(surfaceCellColor(index, 0.36));
            context.setFrameColor(color(0x7a8287, alphaByte(0.78)));
            context.setLineWidth(1.5);
            context.drawGraphicsPath(path, CDrawContext::kPathFilled);
            context.drawGraphicsPath(path, CDrawContext::kPathStroked);
        }

        context.setFrameColor(color(0x858f94, alphaByte(0.16)));
        context.setLineWidth(1.0);
        for (uint32_t division = 1u; division < 8u; ++division) {
            const double x = plot.left + plot.getWidth() * division / 8.0;
            const double y = plot.top + plot.getHeight() * division / 8.0;
            context.drawLine(CPoint(x, plot.top), CPoint(x, plot.bottom));
            context.drawLine(CPoint(plot.left, y), CPoint(plot.right, y));
        }

        for (uint32_t index = 0u; index < count; ++index) {
            const auto site = sites[index];
            const double distance = std::hypot(
                site.x - cursor.x, site.y - cursor.y);
            const double radius = std::clamp(
                42.0 / std::max(0.25, distance / 42.0 + 0.25),
                8.0, 42.0);
            context.setFillColor(surfaceCellColor(index, 0.16));
            context.drawEllipse(rect(site.x - radius * 1.9,
                site.y - radius * 1.9, radius * 3.8, radius * 3.8),
                kDrawFilled);
            context.setFillColor(surfaceCellColor(index, 0.30));
            context.drawEllipse(rect(site.x - radius, site.y - radius,
                radius * 2.0, radius * 2.0), kDrawFilled);
            const bool selected = static_cast<int>(index)
                == selectedSurfaceCell;
            const double nodeRadius = selected ? 6.0 : 4.5;
            context.setFillColor(surfaceCellColor(index, 0.95));
            context.setFrameColor(color(selected ? 0xdbe5e9 : 0x596166));
            context.setLineWidth(selected ? 2.0 : 1.0);
            context.drawEllipse(rect(site.x - nodeRadius,
                site.y - nodeRadius, nodeRadius * 2.0, nodeRadius * 2.0),
                kDrawFilledAndStroked);
            const auto& cell = snapshot.surface.cells[index];
            std::string label = cell.name[0] ? cell.name : "CELL";
            if (label.size() > 18u) label = label.substr(0u, 16u) + "...";
            text(context, label, site.x + nodeRadius + 5.0,
                site.y - 6.0, 116.0, style.value);
        }

        context.setFrameColor(color(0xc7d6db));
        context.setLineWidth(2.0);
        context.drawLine(CPoint(cursor.x - 13.0, cursor.y),
            CPoint(cursor.x + 13.0, cursor.y));
        context.drawLine(CPoint(cursor.x, cursor.y - 13.0),
            CPoint(cursor.x, cursor.y + 13.0));
        context.setLineWidth(1.5);
        context.drawRect(rect(cursor.x - 5.0, cursor.y - 5.0,
            10.0, 10.0), kDrawStroked);
        if (std::hypot(target.x - cursor.x, target.y - cursor.y) > 2.0) {
            auto diamond = owned(context.createGraphicsPath());
            if (diamond) {
                diamond->beginSubpath(CPoint(target.x, target.y - 6.0));
                diamond->addLine(CPoint(target.x + 6.0, target.y));
                diamond->addLine(CPoint(target.x, target.y + 6.0));
                diamond->addLine(CPoint(target.x - 6.0, target.y));
                diamond->closeSubpath();
                context.setFrameColor(color(0x9ea8ad, alphaByte(0.72)));
                context.setLineWidth(1.2);
                context.drawGraphicsPath(diamond,
                    CDrawContext::kPathStroked);
            }
        }
        context.setFrameColor(color(0x596166));
        context.setLineWidth(1.4);
        context.drawRect(plot, kDrawStroked);
    }

    void drawSurfacePage(CDrawContext& context)
    {
        context.setFillColor(color(0x090909));
        context.drawRect(kField, kDrawFilled);
        context.setFrameColor(color(0x555555));
        context.drawRect(kField, kDrawStroked);
        drawButton(context, surfaceEditRect(),
            surfaceEdit ? "EDIT" : "PLAY", surfaceEdit);
        drawButton(context, surfaceEnableRect(),
            snapshot.surface.enabled ? "ON" : "OFF",
            snapshot.surface.enabled != 0u);
        drawButton(context, surfaceAddRect(), "ADD", false);
        drawButton(context, surfaceDeleteRect(), "DEL", false);
        drawButton(context, surfaceCaptureRect(), "CAP", false);
        drawButton(context, surfacePopRect(), "POP", false);
        drawMenuBox(context, surfacePresetRect(),
            menuValue(MenuKind::SurfacePreset));
        drawMenuBox(context, surfaceCurveRect(),
            std::string("CURVE  ") + menuValue(MenuKind::SurfaceCurve));
        drawButton(context, surfaceFocusMinusRect(), "-", false);
        drawButton(context, surfaceFocusPlusRect(), "+", false);
        drawButton(context, surfaceGlideMinusRect(), "-", false);
        drawButton(context, surfaceGlidePlusRect(), "+", false);
        text(context, "FOCUS", kField.left + 132.0,
            kField.top + 40.0, 48.0, style.label);
        char focus[24] {};
        std::snprintf(focus, sizeof(focus), "%.2f", snapshot.surface.focus);
        text(context, focus, kField.left + 202.0,
            kField.top + 40.0, 34.0, style.value);
        text(context, "GLIDE", kField.left + 278.0,
            kField.top + 40.0, 46.0, style.label);
        char glide[32] {};
        if (snapshot.surface.glideMs < 0.5f)
            std::snprintf(glide, sizeof(glide), "%s", "OFF");
        else std::snprintf(glide, sizeof(glide), "%.0f MS",
            snapshot.surface.glideMs);
        text(context, glide, kField.left + 350.0,
            kField.top + 40.0, 68.0, style.value);

        drawSurfaceVoronoi(context);
        char status[160] {};
        std::snprintf(status, sizeof(status),
            "T %.3f %.3f   /   A %.3f %.3f   /   %u CELLS   /   %s",
            snapshot.params.surfaceX, snapshot.params.surfaceY,
            snapshot.effectiveSurfaceX, snapshot.effectiveSurfaceY,
            snapshot.surface.cellCount,
            snapshot.surface.cellCount < 2u ? "ADD TWO CELLS TO ENABLE"
                : (snapshot.surface.enabled ? "INTERPOLATING" : "BYPASSED"));
        const auto plot = surfacePlotRect();
        text(context, status, plot.left + 8.0, plot.bottom - 18.0,
            plot.getWidth() - 16.0, style.value);
    }

    bool handleSurfaceMouseDown(const CPoint& point)
    {
        if (contains(surfaceEditRect(), point)) {
            surfaceEdit = !surfaceEdit;
            invalid();
            return true;
        }
        if (contains(surfaceEnableRect(), point)) {
            surfaceAction(AmbiStochasticSurfaceAction::ToggleEnabled, -1);
            return true;
        }
        if (contains(surfaceAddRect(), point)) {
            if (surfaceAction(AmbiStochasticSurfaceAction::Add, -1))
                selectedSurfaceCell = static_cast<int>(
                    snapshot.surface.cellCount) - 1;
            return true;
        }
        if (contains(surfaceDeleteRect(), point)) {
            surfaceAction(AmbiStochasticSurfaceAction::Remove,
                selectedSurfaceCell);
            return true;
        }
        if (contains(surfaceCaptureRect(), point)) {
            surfaceAction(AmbiStochasticSurfaceAction::Capture,
                selectedSurfaceCell);
            return true;
        }
        if (contains(surfacePresetRect(), point)) {
            openMenu(MenuKind::SurfacePreset, surfacePresetRect());
            return true;
        }
        if (contains(surfaceCurveRect(), point)) {
            openMenu(MenuKind::SurfaceCurve, surfaceCurveRect());
            return true;
        }
        if (contains(surfaceFocusMinusRect(), point)
            || contains(surfaceFocusPlusRect(), point)) {
            const double scale = contains(surfaceFocusMinusRect(), point)
                ? 0.8 : 1.25;
            surfaceAction(AmbiStochasticSurfaceAction::SetFocus, -1,
                std::clamp(snapshot.surface.focus * scale, 0.25, 8.0));
            return true;
        }
        if (contains(surfaceGlideMinusRect(), point)
            || contains(surfaceGlidePlusRect(), point)) {
            const int direction = contains(surfaceGlideMinusRect(), point)
                ? -1 : 1;
            surfaceAction(AmbiStochasticSurfaceAction::SetGlide, -1,
                s3g::parameterSurfaceSteppedGlide(
                    snapshot.surface.glideMs, direction));
            return true;
        }
        if (contains(surfacePlotRect(), point)) {
            if (surfaceEdit) {
                const int cell = hitSurfaceCell(point);
                if (cell >= 0) {
                    selectedSurfaceCell = cell;
                    dragSurfaceCell = cell;
                }
            } else {
                dragSurfaceCursor = true;
                surfaceXEdit.begin(kSurfaceXParamId);
                surfaceYEdit.begin(kSurfaceYParamId);
                updateSurfacePosition(point, true);
            }
            invalid();
            return true;
        }
        return contains(kField, point);
    }

    void updateSurfacePosition(const CPoint& point, bool cursor)
    {
        const auto plot = surfacePlotRect();
        const double x = std::clamp(
            (point.x - plot.left) / plot.getWidth(), 0.0, 1.0);
        const double y = std::clamp(
            (plot.bottom - point.y) / plot.getHeight(), 0.0, 1.0);
        if (cursor) {
            surfaceXEdit.set(x);
            surfaceYEdit.set(y);
            refreshSnapshot();
            invalid();
        } else {
            surfaceAction(AmbiStochasticSurfaceAction::MoveCell,
                dragSurfaceCell, x, y);
        }
    }

    void drawInset(CDrawContext& context, const CRect& bounds,
                   const std::string& title)
    {
        context.setFillColor(color(0x101010));
        context.drawRect(bounds, kDrawFilled);
        context.setFrameColor(color(0x464646));
        context.setLineWidth(1.0);
        context.drawRect(bounds, kDrawStroked);
        text(context, title, bounds.left + 8.0, bounds.top + 6.0,
            bounds.getWidth() - 16.0, style.value);
    }

    void drawRateMeter(CDrawContext& context, const std::string& label,
                       float value, const CRect& bounds)
    {
        value = std::clamp(value, 0.0f, 1.0f);
        text(context, label, bounds.left, bounds.top - 3.0,
            70.0, style.value);
        const auto track = rect(bounds.left + 72.0, bounds.top,
            bounds.getWidth() - 72.0, 8.0);
        context.setFillColor(color(0x202020));
        context.drawRect(track, kDrawFilled);
        context.setFrameColor(color(0x444444));
        context.drawRect(track, kDrawStroked);
        context.setFillColor(color(0xbdbdbd,
            alphaByte(0.30 + value * 0.62)));
        context.drawRect(rect(track.left + 1.0, track.top + 1.0,
            value * (track.getWidth() - 2.0), track.getHeight() - 2.0),
            kDrawFilled);
    }

    CPoint projectDirection(const s3g::Vec3& direction) const
    {
        const CPoint center(kField.left + 164.0, kField.top + 216.0);
        constexpr double radius = 126.0;
        const float cameraAz = static_cast<float>(
            viewAzimuth * s3g::kPi / 180.0);
        const float cameraEl = static_cast<float>(
            viewElevation * s3g::kPi / 180.0);
        const float x1 = std::cos(cameraAz) * direction.x
            - std::sin(cameraAz) * direction.y;
        const float y1 = std::sin(cameraAz) * direction.x
            + std::cos(cameraAz) * direction.y;
        const float y2 = std::cos(cameraEl) * y1
            + std::sin(cameraEl) * direction.z;
        return CPoint(center.x + x1 * radius, center.y - y2 * radius);
    }

    void drawListenerPage(CDrawContext& context)
    {
        context.setFillColor(color(0x090909));
        context.drawRect(kField, kDrawFilled);
        context.setFrameColor(color(0x555555));
        context.drawRect(kField, kDrawStroked);
        text(context,
            "AMBISONIC AUDITORY BODY / NEGATIVE-FEEDBACK VISCOSITY",
            kField.left + 10.0, kField.top + 10.0,
            kField.getWidth() - 20.0, style.value);

        const uint32_t voices = std::clamp<uint32_t>(
            snapshot.params.voices, 1u, kVoiceCount);
        selectedVoice = std::min<uint32_t>(selectedVoice, voices - 1u);
        constexpr uint32_t pickupCount = s3g::kAmbiFieldListenerMaxLobes;
        const auto listenMode = snapshot.params.fieldListenMode;
        const bool listening = listenMode
            != s3g::AmbiStochasticListenMode::Off;
        const bool roaming = listenMode
            == s3g::AmbiStochasticListenMode::Roaming;
        const bool diffuse = listenMode
            == s3g::AmbiStochasticListenMode::Diffuse;
        const uint32_t primary = snapshot.listenerPickup[selectedVoice]
            % pickupCount;
        const uint32_t secondary =
            snapshot.listenerSecondaryPickup[selectedVoice] % pickupCount;
        const float pickupMix = std::clamp(
            snapshot.listenerPickupMix[selectedVoice], 0.0f, 1.0f);
        const uint32_t readEar = roaming && pickupMix >= 0.5f
            ? secondary : primary;
        const float activity = std::clamp(
            snapshot.listenerActivity, 0.0f, 1.0f);
        const float energy = std::clamp(
            snapshot.listenerEnergy[selectedVoice], 0.0f, 1.0f);
        const float capture = std::clamp(
            snapshot.listenerCapture[selectedVoice], 0.0f, 1.0f);
        const float mutation = std::clamp(
            snapshot.listenerMutationRate[selectedVoice], 0.0f, 1.0f);
        const float evolution = std::clamp(
            snapshot.listenerEvolutionRate[selectedVoice], 0.0f, 1.0f);
        const float fieldClock = std::clamp(
            snapshot.listenerFieldClockRate[selectedVoice], 0.0f, 1.0f);
        const float cascade = std::clamp(
            snapshot.listenerCascadeRate[selectedVoice], 0.0f, 1.0f);

        const CPoint bodyCenter(kField.left + 164.0, kField.top + 216.0);
        constexpr double bodyRadius = 126.0;
        context.setFrameColor(color(0x303030));
        context.setLineWidth(0.8);
        context.drawEllipse(rect(bodyCenter.x - bodyRadius,
            bodyCenter.y - bodyRadius, bodyRadius * 2.0,
            bodyRadius * 2.0), kDrawStroked);
        constexpr std::array<s3g::Vec3, 6u> axes {{
            { -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f },
            { 0.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
            { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 1.0f },
        }};
        context.setFrameColor(CColor(26u, 235u, 77u, alphaByte(0.42)));
        for (uint32_t axis = 0u; axis < 3u; ++axis)
            context.drawLine(projectDirection(axes[axis * 2u]),
                projectDirection(axes[axis * 2u + 1u]));

        const auto& directions = s3g::ambiFieldListenerCubeDirections();
        std::array<CPoint, pickupCount> ears {};
        std::array<uint32_t, pickupCount> counts {};
        float peakEnvelope = 0.0f;
        for (uint32_t ear = 0u; ear < pickupCount; ++ear) {
            ears[ear] = projectDirection(directions[ear]);
            peakEnvelope = std::max(
                peakEnvelope, snapshot.listenerEnvelope[ear]);
        }
        if (listening) {
            if (diffuse) counts.fill(voices);
            else {
                for (uint32_t voice = 0u; voice < voices; ++voice) {
                    const uint32_t first = snapshot.listenerPickup[voice]
                        % pickupCount;
                    const uint32_t second =
                        snapshot.listenerSecondaryPickup[voice] % pickupCount;
                    ++counts[roaming
                            && snapshot.listenerPickupMix[voice] >= 0.5f
                        ? second : first];
                }
            }
        }

        const auto& selectedPoint = snapshot.points[selectedVoice];
        const CPoint voicePoint = projectDirection(s3g::directionFromAed(
            selectedPoint.azimuthDeg, selectedPoint.elevationDeg));
        auto drawRoute = [&](uint32_t ear, float strength) {
            strength = std::clamp(strength, 0.0f, 1.0f);
            context.setFrameColor(color(0xc7c7c7,
                alphaByte(0.12 + strength * 0.58)));
            context.setLineWidth(0.7 + strength * 1.7);
            context.drawLine(voicePoint, ears[ear % pickupCount]);
        };
        if (listening) {
            if (diffuse) {
                for (uint32_t ear = 0u; ear < pickupCount; ++ear)
                    drawRoute(ear, 0.24f + 0.20f
                        * snapshot.listenerEnvelope[ear]
                        / std::max(0.0001f, peakEnvelope));
            } else if (roaming) {
                drawRoute(primary, 1.0f - pickupMix);
                drawRoute(secondary, pickupMix);
            } else drawRoute(primary, 1.0f);
        }

        for (uint32_t ear = 0u; ear < pickupCount; ++ear) {
            const float earEnergy = std::clamp(
                snapshot.listenerEnvelope[ear]
                    / std::max(0.0001f, peakEnvelope),
                0.0f, 1.0f);
            const bool routed = listening && (diffuse || ear == readEar
                || (roaming && (ear == primary || ear == secondary)));
            const double halo = 7.0 + std::sqrt(earEnergy) * 16.0;
            context.setFillColor(color(0xc7c7c7,
                alphaByte(0.03 + earEnergy * 0.18)));
            context.drawEllipse(rect(ears[ear].x - halo,
                ears[ear].y - halo, halo * 2.0, halo * 2.0),
                kDrawFilled);
            const double radius = routed ? 7.0 : 5.5;
            auto diamond = owned(context.createGraphicsPath());
            if (diamond) {
                diamond->beginSubpath(CPoint(ears[ear].x,
                    ears[ear].y - radius));
                diamond->addLine(CPoint(ears[ear].x + radius,
                    ears[ear].y));
                diamond->addLine(CPoint(ears[ear].x,
                    ears[ear].y + radius));
                diamond->addLine(CPoint(ears[ear].x - radius,
                    ears[ear].y));
                diamond->closeSubpath();
                context.setFillColor(color(routed ? 0xd8d8d8 : 0x767676,
                    alphaByte(0.30 + earEnergy * 0.62)));
                context.setFrameColor(color(0xf0f0f0,
                    routed ? alphaByte(0.86) : 0u));
                context.setLineWidth(1.0);
                context.drawGraphicsPath(diamond,
                    routed ? CDrawContext::kPathFilledEvenOdd
                           : CDrawContext::kPathFilled);
                if (routed) context.drawGraphicsPath(
                    diamond, CDrawContext::kPathStroked);
            }
            char earLabel[24] {};
            std::snprintf(earLabel, sizeof(earLabel), "E%u %02u",
                ear + 1u, counts[ear]);
            text(context, earLabel, ears[ear].x + 8.0,
                ears[ear].y - 6.0, 54.0, style.value);
        }

        constexpr double voiceSize = 11.0;
        context.setFillColor(pointColor(selectedPoint, true,
            alphaByte(0.96)));
        context.setFrameColor(color(0xf0f0f0, alphaByte(0.84)));
        context.drawRect(rect(voicePoint.x - voiceSize * 0.5,
            voicePoint.y - voiceSize * 0.5, voiceSize, voiceSize),
            kDrawFilledAndStroked);
        char voiceLabel[12] {};
        std::snprintf(voiceLabel, sizeof(voiceLabel), "V%02u",
            selectedVoice + 1u);
        text(context, voiceLabel, voicePoint.x + 8.0,
            voicePoint.y - 7.0, 40.0, style.value);

        const auto decision = rect(kField.left + 354.0,
            kField.top + 29.0, 200.0, 128.0);
        drawInset(context, decision, "AUDITORY CAPTURE");
        std::string route = "--";
        char routeBuffer[32] {};
        if (diffuse) route = "ALL EARS";
        else if (roaming) {
            std::snprintf(routeBuffer, sizeof(routeBuffer),
                "E%u > E%u %02.0f%%", primary + 1u,
                secondary + 1u, pickupMix * 100.0f);
            route = routeBuffer;
        } else if (listening) {
            std::snprintf(routeBuffer, sizeof(routeBuffer), "E%u",
                readEar + 1u);
            route = routeBuffer;
        }
        char line[128] {};
        if (listening)
            std::snprintf(line, sizeof(line), "V%02u / %s  ENERGY %3.0f%%",
                selectedVoice + 1u, route.c_str(), energy * 100.0f);
        else std::snprintf(line, sizeof(line), "V%02u / LISTENER OFF",
            selectedVoice + 1u);
        text(context, line, decision.left + 8.0,
            decision.top + 25.0, 184.0, style.value);
        std::snprintf(line, sizeof(line), listening
                ? "CAPTURE %3.0f%%   EVOLVE x%.2f"
                : "CAPTURE AMOUNT %3.0f%%",
            listening ? capture * 100.0f
                      : snapshot.params.fieldListenAmount * 100.0f,
            evolution);
        text(context, line, decision.left + 8.0,
            decision.top + 42.0, 184.0, style.value);
        std::snprintf(line, sizeof(line), "G%u > G%u   %s",
            snapshot.currentGenerator[selectedVoice] + 1u,
            snapshot.nextGenerator[selectedVoice] + 1u,
            listening && capture > 0.08f ? "STATE HELD" : "STATE OPEN");
        text(context, line, decision.left + 8.0,
            decision.top + 59.0, 184.0, style.value);
        const double axisLeft = decision.left + 17.0;
        const double axisRight = decision.right - 17.0;
        const double axisY = decision.top + 91.0;
        context.setFrameColor(color(0x505050));
        context.drawLine(CPoint(axisLeft, axisY), CPoint(axisRight, axisY));
        const uint32_t current = std::min<uint32_t>(
            snapshot.currentGenerator[selectedVoice], 3u);
        const uint32_t next = std::min<uint32_t>(
            snapshot.nextGenerator[selectedVoice], 3u);
        for (uint32_t generator = 0u; generator < 4u; ++generator) {
            const double x = axisLeft + (axisRight - axisLeft)
                * generator / 3.0;
            const auto cell = rect(x - 9.0, axisY - 7.0, 18.0, 14.0);
            context.setFillColor(color(generator == next
                    ? 0xa4a4a4 : 0x252525,
                generator == next ? alphaByte(0.82) : 255u));
            context.drawRect(cell, kDrawFilled);
            context.setFrameColor(color(generator == current
                ? 0xe0e0e0 : 0x5a5a5a));
            context.drawRect(cell, kDrawStroked);
            char generatorLabel[8] {};
            std::snprintf(generatorLabel, sizeof(generatorLabel), "G%u",
                generator + 1u);
            textInRect(context, generatorLabel, cell,
                color(generator == next ? 0x161616 : 0xc5c5c5),
                kCenterText, tinyFont);
        }
        std::snprintf(line, sizeof(line), "LAW %s   MUTATE x%.2f",
            kSelectionItems[std::clamp<uint32_t>(
                static_cast<uint32_t>(snapshot.params.selection), 0u, 5u)],
            mutation);
        text(context, line, decision.left + 8.0,
            decision.top + 109.0, 184.0, style.value);

        const auto mutationPanel = rect(kField.left + 354.0,
            kField.top + 166.0, 200.0, 73.0);
        drawInset(context, mutationPanel, "SECOND-ORDER FRICTION");
        std::snprintf(line, sizeof(line), "A x%.2f", mutation);
        drawRateMeter(context, line, mutation,
            rect(mutationPanel.left + 8.0, mutationPanel.top + 29.0,
                184.0, 8.0));
        std::snprintf(line, sizeof(line), "T x%.2f", mutation);
        drawRateMeter(context, line, mutation,
            rect(mutationPanel.left + 8.0, mutationPanel.top + 49.0,
                184.0, 8.0));

        const auto renewal = rect(kField.left + 354.0,
            kField.top + 248.0, 200.0, 72.0);
        drawInset(context, renewal,
            snapshot.fieldActive[selectedVoice]
                ? "TEMPORAL HOLD / ACTIVE" : "TEMPORAL HOLD / REST");
        std::snprintf(line, sizeof(line), "FIELD x%.2f", fieldClock);
        drawRateMeter(context, line, fieldClock,
            rect(renewal.left + 8.0, renewal.top + 29.0, 184.0, 8.0));
        std::snprintf(line, sizeof(line), "CASC x%.2f", cascade);
        drawRateMeter(context, line, cascade,
            rect(renewal.left + 8.0, renewal.top + 49.0, 184.0, 8.0));

        const auto summary = rect(kField.left + 354.0,
            kField.top + 329.0, 200.0, 64.0);
        drawInset(context, summary, "LISTENER STATE");
        std::snprintf(line, sizeof(line), "%s   EARS %3.0f%%",
            kListenerItems[std::clamp<uint32_t>(
                static_cast<uint32_t>(listenMode), 0u, 4u)],
            activity * 100.0f);
        text(context, line, summary.left + 8.0,
            summary.top + 26.0, 184.0, style.value);
        std::snprintf(line, sizeof(line), "CAPTURE %3.0f%%   CUBE 8 FIXED",
            snapshot.params.fieldListenAmount * 100.0f);
        text(context, line, summary.left + 8.0,
            summary.top + 43.0, 184.0, style.value);

        const auto voiceControl = listenerVoiceRect();
        context.setFillColor(style.strip);
        context.drawRect(voiceControl, kDrawFilled);
        context.setFrameColor(style.grid);
        context.drawRect(voiceControl, kDrawStroked);
        textInRect(context, "<", rect(voiceControl.left,
            voiceControl.top, 25.0, voiceControl.getHeight()), style.value);
        textInRect(context, voiceLabel, rect(voiceControl.left + 25.0,
            voiceControl.top, 42.0, voiceControl.getHeight()), style.value);
        textInRect(context, ">", rect(voiceControl.right - 25.0,
            voiceControl.top, 25.0, voiceControl.getHeight()), style.value);
        text(context, "VOICE", voiceControl.left,
            voiceControl.top - 16.0, 60.0, style.value);
        std::snprintf(line, sizeof(line), "%s / EARS %3.0f%% / CAPTURE %3.0f%%",
            kListenerItems[std::clamp<uint32_t>(
                static_cast<uint32_t>(listenMode), 0u, 4u)],
            activity * 100.0f, snapshot.params.fieldListenAmount * 100.0f);
        text(context, line, voiceControl.left + 112.0,
            voiceControl.top + 2.0, 340.0, style.value);
        text(context, "SPATIAL LINK NONE / TOPOLOGY UNCHANGED",
            kField.left + 10.0, kField.bottom - 18.0,
            kField.getWidth() - 20.0, color(0xd7d7d7),
            kLeftText, tinyFont);
    }

    void drawPressure(CDrawContext& context)
    {
        drawPanel(context, kWavePanel, "PRESSURE CURVE");
        const uint32_t current = snapshot.currentGenerator[selectedVoice] + 1u;
        const uint32_t next = snapshot.nextGenerator[selectedVoice] + 1u;
        char status[128] {};
        std::snprintf(status, sizeof(status),
            "G%u > G%u   A1 +/-%0.3f   T1 +/-%0.1f SAMP",
            current, next, snapshot.amplitudeBarrier,
            snapshot.durationBarrier);
        context.setFont(font);
        const double statusWidth = context.getStringWidth(status);
        text(context, status, kWavePanel.right - statusWidth - 8.0,
            kWavePanel.top + 5.0, statusWidth + 1.0, style.value);

        context.setFillColor(color(0x090909));
        context.drawRect(kWave, kDrawFilled);
        context.drawRect(kHistory, kDrawFilled);
        context.setFrameColor(color(0x555555));
        context.setLineWidth(1.0);
        context.drawRect(kWave, kDrawStroked);
        context.drawRect(kHistory, kDrawStroked);
        context.setFrameColor(color(0x292929));
        context.drawLine(CPoint(kWave.left + 1.0, kWave.getCenter().y),
            CPoint(kWave.right - 1.0, kWave.getCenter().y));
        for (uint32_t division = 1u; division < 4u; ++division) {
            const double x = kWave.left + kWave.getWidth()
                * division / 4.0;
            context.drawLine(CPoint(x, kWave.top + 1.0),
                CPoint(x, kWave.bottom - 1.0));
        }

        auto currentCurve = owned(context.createGraphicsPath());
        auto nextCurve = owned(context.createGraphicsPath());
        if (currentCurve && nextCurve) {
            for (uint32_t sample = 0u;
                 sample < kStochasticGuiWaveSamples; ++sample) {
                const double x = kWave.left + static_cast<double>(sample)
                    / static_cast<double>(kStochasticGuiWaveSamples - 1u)
                    * kWave.getWidth();
                const CPoint first(x, kWave.getCenter().y
                    - snapshot.currentWaveform[sample]
                        * kWave.getHeight() * 0.42);
                const CPoint second(x, kWave.getCenter().y
                    - snapshot.nextWaveform[sample]
                        * kWave.getHeight() * 0.42);
                if (sample == 0u) {
                    currentCurve->beginSubpath(first);
                    nextCurve->beginSubpath(second);
                } else {
                    currentCurve->addLine(first);
                    nextCurve->addLine(second);
                }
            }
            context.setFrameColor(color(0x686868, alphaByte(0.72)));
            context.setLineWidth(0.8);
            context.drawGraphicsPath(currentCurve,
                CDrawContext::kPathStroked);
            context.setFrameColor(pointColor(
                snapshot.points[selectedVoice], true, alphaByte(0.92)));
            context.setLineWidth(1.25);
            context.drawGraphicsPath(nextCurve,
                CDrawContext::kPathStroked);
        }

        const uint32_t count = std::min<uint32_t>(
            snapshot.breakpointCount, s3g::kAmbiStochasticMaxBreakpoints);
        for (uint32_t point = 0u; point < count; ++point) {
            const double x = kWave.left
                + snapshot.breakpointPosition[point] * kWave.getWidth();
            const double y = kWave.getCenter().y
                - snapshot.breakpointAmplitude[point]
                    * kWave.getHeight() * 0.42;
            context.setFillColor(color(0xc0c0c0, alphaByte(0.82)));
            context.drawRect(rect(x - 1.5, y - 1.5, 3.0, 3.0),
                kDrawFilled);
        }

        text(context, "SELECTION HISTORY", kHistory.left + 8.0,
            kHistory.top + 7.0, 180.0, style.value);
        const double cellY = kHistory.top + 27.0;
        const double cellWidth = (kHistory.getWidth() - 16.0)
            / s3g::kAmbiStochasticHistorySize;
        static constexpr uint32_t shades[] {
            0x4a4a4a, 0x707070, 0x999999, 0xc8c8c8,
        };
        for (uint32_t offset = 0u;
             offset < s3g::kAmbiStochasticHistorySize; ++offset) {
            const uint32_t index = (snapshot.historyCursor + offset)
                % s3g::kAmbiStochasticHistorySize;
            const uint32_t generator = std::min<uint32_t>(
                snapshot.history[index], 3u);
            context.setFillColor(color(shades[generator], alphaByte(0.86)));
            context.drawRect(rect(kHistory.left + 8.0
                    + offset * cellWidth, cellY,
                std::max(1.0, cellWidth - 2.0), 12.0), kDrawFilled);
        }
    }

    void setViewPreset(int mode)
    {
        viewMode = mode;
        if (mode == 0) {
            viewAzimuth = 90.0;
            viewElevation = 0.0;
        } else if (mode == 1) {
            viewAzimuth = 90.0;
            viewElevation = 90.0;
        } else {
            viewAzimuth = 38.0;
            viewElevation = 32.0;
        }
        storeViewState();
    }

    void storeViewState()
    {
        if (config.callbacks.setViewState)
            config.callbacks.setViewState(config.callbacks.context,
                viewMode, viewAzimuth, viewElevation, viewZoom);
        invalid();
    }

    bool isDragging() const
    {
        return dragParam != 0u || dragView || dragSurfaceCell >= 0
            || dragSurfaceCursor;
    }

    void stopDragging()
    {
        parameterEdit.end();
        surfaceXEdit.end();
        surfaceYEdit.end();
        dragParam = 0u;
        dragView = false;
        dragSurfaceCell = -1;
        dragSurfaceCursor = false;
    }

    void selectPreset(bool save)
    {
        if ((save && !config.callbacks.savePreset)
            || (!save && !config.callbacks.loadPreset)) return;
        foundation::FileDialogOptions options;
        options.save = save;
        options.title = save ? "Save Stochastic preset"
                             : "Load Stochastic preset";
        options.extensionDescription = "s3g Stochastic preset";
        options.extension = "s3gstochastic";
        options.defaultSaveName = "s3g-stochastic-preset.s3gstochastic";
        options.initialDirectory = foundation::presetDirectory(
            "Ambi Stochastic Encoder");
        const std::string path = foundation::runFileDialog(getFrame(), options);
        if (path.empty()) return;
        const bool succeeded = save
            ? config.callbacks.savePreset(config.callbacks.context, path.c_str())
            : config.callbacks.loadPreset(config.callbacks.context, path.c_str());
        if (succeeded) {
            refreshSnapshot();
            invalid();
        }
    }

    AmbiStochasticEditorConfig config;
    AmbiStochasticEditorSnapshot snapshot {};
    const foundation::Palette& style = foundation::palette();
    SharedPointer<CFontDesc> font;
    SharedPointer<CFontDesc> titleFont;
    SharedPointer<CFontDesc> tinyFont;
    SharedPointer<CVSTGUITimer> refreshTimer;
    foundation::ParameterEditSession parameterEdit;
    foundation::ParameterEditSession surfaceXEdit;
    foundation::ParameterEditSession surfaceYEdit;
    uint32_t selectedVoice = 0u;
    int fieldPage = 0;
    int viewMode = 0;
    double viewAzimuth = 90.0;
    double viewElevation = 0.0;
    double viewZoom = 1.0;
    bool surfaceEdit = true;
    int selectedSurfaceCell = -1;
    int dragSurfaceCell = -1;
    bool dragSurfaceCursor = false;
    uint32_t dragParam = 0u;
    bool dragView = false;
    CPoint lastDrag {};
    MenuKind activeMenu = MenuKind::None;
    int menuHover = -1;
    CRect dropdownBounds {};
};

} // namespace

class AmbiStochasticEditor final : public foundation::EditorHost {
public:
    AmbiStochasticEditor(const AmbiStochasticEditorConfig& editorConfig,
                         uint32_t requestedWidth,
                         uint32_t requestedHeight)
        : EditorHost(editorConfig.nativeWidth, editorConfig.nativeHeight,
            requestedWidth, requestedHeight)
        , config(editorConfig)
    {
    }

    bool build()
    {
        return ready() && attach(new AmbiStochasticView(config));
    }

private:
    AmbiStochasticEditorConfig config;
};

AmbiStochasticEditor* createAmbiStochasticEditor(
    const AmbiStochasticEditorConfig& config, uint32_t width,
    uint32_t height)
{
    auto* editor = new (std::nothrow) AmbiStochasticEditor(
        config, width, height);
    if (!editor) return nullptr;
    if (!editor->build()) {
        delete editor;
        return nullptr;
    }
    return editor;
}

void destroyAmbiStochasticEditor(AmbiStochasticEditor* editor)
{
    delete editor;
}

bool setAmbiStochasticEditorParent(
    AmbiStochasticEditor* editor, void* nativeParent)
{
    return editor && editor->setParent(nativeParent);
}

bool setAmbiStochasticEditorSize(
    AmbiStochasticEditor* editor, uint32_t width, uint32_t height)
{
    return editor && editor->setSize(width, height);
}

bool setAmbiStochasticEditorVisible(
    AmbiStochasticEditor* editor, bool visible)
{
    return editor && editor->setVisible(visible);
}

} // namespace s3g::portable_gui
