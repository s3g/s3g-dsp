// Literal cartography Cocoa geometry, controls, drawing order and pointer algorithms.
namespace map_encoder_canvas {
namespace drawing = s3g::portable_gui::map_drawing;
namespace foundation = s3g::portable_gui::foundation;
using Kind = s3g::clap_gui::ParamEventKind;
constexpr clap_id kFactoryPresetMenuId = 0x7ffffff0u;
constexpr uint32_t kFactoryPresetCount = 9u;

enum class CartographyPanel : uint8_t {
    Output,
    Cartography,
    Propagation,
    Listener,
    Process,
    SelectedSite,
    Guide,
};

enum class CartographyControlKind : uint8_t {
    Slider,
    Menu,
    Toggle,
};

struct CartographyControl {
    clap_id id;
    const char* label;
    CartographyPanel panel;
    uint32_t row;
    CartographyControlKind kind;
};

constexpr std::array<CartographyControl, kParamCount> kGuiControls {{
    { kOrderParamId, "ORDER", CartographyPanel::Output, 1u,
        CartographyControlKind::Menu },
    { kOutputParamId, "OUT", CartographyPanel::Output, 0u,
        CartographyControlKind::Slider },

    { kSitesParamId, "SITES", CartographyPanel::Output, 2u,
        CartographyControlKind::Slider },
    { kLayoutParamId, "LAYOUT", CartographyPanel::Cartography, 0u,
        CartographyControlKind::Menu },
    { kStereoMapParamId, "STEREO", CartographyPanel::Cartography, 1u,
        CartographyControlKind::Menu },
    { kMapScaleParamId, "SCALE", CartographyPanel::Cartography, 2u,
        CartographyControlKind::Slider },
    { kNetworkSpreadParamId, "NETWORK", CartographyPanel::Cartography, 3u,
        CartographyControlKind::Slider },

    { kTimeReferenceParamId, "TIME REF", CartographyPanel::Propagation, 0u,
        CartographyControlKind::Menu },
    { kPropagationParamId, "PROP TIME", CartographyPanel::Propagation, 1u,
        CartographyControlKind::Slider },
    { kAirParamId, "AIR", CartographyPanel::Propagation, 2u,
        CartographyControlKind::Slider },
    { kDistanceLossParamId, "DIST LOSS", CartographyPanel::Propagation, 3u,
        CartographyControlKind::Slider },
    { kCarryParamId, "CARRY", CartographyPanel::Propagation, 4u,
        CartographyControlKind::Slider },
    { kTurbulenceParamId, "TURB", CartographyPanel::Propagation, 5u,
        CartographyControlKind::Slider },

    { kListenModeParamId, "MODE", CartographyPanel::Listener, 0u,
        CartographyControlKind::Menu },
    { kListenerAmountParamId, "AMOUNT", CartographyPanel::Listener, 1u,
        CartographyControlKind::Slider },
    { kListenerXParamId, "X", CartographyPanel::Listener, 2u,
        CartographyControlKind::Slider },
    { kListenerYParamId, "Y", CartographyPanel::Listener, 3u,
        CartographyControlKind::Slider },
    { kListenerZParamId, "Z", CartographyPanel::Listener, 4u,
        CartographyControlKind::Slider },

    { kMacroEngineParamId, "ENGINE", CartographyPanel::Process, 0u,
        CartographyControlKind::Menu },
    { kShredCircuitParamId, "CIRCUIT", CartographyPanel::Process, 1u,
        CartographyControlKind::Menu },
    { kFractureProcessorParamId, "PROCESSOR", CartographyPanel::Process, 1u,
        CartographyControlKind::Menu },
    { kMacroParamId, "MACRO", CartographyPanel::Process, 2u,
        CartographyControlKind::Slider },
    { kColorParamId, "COLOR", CartographyPanel::Process, 3u,
        CartographyControlKind::Slider },
    { kMemoryParamId, "MEMORY", CartographyPanel::Process, 4u,
        CartographyControlKind::Slider },
    { kProcessMixParamId, "MIX", CartographyPanel::Process, 5u,
        CartographyControlKind::Slider },
    { kMacroMetricParamId, "METRIC", CartographyPanel::Process, 0u,
        CartographyControlKind::Menu },
    { kSpreadParamId, "SPREAD", CartographyPanel::Process, 1u,
        CartographyControlKind::Slider },
    { kDeviationParamId, "DEVIATE", CartographyPanel::Process, 2u,
        CartographyControlKind::Slider },
    { kSkewParamId, "SKEW", CartographyPanel::Process, 3u,
        CartographyControlKind::Slider },
    { kCenterParamId, "CENTER", CartographyPanel::Process, 4u,
        CartographyControlKind::Slider },

    { kSiteParamId, "SITE", CartographyPanel::SelectedSite, 0u,
        CartographyControlKind::Slider },
    { kSiteEnabledParamId, "ENABLED", CartographyPanel::SelectedSite, 1u,
        CartographyControlKind::Toggle },
    { kSiteXParamId, "X", CartographyPanel::SelectedSite, 2u,
        CartographyControlKind::Slider },
    { kSiteYParamId, "Y", CartographyPanel::SelectedSite, 3u,
        CartographyControlKind::Slider },
    { kSiteZParamId, "Z", CartographyPanel::SelectedSite, 4u,
        CartographyControlKind::Slider },
    { kSiteGainParamId, "GAIN", CartographyPanel::SelectedSite, 5u,
        CartographyControlKind::Slider },
    { kSiteNetworkTrimParamId, "NET TRIM", CartographyPanel::SelectedSite, 6u,
        CartographyControlKind::Slider },

    { kMultipathParamId, "MULTIPATH", CartographyPanel::Guide, 0u,
        CartographyControlKind::Slider },
    { kOcclusionParamId, "OCCLUSION", CartographyPanel::Guide, 1u,
        CartographyControlKind::Slider },
    { kNetworkWeatherParamId, "NET WEATHER", CartographyPanel::Guide, 2u,
        CartographyControlKind::Slider },
    { kRefractionParamId, "REFRACT", CartographyPanel::Guide, 3u,
        CartographyControlKind::Slider },
    { kMotionParamId, "MOTION", CartographyPanel::Guide, 0u,
        CartographyControlKind::Slider },
    { kEcologyParamId, "ECOLOGY", CartographyPanel::Guide, 1u,
        CartographyControlKind::Slider },
    { kHorizonParamId, "HORIZON", CartographyPanel::Guide, 2u,
        CartographyControlKind::Slider },
}};

static_assert(kGuiControls.size() == std::size(kParams));
constexpr s3g::gui_layout::Panel kCartographyOutputInputPanel {
    s3g::gui_layout::PluginClass::ProceduralEncoder,
    s3g::gui_layout::PanelRole::Output,
    { 680.0, 42.0, 320.0, 106.0 }, 36.0, 26.0, 3u,
};
static_assert(s3g::gui_layout::combinedOutputSourceCardinalityControlMatches(
    kCartographyOutputInputPanel,
    s3g::gui_layout::SharedControlRole::SourceCardinality));
static_assert(kGuiControls[2].id == kSitesParamId
    && kGuiControls[2].panel == CartographyPanel::Output
    && kGuiControls[2].row == 2u);

struct FactoryPreset {
    const char* name;
    uint32_t sites;
    uint32_t layout;
    uint32_t stereo;
    uint32_t timeReference;
    float scale;
    float network;
    float propagation;
    float air;
    float distanceLoss;
    float carry;
    float turbulence;
    uint32_t engine;
    uint32_t metric;
    float macro;
    float color;
    float memory;
    float spread;
    float deviation;
    float skew;
    float center;
    float mix;
    uint32_t listenMode;
    float listenAmount;
    float listenerX;
    float listenerY;
    float listenerZ;
    float multipath;
    float occlusion;
    float networkWeather;
    float refraction;
    float motion;
    float ecology;
    float horizon;
    uint32_t shredCircuit;
    uint32_t fractureProcessor;
};

constexpr std::array<FactoryPreset, kFactoryPresetCount> kFactoryPresets {{
    { "INIT", 12u, 0u, 0u, 0u, 240.0f, 420.0f,
        0.35f, 0.28f, 0.55f, 0.20f, 0.08f,
        1u, 0u, 0.50f, 0.55f, 0.28f, 0.35f, 0.12f, 0.0f, 0.50f, 0.42f,
        0u, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0u, 0u },
    { "PHYSICAL PLAZA", 8u, 0u, 0u, 0u, 55.0f, 0.0f,
        1.0f, 0.08f, 0.36f, 0.08f, 0.02f,
        0u, 3u, 0.18f, 0.40f, 0.10f, 0.18f, 0.02f, 0.0f, 0.50f, 0.0f,
        0u, 0.0f, 0.0f, 0.0f, 0.0f,
        0.18f, 0.05f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0u, 0u },
    { "CITY RELAYS", 16u, 3u, 2u, 0u, 480.0f, 1450.0f,
        0.70f, 0.30f, 0.42f, 0.40f, 0.10f,
        6u, 0u, 0.62f, 0.58f, 0.62f, 0.74f, 0.18f, 0.20f, 0.45f, 0.62f,
        0u, 0.0f, 0.0f, 0.0f, 0.0f,
        0.10f, 0.10f, 0.70f, 0.10f, 0.08f, 0.22f, 0.15f, 1u, 0u },
    { "RIDGE AFTERIMAGE", 12u, 1u, 0u, 1u, 1200.0f, 620.0f,
        1.0f, 0.46f, 0.52f, 0.60f, 0.12f,
        7u, 1u, 0.72f, 0.42f, 0.76f, 0.58f, 0.16f, -0.18f, 0.56f, 0.68f,
        1u, 0.28f, -0.20f, -0.25f, 0.0f,
        0.20f, 0.65f, 0.12f, -0.25f, 0.12f, 0.15f, 0.25f, 0u, 2u },
    { "WATERFRONT CARRY", 18u, 4u, 1u, 0u, 1800.0f, 880.0f,
        0.85f, 0.18f, 0.32f, 0.85f, 0.16f,
        5u, 3u, 0.42f, 0.68f, 0.48f, 0.66f, 0.10f, 0.16f, 0.62f, 0.54f,
        1u, 0.32f, 0.0f, -0.45f, 0.0f,
        0.42f, 0.10f, 0.20f, 0.55f, 0.18f, 0.12f, 0.35f, 1u, 0u },
    { "CORRIDOR BENDS", 10u, 2u, 2u, 1u, 240.0f, 280.0f,
        0.45f, 0.15f, 0.60f, 0.25f, 0.05f,
        2u, 2u, 0.66f, 0.48f, 0.38f, 0.58f, 0.22f, -0.34f, 0.46f, 0.58f,
        2u, 0.40f, 0.18f, 0.0f, 0.0f,
        0.58f, 0.38f, 0.15f, 0.10f, 0.20f, 0.28f, 0.12f, 2u, 0u },
    { "SHRED GRID", 24u, 3u, 2u, 0u, 700.0f, 1800.0f,
        0.50f, 0.38f, 0.65f, 0.30f, 0.24f,
        3u, 0u, 0.78f, 0.74f, 0.34f, 0.92f, 0.48f, 0.28f, 0.36f, 0.70f,
        0u, 0.0f, 0.0f, 0.0f, 0.0f,
        0.18f, 0.18f, 0.78f, -0.15f, 0.45f, 0.18f, 0.32f, 7u, 0u },
    { "FRACTURE HORIZON", 20u, 4u, 0u, 1u, 1500.0f, 1550.0f,
        1.0f, 0.62f, 0.48f, 0.65f, 0.32f,
        4u, 1u, 0.86f, 0.82f, 0.72f, 0.88f, 0.54f, -0.22f, 0.68f, 0.72f,
        2u, 0.55f, 0.38f, 0.18f, 0.0f,
        0.35f, 0.30f, 0.55f, -0.70f, 0.20f, 0.38f, 0.85f, 0u, 4u },
    { "LISTENER DRIFT", 14u, 0u, 0u, 0u, 520.0f, 780.0f,
        0.72f, 0.26f, 0.48f, 0.44f, 0.14f,
        1u, 2u, 0.56f, 0.52f, 0.58f, 0.62f, 0.20f, 0.12f, 0.52f, 0.56f,
        3u, 0.78f, -0.55f, 0.35f, 0.0f,
        0.22f, 0.12f, 0.28f, 0.15f, 0.72f, 0.25f, 0.28f, 0u, 0u },
}};

static_assert(kFactoryPresets[2].engine == 6u);
static_assert(kFactoryPresets[3].engine == 7u);
static_assert(kFactoryPresets[4].engine == 5u);
static_assert(kFactoryPresets[6].engine == 3u
    && kFactoryPresets[6].shredCircuit == 7u);
static_assert(kFactoryPresets[7].engine == 4u
    && kFactoryPresets[7].fractureProcessor == 4u);

drawing::Rect panelRect(CartographyPanel panel)
{
    switch (panel) {
    case CartographyPanel::Output: return drawing::makeRect(680.0, 42.0, 320.0, 106.0);
    case CartographyPanel::Cartography: return drawing::makeRect(680.0, 150.0, 320.0, 164.0);
    case CartographyPanel::Propagation: return drawing::makeRect(680.0, 326.0, 320.0, 190.0);
    case CartographyPanel::Listener: return drawing::makeRect(680.0, 528.0, 320.0, 210.0);
    case CartographyPanel::Process: return drawing::makeRect(1012.0, 42.0, 326.0, 294.0);
    case CartographyPanel::SelectedSite: return drawing::makeRect(1012.0, 348.0, 326.0, 216.0);
    case CartographyPanel::Guide: return drawing::makeRect(1012.0, 576.0, 326.0, 162.0);
    }
    return drawing::Rect{};
}

drawing::Rect cartographyMapRect()
{
    return drawing::makeRect(34.0, 76.0, 620.0, 642.0);
}

double controlY(const CartographyControl& control)
{
    const drawing::Rect panel = panelRect(control.panel);
    return panel.origin.y
        + s3g::gui_layout::kStandardMetrics.firstRowOffset
        + static_cast<double>(control.row)
            * s3g::gui_layout::kStandardMetrics.rowPitch;
}

drawing::Rect controlHitRect(const CartographyControl& control)
{
    const drawing::Rect panel = panelRect(control.panel);
    return drawing::makeRect(panel.origin.x
            + s3g::gui_layout::kStandardMetrics.hitInset,
        controlY(control) - 7.0,
        panel.size.width
            - 2.0 * s3g::gui_layout::kStandardMetrics.hitInset,
        s3g::gui_layout::kStandardMetrics.hitHeight);
}

drawing::Rect controlMenuBoxRect(const CartographyControl& control)
{
    const drawing::Rect panel = panelRect(control.panel);
    return drawing::makeRect(
        s3g::gui_layout::processorControlX(panel.origin.x),
        controlY(control) - 1.0,
        s3g::gui_layout::processorMenuWidth(panel.size.width), 15.0);
}

const CartographyControl* guiControl(clap_id id)
{
    for (const auto& control : kGuiControls) {
        if (control.id == id) return &control;
    }
    return nullptr;
}

uint32_t menuItemCount(clap_id id)
{
    switch (id) {
    case kOrderParamId: return 7u;
    case kLayoutParamId: return 5u;
    case kStereoMapParamId: return 3u;
    case kTimeReferenceParamId: return 2u;
    case kMacroEngineParamId: return 8u;
    case kMacroMetricParamId: return 4u;
    case kListenModeParamId: return 4u;
    case kShredCircuitParamId: return s3g::kMacroShredCircuitCount;
    case kFractureProcessorParamId: return s3g::kFractureProcessorCount;
    default: return 0u;
    }
}

double menuValue(clap_id id, uint32_t item)
{
    return id == kOrderParamId
        ? static_cast<double>(item + 1u) : static_cast<double>(item);
}

int landscapePageForParam(clap_id id)
{
    if (id == kMultipathParamId || id == kOcclusionParamId
        || id == kNetworkWeatherParamId || id == kRefractionParamId) {
        return 1;
    }
    if (id == kMotionParamId || id == kEcologyParamId
        || id == kHorizonParamId) {
        return 2;
    }
    return -1;
}

int processPageForParam(clap_id id)
{
    switch (id) {
    case kMacroEngineParamId:
    case kShredCircuitParamId:
    case kFractureProcessorParamId:
    case kMacroParamId:
    case kColorParamId:
    case kMemoryParamId:
    case kProcessMixParamId:
        return 0;
    case kMacroMetricParamId:
    case kSpreadParamId:
    case kDeviationParamId:
    case kSkewParamId:
    case kCenterParamId:
        return 1;
    default:
        return -1;
    }
}

bool controlVisible(const CartographyControl& control, int landscapePage,
    int processPage, uint32_t engine)
{
    const int landscape = landscapePageForParam(control.id);
    if (landscape >= 0 && landscape != landscapePage) return false;
    const int process = processPageForParam(control.id);
    if (process >= 0 && process != processPage) return false;
    if (control.id == kShredCircuitParamId) return engine == 3u;
    if (control.id == kFractureProcessorParamId) return engine == 4u;
    return true;
}

const char* processControlLabel(clap_id id, uint32_t engine,
    uint32_t fractureProcessor)
{
    if (id == kSkewParamId && engine == 4u) {
        return s3g::fractureBiasLabel(
            static_cast<s3g::FractureProcessor>(fractureProcessor));
    }
    if (id == kSpreadParamId && engine == 6u) return "RADIUS";
    if (id == kDeviationParamId && engine == 6u) return "JITTER";
    if (id == kSkewParamId && engine == 6u) return "DISPERSION";
    if (id == kCenterParamId && engine == 6u) return "CENTROID";
    if (id == kSkewParamId && engine == 7u) return "DIRECTION";

    if (id == kMacroParamId) {
        switch (engine) {
        case 1u: return "TIME";
        case 2u: return "PITCH";
        case 3u: return "DRIVE";
        case 4u:
            return s3g::fractureAmountLabel(
                static_cast<s3g::FractureProcessor>(fractureProcessor));
        case 5u: return "BODY DRIVE";
        case 6u: return "TRANSFER";
        case 7u: return "DEPTH";
        default: return "MACRO";
        }
    }
    if (id == kColorParamId) {
        switch (engine) {
        case 1u: return "TONE";
        case 2u: return "WINDOW";
        case 3u: return "COLOR";
        case 4u:
            return s3g::fractureColorLabel(
                static_cast<s3g::FractureProcessor>(fractureProcessor));
        case 5u: return "CABINET";
        case 6u: return "TILT";
        case 7u: return "LENGTH";
        default: return "COLOR";
        }
    }
    if (id == kMemoryParamId) {
        switch (engine) {
        case 1u:
        case 3u: return "FEEDBACK";
        case 5u: return "RESONANCE";
        case 6u: return "HOLD";
        case 7u: return "RECURRENCE";
        default: return "MEMORY";
        }
    }
    const auto* control = guiControl(id);
    return control ? control->label : "";
}

const char* processDescription(uint32_t engine)
{
    switch (engine) {
    case 0u: return "DIRECT SITE FEED";
    case 1u: return "DISTRIBUTED ECHO LINES";
    case 2u: return "ORDINAL TRANSPOSE";
    case 5u: return "HORN / CABINET / DRIVER";
    case 6u: return "DISTRIBUTED FILTERBANK";
    case 7u: return "LOCAL CAPTURE / REPEAT";
    default: return "";
    }
}

drawing::Rect processPageButtonRect(uint32_t index)
{
    const drawing::Rect panel = panelRect(CartographyPanel::Process);
    constexpr double width = 50.0;
    constexpr double gap = 4.0;
    return drawing::makeRect(panel.origin.x + 212.0
            + static_cast<double>(index) * (width + gap),
        panel.origin.y + 3.0, width, 15.0);
}

drawing::Rect landscapePageButtonRect(uint32_t index)
{
    const drawing::Rect panel = panelRect(CartographyPanel::Guide);
    constexpr double width = 50.0;
    constexpr double gap = 4.0;
    return drawing::makeRect(panel.origin.x + 152.0
            + static_cast<double>(index) * (width + gap),
        panel.origin.y + 3.0, width, 15.0);
}


class Editor final : public s3g::portable_gui::routing::Canvas {
public:

    Plugin* _plugin;
    CartographyDisplaySnapshot _snapshot{};

    clap_id _dragParam;
    clap_id _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    int _dragSite;
    bool _dragListener;
    bool _dragView;
    bool _dragPositionZ;
    drawing::Point _lastDragPoint;
    int _viewMode;
    double _viewAzDeg;
    double _viewElDeg;
    double _viewZoom;
    uint32_t _viewRevision;
    int _landscapePage;
    int _processPage;
    int _factoryPresetIndex;
    char _presetName[64];

#include "s3g_map_encoder_editor.inc"
Editor(Plugin& instance) : Canvas(kGuiWidth, kGuiHeight) {
  auto* plugin = &instance;

        _plugin = plugin;
        
        _dragParam = CLAP_INVALID_ID;
        _openMenu = CLAP_INVALID_ID;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        _dragSite = -1;
        _dragListener = false;
        _dragView = false;
        _dragPositionZ = false;
        _lastDragPoint = drawing::makePoint(0.0, 0.0);
        _viewMode = plugin
            ? plugin->guiViewMode.load(std::memory_order_acquire) : 0;
        _viewAzDeg = plugin
            ? plugin->guiViewAzDeg.load(std::memory_order_acquire) : 38.0;
        _viewElDeg = plugin
            ? plugin->guiViewElDeg.load(std::memory_order_acquire) : 32.0;
        _viewZoom = plugin
            ? plugin->guiViewZoom.load(std::memory_order_acquire) : 1.0;
        _viewRevision = plugin
            ? plugin->guiViewRevision.load(std::memory_order_acquire) : 0u;
        _landscapePage = plugin ? plugin->guiLandscapePage : 0;
        _processPage = plugin ? plugin->guiProcessPage : 0;
        _factoryPresetIndex = 0;
        std::snprintf(_presetName, sizeof(_presetName), "%s", "INIT");
        

}
~Editor() override {
  stopRefresh();

}
void storeViewState()
{
    if (!_plugin) return;
    _plugin->guiViewMode.store(
        static_cast<int32_t>(_viewMode), std::memory_order_release);
    _plugin->guiViewAzDeg.store(
        static_cast<float>(_viewAzDeg), std::memory_order_release);
    _plugin->guiViewElDeg.store(
        static_cast<float>(_viewElDeg), std::memory_order_release);
    _plugin->guiViewZoom.store(
        static_cast<float>(_viewZoom), std::memory_order_release);
}

void loadViewState()
{
    if (!_plugin) return;
    const uint32_t revision = _plugin->guiViewRevision.load(
        std::memory_order_acquire);
    _viewMode = _plugin->guiViewMode.load(std::memory_order_acquire);
    _viewAzDeg = _plugin->guiViewAzDeg.load(std::memory_order_acquire);
    _viewElDeg = _plugin->guiViewElDeg.load(std::memory_order_acquire);
    _viewZoom = _plugin->guiViewZoom.load(std::memory_order_acquire);
    _viewRevision = revision;
}

void applyFactoryPreset(int index)
{
    if (!_plugin) return;
    index = std::clamp(index, 0,
        static_cast<int>(kFactoryPresetCount) - 1);
    const auto& preset = kFactoryPresets[static_cast<uint32_t>(index)];
    const auto set = [&](clap_id id, double value) {
        editSet(id, value);
    };
    set(kSitesParamId, preset.sites);
    set(kLayoutParamId, preset.layout);
    set(kStereoMapParamId, preset.stereo);
    set(kTimeReferenceParamId, preset.timeReference);
    set(kMapScaleParamId, preset.scale);
    set(kNetworkSpreadParamId, preset.network);
    set(kPropagationParamId, preset.propagation);
    set(kAirParamId, preset.air);
    set(kDistanceLossParamId, preset.distanceLoss);
    set(kCarryParamId, preset.carry);
    set(kTurbulenceParamId, preset.turbulence);
    set(kMacroEngineParamId, preset.engine);
    set(kMacroMetricParamId, preset.metric);
    set(kMacroParamId, preset.macro);
    set(kColorParamId, preset.color);
    set(kMemoryParamId, preset.memory);
    set(kSpreadParamId, preset.spread);
    set(kDeviationParamId, preset.deviation);
    set(kSkewParamId, preset.skew);
    set(kCenterParamId, preset.center);
    set(kProcessMixParamId, preset.mix);
    set(kListenModeParamId, preset.listenMode);
    set(kListenerAmountParamId, preset.listenAmount);
    set(kListenerXParamId, preset.listenerX);
    set(kListenerYParamId, preset.listenerY);
    set(kListenerZParamId, preset.listenerZ);
    set(kMultipathParamId, preset.multipath);
    set(kOcclusionParamId, preset.occlusion);
    set(kNetworkWeatherParamId, preset.networkWeather);
    set(kRefractionParamId, preset.refraction);
    set(kMotionParamId, preset.motion);
    set(kEcologyParamId, preset.ecology);
    set(kHorizonParamId, preset.horizon);
    set(kShredCircuitParamId, preset.shredCircuit);
    set(kFractureProcessorParamId, preset.fractureProcessor);
    set(kSiteParamId, 1.0);
    set(kSiteGainParamId, 1.0);
    set(kSiteNetworkTrimParamId, 0.0);
    set(kSiteEnabledParamId, 1.0);
    resetLayout();
    _factoryPresetIndex = index;
    std::snprintf(_presetName,
        sizeof(_presetName), "%s", preset.name);
    setNeedsDisplay(true);
}

drawing::Rect openMenuRect()
{
    if (_openMenu == kFactoryPresetMenuId) {
        const drawing::Rect box = drawing::cocoaRect(
            drawing::encoderTitleBand(
                kGuiWidth, kGuiHeight).presetMenu);
        return drawing::makeRect(box.origin.x, drawing::maxY(box) + 2.0,
            box.size.width, 18.0 * _menuItemCount);
    }
    const auto* control = guiControl(_openMenu);
    if (!control) return drawing::Rect{};
    const drawing::Rect box = controlMenuBoxRect(*control);
    return drawing::makeRect(box.origin.x, drawing::maxY(box) + 2.0,
        box.size.width, 18.0 * _menuItemCount);
}

void drawOpenMenu_style(drawing::Attrs  attrs, const drawing::Style& style)
{
    if (_openMenu == CLAP_INVALID_ID || _menuItemCount == 0u) return;
    drawing::String  factoryItems[kFactoryPresetCount] = {
        "INIT", "PHYSICAL PLAZA", "CITY RELAYS",
        "RIDGE AFTERIMAGE", "WATERFRONT CARRY",
        "CORRIDOR BENDS", "SHRED GRID",
        "FRACTURE HORIZON", "LISTENER DRIFT"
    };
    drawing::String  orderItems[7] = {
        "1OA / 4CH", "2OA / 9CH", "3OA / 16CH", "4OA / 25CH",
        "5OA / 36CH", "6OA / 49CH", "7OA / 64CH"
    };
    drawing::String  layoutItems[5] = {
        "RADIAL", "RIDGE", "CORRIDOR", "GRID", "WATERFRONT"
    };
    drawing::String  stereoItems[3] = { "MID/SIDE", "MONO", "ALTERNATE" };
    drawing::String  timeItems[2] = { "RELATIVE", "ABSOLUTE" };
    drawing::String  engineItems[8] = {
        "CLEAN", "DELAY", "PITCH", "SHRED", "FRACTURE",
        "BODY", "SPECTRAL RELAY", "RELAY BUFFER"
    };
    drawing::String  shredItems[8] = {
        "SHRED", "WOOL", "RAT", "ZONE A",
        "ZONE B", "FUZZ I", "FUZZ II", "DIODE"
    };
    drawing::String  fractureItems[10] = {
        "RELAY", "CRUSH", "SPLICE", "LOGIC", "VOID",
        "THROAT", "ROBOT", "OCT DOWN", "OCT UP", "OCT STACK"
    };
    drawing::String  metricItems[4] = {
        "NETWORK", "ARRIVAL", "BEARING", "RANGE"
    };
    drawing::String  listenerItems[4] = {
        "OFF", "FOLLOW", "COUNTER", "BALANCE"
    };
    drawing::String  const* items = factoryItems;
    if (_openMenu == kOrderParamId) items = orderItems;
    else if (_openMenu == kLayoutParamId) items = layoutItems;
    else if (_openMenu == kStereoMapParamId) items = stereoItems;
    else if (_openMenu == kTimeReferenceParamId) items = timeItems;
    else if (_openMenu == kMacroEngineParamId) items = engineItems;
    else if (_openMenu == kMacroMetricParamId) items = metricItems;
    else if (_openMenu == kListenModeParamId) items = listenerItems;
    else if (_openMenu == kShredCircuitParamId) items = shredItems;
    else if (_openMenu == kFractureProcessorParamId) items = fractureItems;
    const int selected = _openMenu == kFactoryPresetMenuId
        ? _factoryPresetIndex
        : static_cast<int>(std::lround(
            publishedParamValue(*_plugin, _openMenu)))
            - (_openMenu == kOrderParamId ? 1 : 0);
    drawing::drawDropdownMenu(
        openMenuRect(), 18.0, items, _menuItemCount,
        selected, _hoverMenuItem, attrs, style);
}

drawing::Rect viewButtonRect(int index)
{
    const drawing::Rect panel = drawing::makeRect(18.0, 42.0, 650.0, 696.0);
    return drawing::topologyProcessorCameraButtonRect(
        panel, static_cast<uint32_t>(std::clamp(index, 0, 2)));
}

drawing::Rect zoomButtonRect(int index)
{
    constexpr double width = 18.0;
    constexpr double gap = 4.0;
    const double x = viewButtonRect(0).origin.x - 12.0
        - (2.0 - static_cast<double>(index)) * width
        - (1.0 - static_cast<double>(index)) * gap;
    return drawing::makeRect(x, 45.0, width, 15.0);
}

void setViewPreset(int mode)
{
    _viewMode = std::clamp(mode, 0, 2);
    if (_viewMode == 0) {
        _viewAzDeg = 0.0;
        _viewElDeg = 0.0;
    } else if (_viewMode == 1) {
        _viewAzDeg = 0.0;
        _viewElDeg = 90.0;
    } else {
        _viewAzDeg = 38.0;
        _viewElDeg = 32.0;
    }
    storeViewState();
    setNeedsDisplay(true);
}

double viewScale()
{
    const drawing::Rect rect = cartographyMapRect();
    return std::min(rect.size.width, rect.size.height) * 0.30
        * std::clamp(_viewZoom, 0.55, 2.20);
}

drawing::Point projectWorldPoint_depth(s3g::Vec3 point, double* depth)
{
    const drawing::Rect rect = cartographyMapRect();
    const double scale = viewScale();
    const float azimuth = static_cast<float>(
        _viewAzDeg * M_PI / 180.0);
    const float elevation = static_cast<float>(
        _viewElDeg * M_PI / 180.0);
    const float ca = std::cos(azimuth);
    const float sa = std::sin(azimuth);
    const float ce = std::cos(elevation);
    const float se = std::sin(elevation);
    const float x1 = ca * point.x - sa * point.y;
    const float y1 = sa * point.x + ca * point.y;
    const float y2 = ce * y1 + se * point.z;
    const float z2 = -se * y1 + ce * point.z;
    if (depth) *depth = static_cast<double>(z2);
    return drawing::makePoint(drawing::midX(rect) + static_cast<double>(x1) * scale,
        drawing::midY(rect) - static_cast<double>(y2) * scale);
}

drawing::Point projectWorldPointX_y_z(double x, double y, double z)
{
    return projectWorldPoint_depth(s3g::Vec3 {
        static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) }, nullptr);
}

drawing::Point projectGroundPointX_y(double x, double y)
{
    return projectWorldPointX_y_z(x, y, 0.0);
}

void drawMap_labels_values(const drawing::Style& style, drawing::Attrs  labels, drawing::Attrs  values)
{
    const drawing::Rect rect = cartographyMapRect();
    drawing::setFill(drawing::calibratedColor(0x101010));
    drawing::rectFill(rect);
    drawing::setStroke(style.grid);
    drawing::frameRect(rect);

    drawing::GraphicsScope graphics(*drawing::current->context);
    drawing::clipRect(drawing::inset(rect, 1.0, 1.0));

    for (int line = -3; line <= 3; ++line) {
        const float coordinate = static_cast<float>(line) * 0.5f;
        drawing::setStroke(drawing::ambisonicOrientationGuideColor(
            line == 0 ? 0.40 : 0.13));
        drawing::Path  gridLine = drawing::Path::bezierPath();
        (gridLine).moveToPoint(projectWorldPoint_depth(s3g::Vec3 { coordinate, -1.5f, 0.0f }, nullptr));
        (gridLine).lineToPoint(projectWorldPoint_depth(s3g::Vec3 { coordinate, 1.5f, 0.0f }, nullptr));
        (gridLine).moveToPoint(projectWorldPoint_depth(s3g::Vec3 { -1.5f, coordinate, 0.0f }, nullptr));
        (gridLine).lineToPoint(projectWorldPoint_depth(s3g::Vec3 { 1.5f, coordinate, 0.0f }, nullptr));
        (gridLine).setLineWidth(line == 0 ? 0.75 : 0.45);
        (gridLine).stroke();
    }
    drawing::drawAmbisonicOrientationGuides(
        [&](double x, double y, double z) {
            return projectWorldPoint_depth({ static_cast<float>(x),
                static_cast<float>(y), static_cast<float>(z) }, nullptr);
        }, 1.5);

    const auto params = _snapshot.params;
    const auto& sites = _snapshot.sites();
    const bool moving = publishedParamValue(
        *_plugin, kMotionParamId) > 0.000001;
    const auto positionForSite = [&](uint32_t site) {
        if (!moving) return s3g::Vec3 {
            sites[site].x, sites[site].y, sites[site].z };
        return _snapshot.renderedSitePosition(site);
    };
    float maximumArrival = 0.0001f;
    for (uint32_t site = 0u; site < params.activeSites; ++site) {
        maximumArrival = std::max(maximumArrival,
            _snapshot.siteArrivalSeconds(site));
    }

    drawing::setStroke(drawing::calibratedColor(0x4b4b4b, 0.75));
    drawing::Path  network = drawing::Path::bezierPath();
    for (uint32_t site = 1u; site < params.activeSites; ++site) {
        const auto previousPosition = positionForSite(site - 1u);
        const auto currentPosition = positionForSite(site);
        const drawing::Point previous = projectWorldPoint_depth(previousPosition, nullptr);
        const drawing::Point current = projectWorldPoint_depth(currentPosition, nullptr);
        (network).moveToPoint(previous);
        (network).lineToPoint(current);
    }
    (network).setLineWidth(1.0);
    (network).stroke();

    for (uint32_t site = 0u; site < params.activeSites; ++site) {
        const auto& cartographySite = sites[site];
        const auto renderedPosition = positionForSite(site);
        const drawing::Point point = projectWorldPoint_depth(renderedPosition, nullptr);
        if (std::fabs(renderedPosition.z) > 0.005f) {
            drawing::setStroke(drawing::calibratedColor(0x666666, 0.55));
            drawing::Path  stem = drawing::Path::bezierPath();
            (stem).moveToPoint(projectWorldPoint_depth(s3g::Vec3 {
                renderedPosition.x, renderedPosition.y, 0.0f }, nullptr));
            (stem).lineToPoint(point);
            (stem).setLineWidth(0.8);
            (stem).stroke();
        }
        const float arrival = _snapshot.siteArrivalSeconds(site);
        const float arrivalNorm = s3g::clamp(
            arrival / maximumArrival, 0.0f, 1.0f);
        const float level = s3g::clamp(
            _snapshot.siteLevel(site) * 7.0f,
            0.0f, 1.0f);
        const double radius = 4.0 + level * 7.0;
        const int red = static_cast<int>(100.0f + arrivalNorm * 130.0f);
        const int green = static_cast<int>(190.0f - arrivalNorm * 80.0f);
        const int blue = static_cast<int>(220.0f - arrivalNorm * 120.0f);
        const int rgb = (red << 16) | (green << 8) | blue;
        drawing::setFill(drawing::calibratedColor(rgb,
            cartographySite.enabled ? 0.90 : 0.25));
        drawing::Rect node = drawing::makeRect(point.x - radius, point.y - radius,
            radius * 2.0, radius * 2.0);
        (drawing::Path::bezierPathWithOvalInRect(node)).fill();
        const float occlusion = _snapshot.siteOcclusion(site);
        if (occlusion > 0.01f) {
            drawing::setStroke(drawing::calibratedColor(0x6f6558,
                0.25 + occlusion * 0.65));
            drawing::Path  shadow = drawing::Path::bezierPathWithOvalInRect(drawing::inset(node, -2.0 - occlusion * 4.0,
                    -2.0 - occlusion * 4.0));
            (shadow).setLineWidth(1.0 + occlusion * 1.5);
            (shadow).stroke();
        }
        if (site == params.selectedSite) {
            drawing::setStroke(style.text);
            drawing::Path  selection = drawing::Path::bezierPathWithOvalInRect(drawing::inset(node, -4.0, -4.0));
            (selection).setLineWidth(1.3);
            (selection).stroke();
        }
        drawing::String  label = drawing::stringFormat("%u", site + 1u);
        drawing::drawAtPoint_withAttributes(label, drawing::makePoint(point.x + radius + 3.0,
            point.y - 6.0), values);
    }

    const auto renderedListener = moving
        ? _snapshot.renderedListenerPosition()
        : s3g::Vec3 { params.listenerX, params.listenerY, params.listenerZ };
    const drawing::Point listener = projectWorldPoint_depth(renderedListener, nullptr);
    if (std::fabs(renderedListener.z) > 0.005f) {
        drawing::setStroke(drawing::calibratedColor(0x8f7f3c, 0.60));
        drawing::Path  stem = drawing::Path::bezierPath();
        (stem).moveToPoint(projectWorldPoint_depth(s3g::Vec3 {
            renderedListener.x, renderedListener.y, 0.0f }, nullptr));
        (stem).lineToPoint(listener);
        (stem).setLineWidth(0.9);
        (stem).stroke();
    }
    drawing::setStroke(drawing::calibratedColor(0xf0d35d));
    drawing::Path  listenerMark = drawing::Path::bezierPath();
    (listenerMark).moveToPoint(drawing::makePoint(listener.x - 10.0, listener.y));
    (listenerMark).lineToPoint(drawing::makePoint(listener.x + 10.0, listener.y));
    (listenerMark).moveToPoint(drawing::makePoint(listener.x, listener.y - 10.0));
    (listenerMark).lineToPoint(drawing::makePoint(listener.x, listener.y + 10.0));
    (listenerMark).setLineWidth(1.8);
    (listenerMark).stroke();

    drawing::String  status = drawing::stringFormat("%@ / %.0f m per unit / %@ time",
        drawing::stringFromUtf8(layoutName(
            static_cast<uint32_t>(params.layout))),
        params.mapScaleMeters,
        drawing::stringFromUtf8(timeReferenceName(
            static_cast<uint32_t>(params.timeReference))));
    drawing::drawAtPoint_withAttributes(status, drawing::makePoint(rect.origin.x + 10.0,
        rect.origin.y + 9.0), labels);

    const uint32_t selected = params.selectedSite;
    drawing::String  selectedStatus = drawing::stringFormat("SITE %u   %.1f m   ARRIVAL %.3f s",
        selected + 1u, _snapshot.siteDistanceMeters(selected),
        _snapshot.siteArrivalSeconds(selected));
    drawing::drawAtPoint_withAttributes(selectedStatus, drawing::makePoint(rect.origin.x + 10.0,
        drawing::maxY(rect) - 24.0), values);
    graphics.restore();
}

void drawControls_labels_values(const drawing::Style& style, drawing::Attrs  labels, drawing::Attrs  values)
{
    const uint32_t engine = static_cast<uint32_t>(std::lround(
        publishedParamValue(*_plugin, kMacroEngineParamId)));
    const uint32_t fractureProcessor = static_cast<uint32_t>(std::lround(
        publishedParamValue(*_plugin, kFractureProcessorParamId)));
    for (const auto& control : kGuiControls) {
        if (!controlVisible(
                control, _landscapePage, _processPage, engine)) continue;
        const uint32_t index = paramIndex(control.id);
        if (index >= kParamCount) continue;
        const auto& definition = kParams[index];
        const double value = publishedParamValue(*_plugin, control.id);
        char display[64] {};
        paramsValueToText(&_plugin->plugin, control.id, value,
            display, sizeof(display));
        const drawing::Rect panel = panelRect(control.panel);
        const double y = controlY(control);
        const char* labelText = control.panel == CartographyPanel::Process
            ? processControlLabel(control.id, engine, fractureProcessor)
            : control.label;
        drawing::String  label = drawing::stringFromUtf8(labelText);
        drawing::String  text = drawing::stringFromUtf8(display);
        if (control.kind == CartographyControlKind::Menu) {
            drawing::drawProcessorMenu(
                label, text, y, panel.origin.x, panel.size.width,
                labels, values, style);
        } else if (control.kind == CartographyControlKind::Toggle) {
            drawing::drawToggle(
                label, value >= 0.5, y, labels, values, style,
                panel.origin.x
                    + s3g::gui_layout::kStandardMetrics.labelInset,
                s3g::gui_layout::processorControlX(panel.origin.x),
                s3g::gui_layout::processorMenuWidth(panel.size.width));
        } else {
            const double norm = static_cast<double>((value
                - definition.minimum)
                / std::max(0.000001,
                    definition.maximum - definition.minimum));
            drawing::drawProcessorSlider(
                label, text, norm, y, panel.origin.x, panel.size.width,
                labels, values, style);
        }
    }
}

void drawOriginal(drawing::Rect dirty)
{
    (void)dirty;
    const auto style = drawing::softTextStyle();
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs  labels = drawing::softLabelAttrs();
    drawing::Attrs  values = drawing::softValueAttrs();
    drawing::Attrs  title = drawing::softTitleAttrs();
    drawing::drawEncoderTitleBand(
        "s3g AMBI ENCODER CARTOGRAPHY",
        drawing::stringFromUtf8(_presetName),
        drawing::peakDbText(
            _plugin->outputPeak.load(std::memory_order_relaxed)),
        drawing::encoderTitleBand(kGuiWidth, kGuiHeight),
        title, labels, values, style);

    drawing::drawPanelFrame(18.0, 42.0, 650.0, 696.0, style);
    drawing::drawPanelHeader("LOUDSPEAKER CARTOGRAPHY", true,
        18.0, 42.0, 650.0, 21.0, labels, style);
    const drawing::Rect mapPanel = drawing::makeRect(18.0, 42.0, 650.0, 696.0);
    drawing::drawToolboxHeaderButton(
        zoomButtonRect(0), mapPanel, "-", false, values, style);
    drawing::drawToolboxHeaderButton(
        zoomButtonRect(1), mapPanel, "+", false, values, style);
    drawing::drawTopologyProcessorCameraButtons(
        mapPanel, _viewMode, values, style);
    const auto drawPanel = [&](drawing::String  name, CartographyPanel panelId) {
        const drawing::Rect panel = panelRect(panelId);
        drawing::drawPanelFrame(panel.origin.x, panel.origin.y,
            panel.size.width, panel.size.height, style);
        drawing::drawPanelHeader(name, true,
            panel.origin.x, panel.origin.y,
            panel.size.width, 21.0, labels, style);
    };
    drawPanel("OUTPUT / INPUT", CartographyPanel::Output);
    drawPanel("CARTOGRAPHY", CartographyPanel::Cartography);
    drawPanel("PROPAGATION", CartographyPanel::Propagation);
    drawPanel("LISTENER", CartographyPanel::Listener);
    drawPanel("SITE PROCESS", CartographyPanel::Process);
    drawPanel("SELECTED SITE", CartographyPanel::SelectedSite);
    drawPanel("LANDSCAPE FX", CartographyPanel::Guide);
    drawing::String  processPages[2] = { "CORE", "REL" };
    for (uint32_t index = 0u; index < 2u; ++index) {
        drawing::drawToolboxHeaderButton(
            processPageButtonRect(index),
            panelRect(CartographyPanel::Process), processPages[index],
            _processPage == static_cast<int>(index), values, style);
    }
    drawing::String  landscapePages[3] = { "MAP", "PATH", "FIELD" };
    for (uint32_t index = 0u; index < 3u; ++index) {
        drawing::drawToolboxHeaderButton(
            landscapePageButtonRect(index),
            panelRect(CartographyPanel::Guide), landscapePages[index],
            _landscapePage == static_cast<int>(index), values, style);
    }
    drawMap_labels_values(style, labels, values);
    drawControls_labels_values(style, labels, values);
    const uint32_t engine = static_cast<uint32_t>(std::lround(
        publishedParamValue(*_plugin, kMacroEngineParamId)));
    if (_processPage == 0 && engine != 3u && engine != 4u) {
        const drawing::Rect process = panelRect(CartographyPanel::Process);
        const double y = process.origin.y
            + s3g::gui_layout::kStandardMetrics.firstRowOffset
            + s3g::gui_layout::kStandardMetrics.rowPitch - 6.0;
        drawing::drawAtPoint_withAttributes(drawing::stringFromUtf8(processDescription(engine)), drawing::makePoint(process.origin.x + 16.0, y), values);
    }
    const drawing::Rect guide = panelRect(CartographyPanel::Guide);
    if (_landscapePage == 0) {
        drawing::drawAtPoint_withAttributes("DRAG EMPTY MAP  CAMERA", drawing::makePoint(guide.origin.x + 16.0, guide.origin.y + 38.0), values);
        drawing::drawAtPoint_withAttributes("TOP / SIDE  DRAG LISTENER + / SITE", drawing::makePoint(guide.origin.x + 16.0, guide.origin.y + 62.0), values);
        drawing::drawAtPoint_withAttributes("- / +  CAMERA ZOOM", drawing::makePoint(guide.origin.x + 16.0, guide.origin.y + 86.0), values);
        drawing::drawAtPoint_withAttributes("NODE SIZE / COLOR  LEVEL / ARRIVAL", drawing::makePoint(guide.origin.x + 16.0, guide.origin.y + 110.0), values);
        drawing::drawAtPoint_withAttributes("DOUBLE-CLICK SLIDER  DEFAULT", drawing::makePoint(guide.origin.x + 16.0, guide.origin.y + 134.0), labels);
    } else if (_landscapePage == 1) {
        drawing::drawAtPoint_withAttributes("ZERO = DIRECT / STABLE PATHS", drawing::makePoint(guide.origin.x + 16.0, guide.origin.y + 134.0), labels);
    } else {
        drawing::drawAtPoint_withAttributes("AIR / CARRY / TURB VOICE WEATHER", drawing::makePoint(guide.origin.x + 16.0, guide.origin.y + 110.0), values);
        drawing::drawAtPoint_withAttributes("COLOR / MEMORY / SPREAD VOICE ECOLOGY", drawing::makePoint(guide.origin.x + 16.0, guide.origin.y + 134.0), labels);
    }
    drawOpenMenu_style(values, style);
}

void setContinuousParam_point(clap_id param, drawing::Point point)
{
    const uint32_t index = paramIndex(param);
    const auto* control = guiControl(param);
    if (index >= kParamCount || !control) return;
    const auto& definition = kParams[index];
    const drawing::Rect panel = panelRect(control->panel);
    const double x = s3g::gui_layout::processorControlX(panel.origin.x);
    const double width =
        s3g::gui_layout::processorTrackWidth(panel.size.width);
    const double norm = std::clamp(
        static_cast<double>((point.x - x) / width), 0.0, 1.0);
    editValue(param, definition.minimum
        + norm * (definition.maximum - definition.minimum));
    setNeedsDisplay(true);
}

void applyMapDrag(drawing::Point point)
{
    if (_viewMode != 0 && _viewMode != 1) return;
    const drawing::Rect rect = cartographyMapRect();
    const double scale = viewScale();
    if (scale <= 1.0) return;
    const float x = std::clamp(static_cast<float>(
        (point.x - drawing::midX(rect)) / scale), -1.5f, 1.5f);
    const float vertical = std::clamp(static_cast<float>(
        (drawing::midY(rect) - point.y) / scale),
        _viewMode == 1 ? -1.0f : -1.5f,
        _viewMode == 1 ? 1.0f : 1.5f);
    if (_dragListener) {
        editValue(kListenerXParamId, x);
        editValue(_dragPositionZ ? kListenerZParamId : kListenerYParamId,
            vertical);
    } else if (_dragSite >= 0) {
        editValue(kSiteXParamId, x);
        editValue(_dragPositionZ ? kSiteZParamId : kSiteYParamId,
            vertical);
    }
    setNeedsDisplay(true);
}

void mouseDown(drawing::Event* event)
{
    const drawing::Point point = convertPoint_fromView((event)->locationInWindow(), nullptr);

    if (_openMenu != CLAP_INVALID_ID) {
        const int hit = drawing::dropdownHitIndex(
            point, openMenuRect(), 18.0, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == kFactoryPresetMenuId) {
                applyFactoryPreset(hit);
            } else {
                editSet(_openMenu,
                    menuValue(_openMenu, static_cast<uint32_t>(hit)));
                _factoryPresetIndex = -1;
                std::snprintf(_presetName,
                    sizeof(_presetName), "%s", "CUSTOM");
            }
        }
        _openMenu = CLAP_INVALID_ID;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        setNeedsDisplay(true);
        return;
    }

    const auto band = drawing::encoderTitleBand(kGuiWidth, kGuiHeight);
    if (drawing::contains(point, drawing::cocoaRect(band.presetMenu))) {
        _openMenu = kFactoryPresetMenuId;
        _hoverMenuItem = -1;
        _menuItemCount = kFactoryPresetCount;
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point, drawing::cocoaRect(band.loadButton))) {
        presetDialog(false);
        return;
    }
    if (drawing::contains(point, drawing::cocoaRect(band.saveButton))) {
        presetDialog(true);
        return;
    }
    if (drawing::contains(point, drawing::cocoaRect(band.randomButton))) {
        const auto randomUnit = [] {
            return static_cast<double>(randomBits()) / 4294967295.0;
        };
        const auto set = [&](clap_id id, double value) {
            editSet(id, value);
        };
        set(kSitesParamId, 6.0 + std::floor(randomUnit() * 19.0));
        set(kLayoutParamId,
            std::floor(randomUnit() * 5.0));
        set(kStereoMapParamId, std::floor(randomUnit() * 3.0));
        set(kTimeReferenceParamId, std::floor(randomUnit() * 2.0));
        set(kNetworkSpreadParamId,
            120.0 + randomUnit() * 1480.0);
        set(kMapScaleParamId,
            60.0 * std::pow(24.0, randomUnit()));
        set(kPropagationParamId, 0.25 + randomUnit() * 0.75);
        set(kAirParamId, 0.08 + randomUnit() * 0.58);
        set(kDistanceLossParamId, 0.20 + randomUnit() * 0.55);
        set(kCarryParamId, randomUnit() * 0.82);
        set(kTurbulenceParamId, randomUnit() * 0.38);
        set(kMacroEngineParamId,
            1.0 + std::floor(randomUnit() * 7.0));
        set(kShredCircuitParamId,
            std::floor(randomUnit() * s3g::kMacroShredCircuitCount));
        set(kFractureProcessorParamId,
            std::floor(randomUnit() * s3g::kFractureProcessorCount));
        set(kMacroMetricParamId,
            std::floor(randomUnit() * 4.0));
        set(kMacroParamId, randomUnit());
        set(kColorParamId, randomUnit());
        set(kMemoryParamId, randomUnit());
        set(kSpreadParamId, randomUnit());
        set(kDeviationParamId, randomUnit() * 0.55);
        set(kSkewParamId, randomUnit() * 1.2 - 0.6);
        set(kCenterParamId, randomUnit());
        set(kProcessMixParamId, 0.25 + randomUnit() * 0.55);
        set(kListenModeParamId, std::floor(randomUnit() * 4.0));
        set(kListenerAmountParamId, randomUnit() * 0.72);
        set(kListenerXParamId, randomUnit() * 1.4 - 0.7);
        set(kListenerYParamId, randomUnit() * 1.4 - 0.7);
        set(kMultipathParamId,
            randomUnit() < 0.18 ? 0.0 : randomUnit() * 0.68);
        set(kOcclusionParamId,
            randomUnit() < 0.22 ? 0.0 : randomUnit() * 0.72);
        set(kNetworkWeatherParamId,
            randomUnit() < 0.24 ? 0.0 : randomUnit() * 0.75);
        set(kRefractionParamId,
            randomUnit() < 0.20 ? 0.0 : randomUnit() * 1.4 - 0.7);
        set(kMotionParamId,
            randomUnit() < 0.30 ? 0.0 : randomUnit() * 0.60);
        set(kEcologyParamId,
            randomUnit() < 0.32 ? 0.0 : randomUnit() * 0.40);
        set(kHorizonParamId,
            randomUnit() < 0.25 ? 0.0 : randomUnit() * 0.75);
        resetLayout();
        _factoryPresetIndex = -1;
        std::snprintf(_presetName, sizeof(_presetName), "%s", "RANDOM");
        setNeedsDisplay(true);
        return;
    }

    for (uint32_t index = 0u; index < 3u; ++index) {
        if (drawing::contains(point, landscapePageButtonRect(index))) {
            _landscapePage = static_cast<int>(index);
            _plugin->guiLandscapePage = _landscapePage;
            setNeedsDisplay(true);
            return;
        }
    }

    for (uint32_t index = 0u; index < 2u; ++index) {
        if (drawing::contains(point, processPageButtonRect(index))) {
            _processPage = static_cast<int>(index);
            _plugin->guiProcessPage = _processPage;
            _openMenu = CLAP_INVALID_ID;
            _hoverMenuItem = -1;
            _menuItemCount = 0u;
            setNeedsDisplay(true);
            return;
        }
    }

    for (int index = 0; index < 2; ++index) {
        if (drawing::contains(point, zoomButtonRect(index))) {
            _viewZoom = std::clamp(_viewZoom
                + (index == 0 ? -0.15 : 0.15), 0.55, 2.20);
            storeViewState();
            setNeedsDisplay(true);
            return;
        }
    }
    for (int index = 0; index < 3; ++index) {
        if (drawing::contains(point, viewButtonRect(index))) {
            setViewPreset(index);
            return;
        }
    }

    if (drawing::contains(point, cartographyMapRect())) {
        const auto params = _snapshot.params;
        const auto& sites = _snapshot.sites();
        const bool moving = publishedParamValue(
            *_plugin, kMotionParamId) > 0.000001;
        double bestDistance = 14.0;
        int bestSite = -1;
        for (uint32_t site = 0u; site < params.activeSites; ++site) {
            const auto rendered = moving
                ? _snapshot.renderedSitePosition(site)
                : s3g::Vec3 { sites[site].x, sites[site].y, sites[site].z };
            const drawing::Point sitePoint = projectWorldPoint_depth(rendered, nullptr);
            const double dx = point.x - sitePoint.x;
            const double dy = point.y - sitePoint.y;
            const double distance = std::sqrt(dx * dx + dy * dy);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestSite = static_cast<int>(site);
            }
        }
        const auto renderedListener = moving
            ? _snapshot.renderedListenerPosition()
            : s3g::Vec3 {
                params.listenerX, params.listenerY, params.listenerZ };
        const drawing::Point listener = projectWorldPoint_depth(renderedListener, nullptr);
        const double ldx = point.x - listener.x;
        const double ldy = point.y - listener.y;
        if (std::sqrt(ldx * ldx + ldy * ldy) < 15.0) {
            if (_viewMode == 0 || _viewMode == 1) {
                _dragListener = true;
                _dragPositionZ = _viewMode == 1;
                editBegin(kListenerXParamId);
                editBegin(_dragPositionZ
                        ? kListenerZParamId : kListenerYParamId);
                applyMapDrag(point);
                _factoryPresetIndex = -1;
                std::snprintf(_presetName,
                    sizeof(_presetName), "%s", "CUSTOM");
            }
            setNeedsDisplay(true);
            return;
        }
        if (bestSite >= 0) {
            editSet(kSiteParamId,
                static_cast<double>(bestSite + 1));
            if (_viewMode == 0 || _viewMode == 1) {
                _dragSite = bestSite;
                _dragPositionZ = _viewMode == 1;
                editBegin(kSiteXParamId);
                editBegin(_dragPositionZ ? kSiteZParamId : kSiteYParamId);
                applyMapDrag(point);
            }
            _factoryPresetIndex = -1;
            std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
            setNeedsDisplay(true);
            return;
        }
        _dragView = true;
        _lastDragPoint = point;
        return;
    }

    const uint32_t engine = static_cast<uint32_t>(std::lround(
        publishedParamValue(*_plugin, kMacroEngineParamId)));
    for (const auto& control : kGuiControls) {
        if (!controlVisible(
                control, _landscapePage, _processPage, engine)) continue;
        if (!drawing::contains(point, controlHitRect(control))) continue;
        const uint32_t index = paramIndex(control.id);
        if (index >= kParamCount) continue;
        const auto& definition = kParams[index];
        if (control.kind == CartographyControlKind::Menu) {
            _openMenu = control.id;
            _hoverMenuItem = -1;
            _menuItemCount = menuItemCount(control.id);
            setNeedsDisplay(true);
            return;
        }
        double defaultValue = definition.defaultValue;
        if (drawing::sliderDoubleClickDefault(event,
                &_plugin->plugin, definition.id, &defaultValue)) {
            editSet(definition.id, defaultValue);
        } else if (control.kind == CartographyControlKind::Toggle) {
            const bool current = publishedParamValue(
                *_plugin, definition.id) >= 0.5;
            editSet(definition.id, current ? 0.0 : 1.0);
        } else {
            _dragParam = definition.id;
            editBegin(_dragParam);
            setContinuousParam_point(_dragParam, point);
        }
        _factoryPresetIndex = -1;
        std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
        setNeedsDisplay(true);
        return;
    }
}

void mouseDragged(drawing::Event* event)
{
    const drawing::Point point = convertPoint_fromView((event)->locationInWindow(), nullptr);
    if (_dragListener || _dragSite >= 0) {
        applyMapDrag(point);
    } else if (_dragView) {
        const double dx = point.x - _lastDragPoint.x;
        const double dy = point.y - _lastDragPoint.y;
        _viewAzDeg = std::remainder(_viewAzDeg + dx * 0.35, 360.0);
        _viewElDeg = std::clamp(
            _viewElDeg + dy * 0.35, -85.0, 85.0);
        _viewMode = -1;
        _lastDragPoint = point;
        storeViewState();
        setNeedsDisplay(true);
    } else if (_dragParam != CLAP_INVALID_ID) {
        setContinuousParam_point(_dragParam, point);
    }
}

void mouseUp(drawing::Event* event)
{
    (void)event;
    if (_dragParam != CLAP_INVALID_ID) {
        editEnd(_dragParam);
    }
    if (_dragListener) {
        editEnd(kListenerXParamId);
        editEnd(_dragPositionZ ? kListenerZParamId : kListenerYParamId);
    }
    if (_dragSite >= 0) {
        editEnd(kSiteXParamId);
        editEnd(_dragPositionZ ? kSiteZParamId : kSiteYParamId);
    }
    _dragParam = CLAP_INVALID_ID;
    _dragSite = -1;
    _dragListener = false;
    _dragView = false;
    _dragPositionZ = false;
}

void mouseMoved(drawing::Event* event)
{
    if (_openMenu == CLAP_INVALID_ID) return;
    const drawing::Point point = convertPoint_fromView((event)->locationInWindow(), nullptr);
    const int hover = drawing::dropdownHitIndex(
        point, openMenuRect(), 18.0, _menuItemCount);
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        setNeedsDisplay(true);
    }
}
};
} // namespace map_encoder_canvas
s3g::portable_gui::foundation::ContentView* makePortableCanvas(Plugin& p) {
  return new map_encoder_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
