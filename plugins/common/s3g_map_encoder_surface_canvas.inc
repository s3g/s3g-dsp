// Literal surface Cocoa geometry, controls, drawing order and pointer algorithms.
namespace map_encoder_canvas {
namespace drawing = s3g::portable_gui::map_drawing;
namespace foundation = s3g::portable_gui::foundation;
using Kind = s3g::clap_gui::ParamEventKind;
constexpr auto kOutputSourcePanel = s3g::gui_layout::fittedPanel(
    s3g::gui_layout::PluginClass::ProceduralEncoder,
    s3g::gui_layout::PanelRole::Output,
    { 630.0, 250.0, 42.0 }, 42.0, 3u);
static_assert(s3g::gui_layout::combinedOutputSourceCardinalityControlMatches(
    kOutputSourcePanel,
    s3g::gui_layout::SharedControlRole::SourceCardinality));
constexpr double kInputCountRowY = static_cast<double>(
    s3g::gui_layout::rowY(kOutputSourcePanel,
        s3g::gui_layout::combinedOutputSourceCardinalityRow(
            s3g::gui_layout::SharedControlRole::SourceCardinality)));

VSTGUI::CColor  terrainSourceMarkerColor(uint32_t source, bool selected)
{
    static constexpr int kPalette[] {
        0x00e5ff, 0xfff000, 0xff4fd8, 0x00ff7a,
        0xff5a36, 0x7c6cff, 0x00b7ff, 0xc8ff00,
        0xff9f1c, 0x45ffdd, 0xff3f7f, 0xb7ff5a,
    };
    const int rgb = kPalette[source % (sizeof(kPalette) / sizeof(kPalette[0]))];
    return drawing::calibratedColor(rgb, selected ? 1.0 : 0.92);
}

float terrainDisplayLerp(float current, float target, float follow)
{
    const float next = current + (target - current) * follow;
    return std::fabs(next - target) < 0.0001f ? target : next;
}

float terrainDisplayLerpAngle(float current, float target, float follow)
{
    float delta = target - current;
    while (delta > 180.0f) delta -= 360.0f;
    while (delta < -180.0f) delta += 360.0f;
    float next = current + delta * follow;
    while (next > 180.0f) next -= 360.0f;
    while (next <= -180.0f) next += 360.0f;
    return std::fabs(delta * (1.0f - follow)) < 0.0001f ? target : next;
}

float terrainDisplayLerpPhase(float current, float target, float follow)
{
    float delta = target - current;
    while (delta > 0.5f) delta -= 1.0f;
    while (delta < -0.5f) delta += 1.0f;
    float next = current + delta * follow;
    next -= std::floor(next);
    return std::fabs(delta * (1.0f - follow)) < 0.0001f ? target : next;
}

void smoothTerrainDisplayParams(s3g::AmbiTerrainNavigatorParams& current,
    const s3g::AmbiTerrainNavigatorParams& target, float follow)
{
    // Menus and cardinality controls change immediately. Continuously valued
    // geometry is eased independently for the display so path and terrain
    // edits do not teleport while the DSP remains authoritative.
    auto next = target;
    next.rateHz = terrainDisplayLerp(
        current.rateHz, target.rateHz, follow);
    next.azimuthDeg = terrainDisplayLerpAngle(
        current.azimuthDeg, target.azimuthDeg, follow);
    next.elevationDeg = terrainDisplayLerp(
        current.elevationDeg, target.elevationDeg, follow);
    next.distance = terrainDisplayLerp(
        current.distance, target.distance, follow);
    next.traversal = terrainDisplayLerp(
        current.traversal, target.traversal, follow);
    next.terrainDepth = terrainDisplayLerp(
        current.terrainDepth, target.terrainDepth, follow);
    next.layerSpread = terrainDisplayLerp(
        current.layerSpread, target.layerSpread, follow);
    next.innerRadius = terrainDisplayLerp(
        current.innerRadius, target.innerRadius, follow);
    next.outerRadius = terrainDisplayLerp(
        current.outerRadius, target.outerRadius, follow);
    next.azimuthWarpDeg = terrainDisplayLerp(
        current.azimuthWarpDeg, target.azimuthWarpDeg, follow);
    next.elevationWarpDeg = terrainDisplayLerp(
        current.elevationWarpDeg, target.elevationWarpDeg, follow);
    next.distanceWarp = terrainDisplayLerp(
        current.distanceWarp, target.distanceWarp, follow);
    next.fold = terrainDisplayLerp(current.fold, target.fold, follow);
    next.smoothing = terrainDisplayLerp(
        current.smoothing, target.smoothing, follow);
    next.inputGainDb = terrainDisplayLerp(
        current.inputGainDb, target.inputGainDb, follow);
    next.outputGainDb = terrainDisplayLerp(
        current.outputGainDb, target.outputGainDb, follow);
    next.syncDivisionBeats = terrainDisplayLerp(
        current.syncDivisionBeats, target.syncDivisionBeats, follow);
    next.phase = terrainDisplayLerpPhase(current.phase, target.phase, follow);
    next.phaseSpread = terrainDisplayLerp(
        current.phaseSpread, target.phaseSpread, follow);
    next.ease = terrainDisplayLerp(current.ease, target.ease, follow);
    next.distanceScale = terrainDisplayLerp(
        current.distanceScale, target.distanceScale, follow);
    next.doppler = terrainDisplayLerp(
        current.doppler, target.doppler, follow);
    next.air = terrainDisplayLerp(current.air, target.air, follow);
    next.rateSpread = terrainDisplayLerp(
        current.rateSpread, target.rateSpread, follow);
    next.rateDeviation = terrainDisplayLerp(
        current.rateDeviation, target.rateDeviation, follow);
    next.terrainFacet = terrainDisplayLerp(
        current.terrainFacet, target.terrainFacet, follow);
    next.terrainBevel = terrainDisplayLerp(
        current.terrainBevel, target.terrainBevel, follow);
    next.terrainOrientation = terrainDisplayLerp(
        current.terrainOrientation, target.terrainOrientation, follow);
    next.terrainTerrace = terrainDisplayLerp(
        current.terrainTerrace, target.terrainTerrace, follow);
    next.terrainRidge = terrainDisplayLerp(
        current.terrainRidge, target.terrainRidge, follow);
    next.terrainErosion = terrainDisplayLerp(
        current.terrainErosion, target.terrainErosion, follow);
    next.terrainDomainWarp = terrainDisplayLerp(
        current.terrainDomainWarp, target.terrainDomainWarp, follow);
    next.terrainTwist = terrainDisplayLerp(
        current.terrainTwist, target.terrainTwist, follow);
    next.terrainRoughness = terrainDisplayLerp(
        current.terrainRoughness, target.terrainRoughness, follow);
    next.terrainRelief = terrainDisplayLerp(
        current.terrainRelief, target.terrainRelief, follow);
    next.terrainReadMix = terrainDisplayLerp(
        current.terrainReadMix, target.terrainReadMix, follow);
    current = next;
}

bool terrainDisplaySurfaceMatches(
    const s3g::AmbiTerrainNavigatorParams& a,
    const s3g::AmbiTerrainNavigatorParams& b)
{
    return a.distance == b.distance
        && a.terrainDepth == b.terrainDepth
        && a.layerSpread == b.layerSpread
        && a.innerRadius == b.innerRadius
        && a.outerRadius == b.outerRadius
        && a.azimuthWarpDeg == b.azimuthWarpDeg
        && a.elevationWarpDeg == b.elevationWarpDeg
        && a.distanceWarp == b.distanceWarp
        && a.fold == b.fold
        && a.palette == b.palette
        && a.distanceScale == b.distanceScale
        && a.terrainForm == b.terrainForm
        && a.terrainFacet == b.terrainFacet
        && a.terrainBevel == b.terrainBevel
        && a.terrainOrientation == b.terrainOrientation
        && a.terrainTerrace == b.terrainTerrace
        && a.terrainTerraceSteps == b.terrainTerraceSteps
        && a.terrainRidge == b.terrainRidge
        && a.terrainErosion == b.terrainErosion
        && a.terrainDomainWarp == b.terrainDomainWarp
        && a.terrainTwist == b.terrainTwist
        && a.terrainRoughness == b.terrainRoughness
        && a.terrainRelief == b.terrainRelief
        && a.terrainRead == b.terrainRead
        && a.terrainReadMix == b.terrainReadMix;
}

bool terrainDisplayPathMatches(
    const s3g::AmbiTerrainNavigatorParams& a,
    const s3g::AmbiTerrainNavigatorParams& b)
{
    return terrainDisplaySurfaceMatches(a, b)
        && a.points == b.points
        && a.selectedSource == b.selectedSource
        && a.azimuthDeg == b.azimuthDeg
        && a.elevationDeg == b.elevationDeg
        && a.traversal == b.traversal
        && a.orbit == b.orbit
        && a.phaseSpread == b.phaseSpread
        && a.ease == b.ease;
}

constexpr uint32_t kTerrainDisplayLongitudeBands = 28u;
constexpr uint32_t kTerrainDisplayLatitudeBands = 14u;
constexpr uint32_t kTerrainDisplayFineLongitudes =
    kTerrainDisplayLongitudeBands * 2u + 1u;
constexpr uint32_t kTerrainDisplayFineLatitudes =
    kTerrainDisplayLatitudeBands * 2u + 1u;
constexpr uint32_t kTerrainDisplaySurfaceSamples =
    kTerrainDisplayFineLongitudes * kTerrainDisplayFineLatitudes;
constexpr uint32_t kTerrainDisplayMaxPaths = 16u;
constexpr uint32_t kTerrainDisplayPathSegments = 128u;

struct TerrainDisplaySample {
    s3g::Vec3 world {};
    float terrain = 0.0f;
};

constexpr uint32_t terrainDisplaySurfaceIndex(
    uint32_t longitude, uint32_t latitude)
{
    return latitude * kTerrainDisplayFineLongitudes + longitude;
}

struct TerrainSurfaceRenderCoordinator {
    std::atomic<uint64_t> generation { 0u };
    std::atomic<void*> owner { nullptr };
};

struct TerrainSurfaceRenderRequest {
    s3g::AmbiTerrainNavigatorParams params {};
    drawing::Size size {};
    double viewAzimuthDeg = 90.0;
    double viewElevationDeg = 0.0;
    double viewZoom = 1.0;
    uint64_t generation = 0u;
};


s3g::Vec3 terrainDisplayWorldPoint(const s3g::AmbiTerrainPoint& point)
{
    const s3g::Vec3 direction = s3g::directionFromAed(
        point.azimuthDeg, point.elevationDeg);
    return { direction.x * point.distance,
        direction.y * point.distance,
        direction.z * point.distance };
}

drawing::Point terrainDisplayProjectPoint(const s3g::Vec3& point,
    const TerrainSurfaceRenderRequest& request, double* depth)
{
    const double scale = std::min(
        request.size.width, request.size.height) * 0.38
        * std::clamp(request.viewZoom, 0.55, 2.40);
    const float az = static_cast<float>(
        request.viewAzimuthDeg * s3g::kPi / 180.0);
    const float el = static_cast<float>(
        request.viewElevationDeg * s3g::kPi / 180.0);
    const float ca = std::cos(az);
    const float sa = std::sin(az);
    const float ce = std::cos(el);
    const float se = std::sin(el);
    const float x1 = ca * point.x - sa * point.y;
    const float y1 = sa * point.x + ca * point.y;
    const float y2 = ce * y1 + se * point.z;
    const float z2 = -se * y1 + ce * point.z;
    if (depth) *depth = static_cast<double>(z2);
    return drawing::makePoint(request.size.width * 0.5
            + static_cast<double>(x1) * scale,
        request.size.height * 0.5
            - static_cast<double>(y2) * scale);
}

std::shared_ptr<drawing::RenderList> createTerrainSurfaceImage(
    const TerrainSurfaceRenderRequest& request,
    const std::shared_ptr<TerrainSurfaceRenderCoordinator>& coordinator)
{
    if (request.size.width <= 0.0 || request.size.height <= 0.0)
        return nullptr;
    const auto cancelled = [&] {
        return !coordinator
            || coordinator->generation.load(std::memory_order_acquire)
                != request.generation;
    };
    if (cancelled()) return nullptr;
    s3g::AmbiTerrainNavigator encoder;
    // Display queries are stateless and do not use the depth processor. Do
    // not call prepare(): it allocates and clears 64 audio delay lines.
    encoder.setParams(request.params);
    std::array<TerrainDisplaySample,
        kTerrainDisplaySurfaceSamples> samples {};
    for (uint32_t latitude = 0u;
         latitude < kTerrainDisplayFineLatitudes; ++latitude) {
        if (cancelled()) return nullptr;
        const float v = static_cast<float>(latitude)
            / static_cast<float>(kTerrainDisplayFineLatitudes - 1u);
        for (uint32_t longitude = 0u;
             longitude < kTerrainDisplayFineLongitudes; ++longitude) {
            const float u = static_cast<float>(longitude)
                / static_cast<float>(kTerrainDisplayFineLongitudes - 1u);
            const auto point = encoder.surfacePointForDisplay(u, v);
            auto& sample = samples[terrainDisplaySurfaceIndex(
                longitude, latitude)];
            sample.world = terrainDisplayWorldPoint(point);
            sample.terrain = point.terrain;
        }
    }

    drawing::RecordingContext recording(request.size);
    auto* context = &recording;

    struct Facet {
        std::array<drawing::Point, 4u> points {};
        double depth = 0.0;
        float terrain = 0.0f;
    };
    std::vector<Facet> facets;
    facets.reserve(kTerrainDisplayLongitudeBands
        * kTerrainDisplayLatitudeBands);
    for (uint32_t latitude = 0u;
         latitude < kTerrainDisplayLatitudeBands; ++latitude) {
        if (cancelled()) {
            
            return nullptr;
        }
        for (uint32_t longitude = 0u;
             longitude < kTerrainDisplayLongitudeBands; ++longitude) {
            const uint32_t x0 = longitude * 2u;
            const uint32_t x1 = (longitude + 1u) * 2u;
            const uint32_t y0 = latitude * 2u;
            const uint32_t y1 = (latitude + 1u) * 2u;
            const std::array<const TerrainDisplaySample*, 4u> corners {{
                &samples[terrainDisplaySurfaceIndex(x0, y0)],
                &samples[terrainDisplaySurfaceIndex(x1, y0)],
                &samples[terrainDisplaySurfaceIndex(x1, y1)],
                &samples[terrainDisplaySurfaceIndex(x0, y1)],
            }};
            Facet facet {};
            for (uint32_t corner = 0u; corner < 4u; ++corner) {
                double depth = 0.0;
                facet.points[corner] = terrainDisplayProjectPoint(
                    corners[corner]->world, request, &depth);
                facet.depth += depth * 0.25;
                facet.terrain += corners[corner]->terrain * 0.25f;
            }
            facets.push_back(facet);
        }
    }
    std::sort(facets.begin(), facets.end(),
        [](const Facet& a, const Facet& b) {
            return a.depth > b.depth;
        });
    for (const auto& facet : facets) {
        const double light = std::clamp<double>(
            0.115 + static_cast<double>(facet.terrain) * 0.055
                + facet.depth * 0.012,
            0.055, 0.22);
        context->fill = drawing::srgbColor(light, light, light, 0.82);
        context->begin();
        context->move(facet.points[0].x, facet.points[0].y);
        for (uint32_t corner = 1u; corner < 4u; ++corner) {
            context->line(facet.points[corner].x, facet.points[corner].y);
        }
        context->close();
        context->finish(true);
    }

    context->stroke = drawing::srgbColor(0.48, 0.48, 0.48, 0.24);
    context->width = 0.45;
    for (uint32_t latitude = 1u;
         latitude < kTerrainDisplayLatitudeBands; ++latitude) {
        context->begin();
        for (uint32_t longitude = 0u;
             longitude < kTerrainDisplayFineLongitudes; ++longitude) {
            const auto& sample = samples[terrainDisplaySurfaceIndex(
                longitude, latitude * 2u)];
            const drawing::Point point = terrainDisplayProjectPoint(
                sample.world, request, nullptr);
            if (longitude == 0u)
                context->move(point.x, point.y);
            else
                context->line(point.x, point.y);
        }
        context->finish(false);
    }
    for (uint32_t longitude = 0u;
         longitude < kTerrainDisplayLongitudeBands; longitude += 2u) {
        context->begin();
        for (uint32_t latitude = 0u;
             latitude < kTerrainDisplayFineLatitudes; ++latitude) {
            const auto& sample = samples[terrainDisplaySurfaceIndex(
                longitude * 2u, latitude)];
            const drawing::Point point = terrainDisplayProjectPoint(
                sample.world, request, nullptr);
            if (latitude == 0u)
                context->move(point.x, point.y);
            else
                context->line(point.x, point.y);
        }
        context->finish(false);
    }

    // The static path mesh belongs to the same target geometry as the shell.
    // Rasterize both on the worker so drawRect never evaluates terrain DSP.
    const uint32_t pathCount = std::min<uint32_t>(
        request.params.points, kTerrainDisplayMaxPaths);
    for (uint32_t lane = 0u; lane < pathCount; ++lane) {
        if (cancelled()) {
            
            return nullptr;
        }
        const uint32_t source = request.params.selectedSource >= pathCount
                && lane == pathCount - 1u
            ? request.params.selectedSource : lane;
        const bool selected = source == request.params.selectedSource;
        const double light = selected ? 0.78 : 0.42;
        context->stroke = drawing::srgbColor(light, light, light,
            selected ? 0.90 : 0.45);
        context->width = selected ? 1.4 : 0.7;
        context->begin();
        for (uint32_t segment = 0u;
             segment <= kTerrainDisplayPathSegments; ++segment) {
            const float phase = segment == kTerrainDisplayPathSegments
                ? 0.0f
                : static_cast<float>(segment)
                    / static_cast<float>(kTerrainDisplayPathSegments);
            const auto point = encoder.pathPointForDisplay(source, phase);
            const drawing::Point projected = terrainDisplayProjectPoint(
                terrainDisplayWorldPoint(point), request, nullptr);
            if (segment == 0u)
                context->move(projected.x, projected.y);
            else
                context->line(projected.x, projected.y);
        }
        context->finish(false);
    }
    return cancelled() ? nullptr : std::make_shared<drawing::RenderList>(std::move(context->result));
}


class Editor final : public s3g::portable_gui::routing::Canvas {
public:

    Plugin* _plugin;

    int _dragParam;
    bool _pendingDragValueDirty;
    double _pendingDragValue;
    bool _pendingGestureEnd;
    clap_id _pendingGestureParam;
    clap_id _displayOverrideParam;
    double _displayOverrideValue;
    double _displayOverrideReleaseTime;
    int _viewMode;
    bool _dragView;
    drawing::Point _lastDragPoint;
    double _viewAzDeg;
    double _viewElDeg;
    double _viewZoom;
    int _surfacePage;
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    char _titlePresetName[64];
    s3g::AmbiTerrainNavigatorParams _paramsSnapshot;
    s3g::AmbiTerrainNavigatorParams _displayParams;
    s3g::AmbiTerrainNavigator _displayEncoder;
    std::array<s3g::AmbiTerrainPoint, s3g::kAmbiTerrainMaxPoints> _displayPoints;
    std::array<bool, s3g::kAmbiTerrainMaxPoints> _displayPointsPrimed;
    VSTGUI::SharedPointer<VSTGUI::CBitmap> _surfaceImageCache;
    bool _surfaceImageCacheValid;
    VSTGUI::SharedPointer<VSTGUI::CBitmap> _previousSurfaceImage;
    double _surfaceCrossfadeStartTime;
    s3g::AmbiTerrainNavigatorParams _requestedSurfaceParams;
    std::shared_ptr<TerrainSurfaceRenderCoordinator> _surfaceRenderCoordinator;
    double _lastDisplayUpdateTime;
    float _displayFollow;
    uint64_t _drawPassCount;
using SurfaceWorker = drawing::LatestWorker<TerrainSurfaceRenderRequest, drawing::RenderList>;
std::unique_ptr<SurfaceWorker> _surfaceWorker;
#include "s3g_map_surface_cache.inc"

#include "s3g_map_encoder_editor.inc"
Editor(Plugin& instance) : Canvas(kGuiWidth, kGuiHeight) {
  auto* plugin = &instance;

        _plugin = plugin;
        
        _dragParam = 0;
        _pendingDragValueDirty = false;
        _pendingDragValue = 0.0;
        _pendingGestureEnd = false;
        _pendingGestureParam = CLAP_INVALID_ID;
        _displayOverrideParam = CLAP_INVALID_ID;
        _displayOverrideValue = 0.0;
        _displayOverrideReleaseTime = 0.0;
        _viewMode = 0;
        _dragView = false;
        _lastDragPoint = drawing::Point{};
        _viewAzDeg = 90.0;
        _viewElDeg = 0.0;
        _viewZoom = 1.0;
        _surfacePage = 0;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        _paramsSnapshot = publishedParamsSnapshot(*plugin);
        _displayParams = _paramsSnapshot;
        _displayEncoder.setParams(_displayParams);
        _displayPoints = {};
        _displayPointsPrimed = {};
        _surfaceImageCache = nullptr;
        _surfaceImageCacheValid = false;
        _previousSurfaceImage = nullptr;
        _surfaceCrossfadeStartTime = 0.0;
        _requestedSurfaceParams = _displayParams;
        _surfaceRenderCoordinator
            = std::make_shared<TerrainSurfaceRenderCoordinator>();
        _surfaceWorker = std::make_unique<SurfaceWorker>([coordinator = _surfaceRenderCoordinator](const auto& request) { return createTerrainSurfaceImage(request, coordinator); });
        _lastDisplayUpdateTime = drawing::uptime();
        _displayFollow = 1.0f;
        _drawPassCount = 0u;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CURRENT");
        
        requestSurfaceImageForParams(_requestedSurfaceParams);

}
~Editor() override {
  stopRefresh();
  _surfaceRenderCoordinator->generation.fetch_add(1u);
  _surfaceWorker.reset();
}
void refreshControlSnapshot()
{
    if (!_plugin) return;
    auto target = publishedParamsSnapshot(*_plugin);
    const double now = drawing::uptime();
    if (_displayOverrideParam != CLAP_INVALID_ID) {
        double published = 0.0;
        const bool hasPublished = paramValueFromParams(
            target, _displayOverrideParam, published);
        if (!_dragParam && hasPublished
            && (std::fabs(published - _displayOverrideValue) < 0.000001
                || (_displayOverrideReleaseTime > 0.0
                    && now - _displayOverrideReleaseTime > 0.25))) {
            _displayOverrideParam = CLAP_INVALID_ID;
            _displayOverrideReleaseTime = 0.0;
        } else {
            (void)assignParam(target, _displayOverrideParam,
                _displayOverrideValue);
        }
    }
    if (!terrainDisplayPathMatches(
            _requestedSurfaceParams, target)) {
        _requestedSurfaceParams = target;
        requestSurfaceImageForParams(target);
    }
    const double elapsed = std::clamp(
        now - _lastDisplayUpdateTime, 0.0, 0.25);
    _lastDisplayUpdateTime = now;
    constexpr double kDisplayTransitionSeconds = 0.10;
    _displayFollow = static_cast<float>(
        1.0 - std::exp(-elapsed / kDisplayTransitionSeconds));
    _paramsSnapshot = target;
    smoothTerrainDisplayParams(_displayParams, target, _displayFollow);
    _displayEncoder.setParams(_displayParams);
}

drawing::String  valueText_value(clap_id param, double value)
{
    char text[64] {};
    paramsValueToText(&_plugin->plugin, param, value, text, sizeof(text));
    return drawing::stringFromUtf8(text);
}

double paramValue(clap_id param)
{
    if (_displayOverrideParam == param) return _displayOverrideValue;
    double value = 0.0;
    (void)paramValueFromParams(_paramsSnapshot, param, value);
    return value;
}

void flushPendingDragValue()
{
    if (!_plugin) return;
    if (_pendingGestureEnd) {
        const bool queued = _pendingDragValueDirty
            ? queueGuiParamValueAndGestureEnd(*_plugin,
                _pendingGestureParam, _pendingDragValue)
            : queueGuiParamEvent(*_plugin,
                s3g::clap_gui::ParamEventKind::GestureEnd,
                _pendingGestureParam);
        if (queued) {
            _pendingDragValueDirty = false;
            _pendingGestureEnd = false;
            _pendingGestureParam = CLAP_INVALID_ID;
        }
        return;
    }
    if (!_dragParam || !_pendingDragValueDirty) return;
    if (queueGuiCoalescedDragValue(*_plugin,
            static_cast<clap_id>(_dragParam), _pendingDragValue)) {
        _pendingDragValueDirty = false;
    }
}

const ParamDef* paramDef(clap_id param)
{
    for (const auto& def : kParams) if (def.id == param) return &def;
    return nullptr;
}

double viewScaleForRect(drawing::Rect rect)
{
    return std::min(rect.size.width, rect.size.height) * 0.38 * std::clamp(_viewZoom, 0.55, 2.40);
}

drawing::Point projectWorldPoint_rect_depth(s3g::Vec3 p, drawing::Rect rect, double* depth)
{
    const double cx = drawing::midX(rect);
    const double cy = drawing::midY(rect);
    const double scale = viewScaleForRect(rect);
    const float az = static_cast<float>(_viewAzDeg * s3g::kPi / 180.0);
    const float el = static_cast<float>(_viewElDeg * s3g::kPi / 180.0);
    const float ca = std::cos(az);
    const float sa = std::sin(az);
    const float ce = std::cos(el);
    const float se = std::sin(el);
    const float x1 = ca * p.x - sa * p.y;
    const float y1 = sa * p.x + ca * p.y;
    const float y2 = ce * y1 + se * p.z;
    const float z2 = -se * y1 + ce * p.z;
    if (depth) *depth = static_cast<double>(z2);
    return drawing::makePoint(cx + static_cast<double>(x1) * scale, cy - static_cast<double>(y2) * scale);
}

void setViewPreset(int mode)
{
    _viewMode = mode;
    if (mode == 0) { _viewAzDeg = 90.0; _viewElDeg = 0.0; }
    else if (mode == 1) { _viewAzDeg = 90.0; _viewElDeg = 90.0; }
    else { _viewAzDeg = 38.0; _viewElDeg = 32.0; }
    requestSurfaceImageForParams(_requestedSurfaceParams);
    setNeedsDisplay(true);
}

drawing::Rect fieldPanelRect()
{ return drawing::makeRect(18, 42, 596, 732); }

drawing::Rect fieldRect()
{ return drawing::makeRect(34, 76, 564, 682); }

drawing::Rect zoomButtonRect_inRect(int index, drawing::Rect rect)
{
    const double w = 22.0;
    const double gap = 6.0;
    const double viewTotal = 3.0 * 38.0 + 2.0 * 6.0;
    const double zoomTotal = 2.0 * w + gap;
    const double viewStart = drawing::maxX(rect) - 10.0 - viewTotal;
    const double start = viewStart - 12.0 - zoomTotal;
    return drawing::makeRect(start + static_cast<double>(index) * (w + gap), rect.origin.y + 3.0, w, 16.0);
}

drawing::Rect viewButtonRect_inRect(int index, drawing::Rect rect)
{
    const double w = 38.0;
    const double gap = 6.0;
    const double start = drawing::maxX(zoomButtonRect_inRect(1, rect)) + 12.0;
    return drawing::makeRect(start + static_cast<double>(index) * (w + gap), rect.origin.y + 3.0, w, 16.0);
}

drawing::Rect pathTabRect(int index)
{
    static constexpr double starts[] { 638.0, 682.0, 730.0, 774.0, 822.0 };
    static constexpr double widths[] { 40.0, 44.0, 40.0, 44.0, 42.0 };
    const int safe = std::clamp(index, 0, 4);
    return drawing::makeRect(starts[safe], 163.0, widths[safe], 16.0);
}

void drawViewButtonsInRect_attrs_style(drawing::Rect rect, drawing::Attrs  attrs, const drawing::Style& style)
{
    static drawing::String  labels[] = { "TOP", "SIDE", "3/4" };
    for (int i = 0; i < 3; ++i) drawing::drawToolboxHeaderButton(viewButtonRect_inRect(i, rect), rect, labels[i], i == _viewMode, attrs, style);
}

void drawZoomButtonsInRect_attrs_style(drawing::Rect rect, drawing::Attrs  attrs, const drawing::Style& style)
{
    static drawing::String  labels[] = { "-", "+" };
    for (int i = 0; i < 2; ++i) drawing::drawToolboxHeaderButton(zoomButtonRect_inRect(i, rect), rect, labels[i], false, attrs, style);
}

void drawPathTabsWithAttrs_style(drawing::Attrs  attrs, const drawing::Style& style)
{
    static drawing::String  labels[] = { "PATH", "FORM", "SKIN", "WARP", "READ" };
    const drawing::Rect tabHeader = drawing::makeRect(630, 160, 250, 21);
    for (int index = 0; index < 5; ++index)
        drawing::drawToolboxHeaderButton(pathTabRect(index), tabHeader, labels[index], index == _surfacePage, attrs, style);
}

s3g::Vec3 worldPoint(const s3g::AmbiTerrainPoint& point)
{
    const s3g::Vec3 direction = s3g::directionFromAed(point.azimuthDeg, point.elevationDeg);
    return { direction.x * point.distance, direction.y * point.distance, direction.z * point.distance };
}

void drawField_attrs_style(drawing::Rect rect, drawing::Attrs  attrs, const drawing::Style& style)
{
    drawing::setFill(drawing::calibratedColor(0x111111));
    drawing::rectFill(rect);
    drawing::setStroke(style.grid);
    drawing::frameRect(rect);
    drawing::GraphicsScope graphics(*drawing::current->context);
    drawing::clipRect(drawing::inset(rect, 1, 1));

    const auto params = _paramsSnapshot;
    drawTerrainShellInRect(rect);

    const uint32_t active = std::min<uint32_t>(params.points, s3g::kAmbiTerrainMaxPoints);
    for (uint32_t src = 0; src < active; ++src) {
        s3g::AmbiTerrainPoint target {};
        target.azimuthDeg = _plugin->guiAzimuth[src].load(std::memory_order_relaxed);
        target.elevationDeg = _plugin->guiElevation[src].load(std::memory_order_relaxed);
        target.distance = _plugin->guiDistance[src].load(std::memory_order_relaxed);
        target.terrain = _plugin->guiTerrain[src].load(std::memory_order_relaxed);
        if (target.distance <= 0.0f) {
            target = _displayEncoder.pathPointForDisplay(
                src, _displayParams.phase);
        }
        auto& p = _displayPoints[src];
        if (!_displayPointsPrimed[src]) {
            p = target;
            _displayPointsPrimed[src] = true;
        } else {
            p.azimuthDeg = terrainDisplayLerpAngle(
                p.azimuthDeg, target.azimuthDeg, _displayFollow);
            p.elevationDeg = terrainDisplayLerp(
                p.elevationDeg, target.elevationDeg, _displayFollow);
            p.distance = terrainDisplayLerp(
                p.distance, target.distance, _displayFollow);
            p.terrain = terrainDisplayLerp(
                p.terrain, target.terrain, _displayFollow);
            p.shell = target.shell;
        }
        const s3g::Vec3 pos = worldPoint(p);
        drawing::Point pt = projectWorldPoint_rect_depth(pos, rect, nullptr);
        const bool selected = src == params.selectedSource;
        const double r = selected ? 5.0 : 4.0;
        const drawing::Rect outer = drawing::makeRect(std::round(pt.x - r - 1.0), std::round(pt.y - r - 1.0), (r + 1.0) * 2.0, (r + 1.0) * 2.0);
        drawing::setFill(drawing::calibratedWhite(0.02, 0.95));
        drawing::rectFill(outer);
        drawing::setFill(terrainSourceMarkerColor(src, selected));
        drawing::rectFill(drawing::makeRect(std::round(pt.x - r), std::round(pt.y - r), r * 2.0, r * 2.0));
        drawing::setStroke(drawing::calibratedWhite(selected ? 0.96 : 0.12, selected ? 0.95 : 0.75));
        drawing::frameRect(drawing::makeRect(std::round(pt.x - r), std::round(pt.y - r), r * 2.0, r * 2.0));
    }
    graphics.restore();

}

void drawSlider_param_y_attrs_style(drawing::String  name, clap_id param, double y, drawing::Attrs  attrs, const drawing::Style& style)
{
    const ParamDef* def = paramDef(param);
    if (!def) return;
    const double value = paramValue(param);
    double norm = (value - def->min) / std::max(0.000001, def->max - def->min);
    if (param == kAzimuthParamId) {
        norm = s3g::aedAzimuthSliderNorm(static_cast<float>(value));
    } else if (param == kRateParamId || param == kDivisionParamId) {
        const double safeMin = std::max(0.000001, def->min);
        norm = std::log(std::max(safeMin, value) / safeMin) / std::log(def->max / safeMin);
    }
    auto text = valueText_value(param, value);
    // Preserve both number and unit in the original 36-point value box with
    // Fira Code. The CLAP value-to-text API retains its full verbose units.
    if (param == kDivisionParamId) {
        const auto suffix = text.find("beats");
        if (suffix != drawing::String::npos) text.replace(suffix, 5, "bt");
    } else if (param == kRateParamId) {
        const auto suffix = text.find("/cycle");
        if (suffix != drawing::String::npos) text.replace(suffix, 6, "/c");
        const auto minutes = text.find("min/c");
        if (minutes != drawing::String::npos) text.replace(minutes, 5, "m/c");
    }
    drawing::drawSlider(name, text, norm, y, attrs, drawing::softValueAttrs(), style, 646.0, 738.0, 826.0, 82.0);
}

void drawMenu_param_y_attrs_style(drawing::String  name, clap_id param, double y, drawing::Attrs  attrs, const drawing::Style& style)
{
    drawing::drawMenu(name, valueText_value(param, paramValue(param)), y, attrs, drawing::softValueAttrs(), style, 646.0, 738.0, 126.0);
}

bool point_isInControlRowAtY(drawing::Point point, double y)
{
    return drawing::contains(point, drawing::makeRect(626.0, y - 8.0, 266.0, 24.0));
}

double menuRowY(int menu)
{
    switch (menu) {
    case 1: return 104.0;
    case 2: return 222.0;
    case 3: return 196.0;
    case 4: return 392.0;
    case 5: return 418.0;
    case 6: return 196.0;
    case 7: return 196.0;
    default: return 0.0;
    }
}

double menuY()
{ return menuRowY(_openMenu) + 17.0; }

drawing::Rect menuControlRect(int menu)
{
    return drawing::makeRect(738.0, menuRowY(menu) - 1.0, 126.0, 17.0);
}

void drawOpenMenu_style(drawing::Attrs  attrs, const drawing::Style& style)
{
    if (_openMenu <= 0 || _menuItemCount == 0u) return;
    static drawing::String  orderItems[] = { "1OA", "2OA", "3OA", "4OA", "5OA", "6OA", "7OA" };
    static drawing::String  orbitItems[] = { "DRIFT", "LISSAJOUS", "SPIRAL", "FOLD" };
    static drawing::String  paletteItems[] = { "HARMONIC", "FBM", "CELL", "VOT", "RIDGES", "DUNES", "CRATERS", "TECTONIC" };
    static drawing::String  playbackItems[] = { "OFF", "RUN", "SCRUB" };
    static drawing::String  syncItems[] = { "FREE", "SYNC" };
    static drawing::String  formItems[] = { "SPHERE", "TETRA", "CUBE", "OCTA", "DODECA", "ICOSA" };
    static drawing::String  readItems[] = {
        "HEIGHT", "EDGE", "CURVE", "BLEND", "GRADIENT",
        "RIDGE", "VALLEY", "NORMAL", "CROSS", "VECTOR",
    };
    drawing::String  const* items = orderItems;
    int selected = 0;
    if (_openMenu == 1) {
        items = orderItems;
        selected = static_cast<int>(_paramsSnapshot.order) - 1;
        _menuItemCount = 7u;
    } else if (_openMenu == 2) {
        items = orbitItems;
        selected = static_cast<int>(static_cast<uint32_t>(_paramsSnapshot.orbit));
        _menuItemCount = 4u;
    } else if (_openMenu == 3) {
        items = paletteItems;
        selected = static_cast<int>(static_cast<uint32_t>(_paramsSnapshot.palette));
        _menuItemCount = 8u;
    } else if (_openMenu == 4) {
        items = playbackItems;
        selected = static_cast<int>(static_cast<uint32_t>(_paramsSnapshot.playback));
        _menuItemCount = 3u;
    } else if (_openMenu == 5) {
        items = syncItems;
        selected = static_cast<int>(static_cast<uint32_t>(_paramsSnapshot.syncMode));
        _menuItemCount = 2u;
    } else if (_openMenu == 6) {
        items = formItems;
        selected = static_cast<int>(static_cast<uint32_t>(_paramsSnapshot.terrainForm));
        _menuItemCount = 6u;
    } else if (_openMenu == 7) {
        items = readItems;
        selected = static_cast<int>(static_cast<uint32_t>(_paramsSnapshot.terrainRead));
        _menuItemCount = 10u;
    }
    const double itemH = 18.0;
    drawing::drawDropdownMenu(drawing::makeRect(738.0, menuY(), 126.0, itemH * _menuItemCount),
        itemH, items, _menuItemCount, selected, _hoverMenuItem, attrs, style);
}

void drawOriginal(drawing::Rect dirty)
{
    (void)dirty;
    ++_drawPassCount;
    refreshControlSnapshot();
    const drawing::Style style = drawing::softTextStyle();
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs  labelAttrs = drawing::softLabelAttrs();
    drawing::Attrs  titleAttrs = drawing::softTitleAttrs();
    drawing::Attrs  valueAttrs = drawing::softValueAttrs();
    drawing::drawEncoderTitleBand(
        "s3g AMBI ENCODER SURFACE TERRAIN",
        drawing::stringFromUtf8(_titlePresetName),
        drawing::peakDbText(
            _plugin->outputPeak.load(std::memory_order_relaxed)),
        drawing::encoderTitleBand(900.0, 792.0),
        titleAttrs, labelAttrs, valueAttrs, style);
    const drawing::Rect fieldPanel = fieldPanelRect();
    drawing::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, fieldPanel.size.height, style);
    drawing::drawPanelHeader("SURFACE TERRAIN FIELD", true, fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, 21, labelAttrs, style);
    drawViewButtonsInRect_attrs_style(fieldPanel, valueAttrs, style);
    drawZoomButtonsInRect_attrs_style(fieldPanel, valueAttrs, style);
    drawField_attrs_style(fieldRect(), valueAttrs, style);
    constexpr double outputPanelHeight = static_cast<double>(
        s3g::gui_layout::toolboxHeightForRows(3u));
    constexpr double surfacePanelHeight = static_cast<double>(
        s3g::gui_layout::toolboxHeightForRows(6u));
    constexpr double motionPanelHeight = static_cast<double>(
        s3g::gui_layout::toolboxHeightForRows(13u));
    drawing::drawPanelFrame(630, 42, 250, outputPanelHeight, style);
    drawing::drawPanelHeader("OUTPUT / INPUT", true, 630, 42, 250, 21, labelAttrs, style);
    drawSlider_param_y_attrs_style("OUT", kOutputParamId, 78, labelAttrs, style);
    drawMenu_param_y_attrs_style("ORDER", kOrderParamId, 104, labelAttrs, style);
    drawSlider_param_y_attrs_style("INPUTS", kPointsParamId, kInputCountRowY, labelAttrs, style);
    drawing::drawPanelFrame(630, 160, 250, surfacePanelHeight, style);
    drawing::drawPanelHeader("", true, 630, 160, 250, 21, labelAttrs, style);
    drawPathTabsWithAttrs_style(valueAttrs, style);
    drawing::drawPanelFrame(630, 356, 250, motionPanelHeight, style);
    drawing::drawPanelHeader("PLAYBACK / MOTION", true, 630, 356, 250, 21, labelAttrs, style);
    if (_surfacePage == 0) {
        drawSlider_param_y_attrs_style("SOURCE", kSelectedSourceParamId, 196, labelAttrs, style);
        drawMenu_param_y_attrs_style("TRACE", kOrbitParamId, 222, labelAttrs, style);
        drawSlider_param_y_attrs_style("AZIM", kAzimuthParamId, 248, labelAttrs, style);
        drawSlider_param_y_attrs_style("ELEV", kElevationParamId, 274, labelAttrs, style);
        drawSlider_param_y_attrs_style("TRAVERSE", kTraversalParamId, 300, labelAttrs, style);
    } else if (_surfacePage == 1) {
        drawMenu_param_y_attrs_style("FORM", kTerrainFormParamId, 196, labelAttrs, style);
        drawSlider_param_y_attrs_style("FACET", kTerrainFacetParamId, 222, labelAttrs, style);
        drawSlider_param_y_attrs_style("BEVEL", kTerrainBevelParamId, 248, labelAttrs, style);
        drawSlider_param_y_attrs_style("ORIENT", kTerrainOrientationParamId, 274, labelAttrs, style);
    } else if (_surfacePage == 2) {
        drawMenu_param_y_attrs_style("SKIN", kPaletteParamId, 196, labelAttrs, style);
        drawSlider_param_y_attrs_style("DEPTH", kTerrainDepthParamId, 222, labelAttrs, style);
        drawSlider_param_y_attrs_style("ROUGH", kTerrainRoughnessParamId, 248, labelAttrs, style);
        drawSlider_param_y_attrs_style("FOLD", kFoldParamId, 274, labelAttrs, style);
        drawSlider_param_y_attrs_style("RELIEF", kTerrainReliefParamId, 300, labelAttrs, style);
    } else if (_surfacePage == 3) {
        drawSlider_param_y_attrs_style("TERRACE", kTerrainTerraceParamId, 196, labelAttrs, style);
        drawSlider_param_y_attrs_style("STEPS", kTerrainTerraceStepsParamId, 222, labelAttrs, style);
        drawSlider_param_y_attrs_style("RIDGE", kTerrainRidgeParamId, 248, labelAttrs, style);
        drawSlider_param_y_attrs_style("ERODE", kTerrainErosionParamId, 274, labelAttrs, style);
        drawSlider_param_y_attrs_style("DOMAIN", kTerrainDomainWarpParamId, 300, labelAttrs, style);
        drawSlider_param_y_attrs_style("TWIST", kTerrainTwistParamId, 326, labelAttrs, style);
    } else {
        drawMenu_param_y_attrs_style("READ", kTerrainReadParamId, 196, labelAttrs, style);
        drawSlider_param_y_attrs_style("MIX", kTerrainReadMixParamId, 222, labelAttrs, style);
        drawSlider_param_y_attrs_style("AZ WARP", kAzimuthWarpParamId, 248, labelAttrs, style);
        drawSlider_param_y_attrs_style("EL WARP", kElevationWarpParamId, 274, labelAttrs, style);
        drawSlider_param_y_attrs_style("DIST WARP", kDistanceWarpParamId, 300, labelAttrs, style);
    }

    drawMenu_param_y_attrs_style("PLAY", kPlaybackParamId, 392, labelAttrs, style);
    drawMenu_param_y_attrs_style("SYNC", kSyncParamId, 418, labelAttrs, style);
    drawSlider_param_y_attrs_style("DIV", kDivisionParamId, 444, labelAttrs, style);
    drawSlider_param_y_attrs_style("RATE SP", kRateSpreadParamId, 470, labelAttrs, style);
    drawSlider_param_y_attrs_style("RATE DEV", kRateDeviationParamId, 496, labelAttrs, style);
    drawSlider_param_y_attrs_style("RATE", kRateParamId, 522, labelAttrs, style);
    drawSlider_param_y_attrs_style("PHASE", kPhaseParamId, 548, labelAttrs, style);
    drawSlider_param_y_attrs_style("PH SPREAD", kPhaseSpreadParamId, 574, labelAttrs, style);
    drawSlider_param_y_attrs_style("SMOOTH", kSmoothingParamId, 600, labelAttrs, style);
    drawSlider_param_y_attrs_style("EASE", kEaseParamId, 626, labelAttrs, style);
    drawSlider_param_y_attrs_style("DIST", kDistanceScaleParamId, 652, labelAttrs, style);
    drawSlider_param_y_attrs_style("DOPPLER", kDopplerParamId, 678, labelAttrs, style);
    drawSlider_param_y_attrs_style("AIR", kAirParamId, 704, labelAttrs, style);
    drawOpenMenu_style(labelAttrs, style);
}

clap_id paramAtPoint(drawing::Point pt)
{
    struct Row { clap_id id; double y; };
    static constexpr Row pathRows[] {
        { kSelectedSourceParamId, 196 },
        { kAzimuthParamId, 248 }, { kElevationParamId, 274 }, { kTraversalParamId, 300 },
    };
    static constexpr Row formRows[] {
        { kTerrainFacetParamId, 222 }, { kTerrainBevelParamId, 248 },
        { kTerrainOrientationParamId, 274 },
    };
    static constexpr Row skinRows[] {
        { kTerrainDepthParamId, 222 }, { kTerrainRoughnessParamId, 248 },
        { kFoldParamId, 274 }, { kTerrainReliefParamId, 300 },
    };
    static constexpr Row warpRows[] {
        { kTerrainTerraceParamId, 196 }, { kTerrainTerraceStepsParamId, 222 },
        { kTerrainRidgeParamId, 248 }, { kTerrainErosionParamId, 274 },
        { kTerrainDomainWarpParamId, 300 }, { kTerrainTwistParamId, 326 },
    };
    static constexpr Row readRows[] {
        { kTerrainReadMixParamId, 222 }, { kAzimuthWarpParamId, 248 },
        { kElevationWarpParamId, 274 }, { kDistanceWarpParamId, 300 },
    };
    static constexpr Row motionRows[] {
        { kDivisionParamId, 444 }, { kRateSpreadParamId, 470 }, { kRateDeviationParamId, 496 },
        { kRateParamId, 522 }, { kPhaseParamId, 548 },
        { kPhaseSpreadParamId, 574 }, { kSmoothingParamId, 600 }, { kEaseParamId, 626 },
        { kDistanceScaleParamId, 652 }, { kDopplerParamId, 678 }, { kAirParamId, 704 },
    };
    if (point_isInControlRowAtY(pt, 78.0)) return kOutputParamId;
    if (point_isInControlRowAtY(pt, kInputCountRowY)) return kPointsParamId;
    if (_surfacePage == 0) {
        for (const auto& row : pathRows) if (point_isInControlRowAtY(pt, row.y)) return row.id;
    } else if (_surfacePage == 1) {
        for (const auto& row : formRows) if (point_isInControlRowAtY(pt, row.y)) return row.id;
    } else if (_surfacePage == 2) {
        for (const auto& row : skinRows) if (point_isInControlRowAtY(pt, row.y)) return row.id;
    } else if (_surfacePage == 3) {
        for (const auto& row : warpRows) if (point_isInControlRowAtY(pt, row.y)) return row.id;
    } else {
        for (const auto& row : readRows) if (point_isInControlRowAtY(pt, row.y)) return row.id;
    }
    for (const auto& row : motionRows) if (point_isInControlRowAtY(pt, row.y)) return row.id;
    return 0;
}

void setParam_fromPoint(clap_id param, drawing::Point pt)
{
    const ParamDef* def = paramDef(param);
    if (!def) return;
    double norm = std::clamp((static_cast<double>(pt.x) - 738.0) / 82.0, 0.0, 1.0);
    double value = def->min + norm * (def->max - def->min);
    if (param == kAzimuthParamId) value = s3g::aedAzimuthFromSliderNorm(
        static_cast<float>(norm));
    else if (param == kRateParamId || param == kDivisionParamId) value = def->min * std::pow(def->max / def->min, norm);
    if (def->stepped) value = std::round(value);
    value = canonicalParamValue(_paramsSnapshot, param, value);
    _pendingDragValue = value;
    _pendingDragValueDirty = true;
    _displayOverrideParam = param;
    _displayOverrideValue = value;
    _displayOverrideReleaseTime = 0.0;
    // The common-mode refresh timer redraws at a stable 30 Hz. Avoid asking
    // AppKit for an expensive full terrain render on every mouse event.
    if (!refreshActive) setNeedsDisplay(true);
}

void mouseDown(drawing::Event* event)
{
    refreshControlSnapshot();
    drawing::Point pt = convertPoint_fromView((event)->locationInWindow(), nullptr);
    const auto titleBand = drawing::encoderTitleBand(900.0, 792.0);
    if (drawing::contains(pt, drawing::cocoaRect(titleBand.presetMenu))) {
        const uint32_t order = _paramsSnapshot.order;
        const float output = _paramsSnapshot.outputGainDb;
        auto initial = s3g::AmbiTerrainNavigatorParams {};
        initial.order = order;
        initial.outputGainDb = output;
        if (!queueGuiState(*_plugin, initial)) beep();
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "INIT");
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(pt, drawing::cocoaRect(titleBand.loadButton))) {
        presetDialog(false);
        return;
    }
    if (drawing::contains(pt, drawing::cocoaRect(titleBand.saveButton))) {
        presetDialog(true);
        return;
    }
    if (drawing::contains(pt, drawing::cocoaRect(titleBand.randomButton))) {
        const auto randomUnit = [] {
            return static_cast<double>(randomBits()) / 4294967295.0;
        };
        auto randomized = _paramsSnapshot;
        (void)assignParam(randomized, kOrbitParamId, randomBounded(4u));
        (void)assignParam(randomized, kPaletteParamId, randomBounded(8u));
        (void)assignParam(randomized, kTerrainFormParamId, randomBounded(6u));
        (void)assignParam(randomized, kTerrainReadParamId, randomBounded(10u));
        (void)assignParam(randomized, kTraversalParamId, randomUnit());
        (void)assignParam(randomized, kTerrainDepthParamId, randomUnit());
        (void)assignParam(randomized, kTerrainRoughnessParamId, randomUnit());
        (void)assignParam(randomized, kFoldParamId, randomUnit());
        (void)assignParam(randomized, kTerrainReliefParamId, 0.35 + randomUnit() * 0.65);
        (void)assignParam(randomized, kRateParamId, 0.002 * std::pow(250.0, randomUnit()));
        (void)assignParam(randomized, kPhaseSpreadParamId, randomUnit());
        (void)assignParam(randomized, kEaseParamId, randomUnit());
        (void)assignParam(randomized, kDopplerParamId, randomUnit());
        (void)assignParam(randomized, kAirParamId, randomUnit());
        if (!queueGuiState(*_plugin, randomized)) beep();
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "RANDOM");
        setNeedsDisplay(true);
        return;
    }
    for (int index = 0; index < 5; ++index) {
        if (drawing::contains(pt, pathTabRect(index))) {
            _surfacePage = index;
            _openMenu = 0;
            _hoverMenuItem = -1;
            setNeedsDisplay(true);
            return;
        }
    }
    if (_openMenu > 0) {
        const double itemH = 18.0;
        const int hit = drawing::dropdownHitIndex(pt, drawing::makeRect(738.0, menuY(), 126.0, itemH * _menuItemCount), itemH, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == 1) queueGuiParamValue(*_plugin, kOrderParamId, hit + 1);
            else if (_openMenu == 2) queueGuiParamValue(*_plugin, kOrbitParamId, hit);
            else if (_openMenu == 3) queueGuiParamValue(*_plugin, kPaletteParamId, hit);
            else if (_openMenu == 4) queueGuiParamValue(*_plugin, kPlaybackParamId, hit);
            else if (_openMenu == 5) queueGuiParamValue(*_plugin, kSyncParamId, hit);
            else if (_openMenu == 6) queueGuiParamValue(*_plugin, kTerrainFormParamId, hit);
            else if (_openMenu == 7) queueGuiParamValue(*_plugin, kTerrainReadParamId, hit);
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        setNeedsDisplay(true);
        return;
    }
    auto openMenu = [&](int menu, uint32_t count) {
        _openMenu = menu;
        _menuItemCount = count;
        _hoverMenuItem = -1;
        setNeedsDisplay(true);
    };
    if (drawing::contains(pt, menuControlRect(1))) { openMenu(1, 7); return; }
    if (_surfacePage == 0) {
        if (drawing::contains(pt, menuControlRect(2))) { openMenu(2, 4); return; }
    } else if (_surfacePage == 1) {
        if (drawing::contains(pt, menuControlRect(6))) { openMenu(6, 6); return; }
    } else if (_surfacePage == 2) {
        if (drawing::contains(pt, menuControlRect(3))) { openMenu(3, 8); return; }
    } else if (_surfacePage == 4) {
        if (drawing::contains(pt, menuControlRect(7))) { openMenu(7, 10); return; }
    }
    if (drawing::contains(pt, menuControlRect(4))) { openMenu(4, 3); return; }
    if (drawing::contains(pt, menuControlRect(5))) { openMenu(5, 2); return; }
    const drawing::Rect fieldPanel = fieldPanelRect();
    for (int i = 0; i < 3; ++i) {
        if (drawing::contains(pt, viewButtonRect_inRect(i, fieldPanel))) {
            setViewPreset(i);
            return;
        }
    }
    for (int i = 0; i < 2; ++i) {
        if (drawing::contains(pt, zoomButtonRect_inRect(i, fieldPanel))) {
            _viewZoom = std::clamp(_viewZoom * (i == 0 ? 0.86 : 1.16), 0.55, 2.40);
            requestSurfaceImageForParams(_requestedSurfaceParams);
            setNeedsDisplay(true);
            return;
        }
    }
    if (drawing::contains(pt, fieldRect())) {
        _dragView = true;
        _lastDragPoint = pt;
        return;
    }
    _dragParam = paramAtPoint(pt);
    if (_dragParam) {
        _pendingDragValueDirty = false;
        double defaultValue = 0.0;
        if (drawing::sliderDoubleClickDefault(
                event, &_plugin->plugin,
                static_cast<clap_id>(_dragParam), &defaultValue)) {
            queueGuiParamValue(*_plugin, static_cast<clap_id>(_dragParam), defaultValue);
            _dragParam = 0;
            _displayOverrideParam = CLAP_INVALID_ID;
            _displayOverrideReleaseTime = 0.0;
            setNeedsDisplay(true);
            return;
        }
        (void)queueGuiParamEvent(*_plugin,
            s3g::clap_gui::ParamEventKind::GestureBegin,
            static_cast<clap_id>(_dragParam));
        setParam_fromPoint(static_cast<clap_id>(_dragParam), pt);
    }
}

void mouseDragged(drawing::Event* event)
{
    const drawing::Point pt = convertPoint_fromView((event)->locationInWindow(), nullptr);
    if (_dragView) {
        const double dx = pt.x - _lastDragPoint.x;
        const double dy = pt.y - _lastDragPoint.y;
        _viewAzDeg += dx * 0.32;
        _viewElDeg = std::clamp(_viewElDeg + dy * 0.24, -88.0, 88.0);
        _viewMode = -1;
        requestSurfaceImageForParams(_requestedSurfaceParams);
        _lastDragPoint = pt;
        setNeedsDisplay(true);
        return;
    }
    if (_dragParam) setParam_fromPoint(static_cast<clap_id>(_dragParam), pt);
}

void mouseUp(drawing::Event* event)
{
    (void)event;
    if (_dragParam) {
        const clap_id param = static_cast<clap_id>(_dragParam);
        bool queued = false;
        if (_pendingDragValueDirty) {
            queued = queueGuiParamValueAndGestureEnd(
                *_plugin, param, _pendingDragValue);
            if (queued) _pendingDragValueDirty = false;
        } else {
            queued = queueGuiParamEvent(*_plugin,
                s3g::clap_gui::ParamEventKind::GestureEnd, param);
        }
        if (!queued) {
            _pendingGestureEnd = true;
            _pendingGestureParam = param;
        }
        _displayOverrideReleaseTime
            = drawing::uptime();
    }
    _dragParam = 0;
    _dragView = false;
}

void mouseMoved(drawing::Event* event)
{
    if (_openMenu <= 0) return;
    const drawing::Point pt = convertPoint_fromView((event)->locationInWindow(), nullptr);
    const double itemH = 18.0;
    const int hover = drawing::dropdownHitIndex(pt, drawing::makeRect(738.0, menuY(), 126.0, itemH * _menuItemCount), itemH, _menuItemCount);
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
