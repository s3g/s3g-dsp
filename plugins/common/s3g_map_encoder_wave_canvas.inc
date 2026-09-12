// Literal wave Cocoa geometry, controls, drawing order and pointer algorithms.
namespace map_encoder_canvas {
namespace drawing = s3g::portable_gui::map_drawing;
namespace foundation = s3g::portable_gui::foundation;
using Kind = s3g::clap_gui::ParamEventKind;
struct GuiRow { const char* label; clap_id param; double panelX; double y; bool menu; };
enum class GuiToolbox : uint8_t {
    Output,
    Engine,
    Terrain,
    Envelope,
    PitchListen,
    Scan,
    Selection,
    Motion,
    Projection,
};
constexpr s3g::gui_layout::Panel kEnginePanel {
    s3g::gui_layout::PluginClass::ProceduralEncoder,
    s3g::gui_layout::PanelRole::Engine,
    { 630.0, 134.0, 250.0,
        s3g::gui_layout::toolboxHeightForRows(6u) },
    36.0, 26.0, 6u,
};
static_assert(s3g::gui_layout::sourceCardinalityControlMatches(
    kEnginePanel,
    s3g::gui_layout::SharedControlRole::SourceCardinality));

constexpr GuiRow kGuiRows[] {
    { "OUT", kOutputParamId, 630, 78, false },
    { "ORDER", kOrderParamId, 630, 104, true }, { "VOICES", kVoicesParamId, 630, 170, false },
    { "MODE", kModeParamId, 630, 196, true }, { "BASE", kBaseNoteParamId, 630, 222, false },
    { "SPREAD", kPitchSpreadParamId, 630, 248, false }, { "TUNE", kTuneParamId, 630, 274, false },
    { "DETUNE", kDetuneParamId, 630, 300, false },
    { "FORM", kTerrainFormParamId, 630, 374, true }, { "FACET", kTerrainFacetParamId, 630, 400, false },
    { "BEVEL", kTerrainBevelParamId, 630, 426, false }, { "ORIENT", kTerrainOrientationParamId, 630, 452, false },
    { "SKIN", kSkinParamId, 630, 374, true }, { "DEPTH", kTerrainDepthParamId, 630, 400, false },
    { "ROUGH", kTerrainRoughnessParamId, 630, 426, false }, { "FOLD", kTerrainFoldParamId, 630, 452, false },
    { "RELIEF", kTerrainReliefParamId, 630, 478, false },
    { "TERRACE", kTerrainTerraceParamId, 630, 374, false }, { "STEPS", kTerrainTerraceStepsParamId, 630, 400, false },
    { "RIDGE", kTerrainRidgeParamId, 630, 426, false }, { "ERODE", kTerrainErosionParamId, 630, 452, false },
    { "DOMAIN", kTerrainDomainWarpParamId, 630, 478, false }, { "TWIST", kTerrainTwistParamId, 630, 504, false },
    { "READ", kInterpretationParamId, 630, 374, true }, { "MIX", kInterpretationMixParamId, 630, 400, false },
    { "ATTACK", kAttackParamId, 630, 578, false }, { "DECAY", kDecayParamId, 630, 604, false },
    { "SUSTAIN", kSustainParamId, 630, 630, false }, { "RELEASE", kReleaseParamId, 630, 656, false },
    { "PITCH", kPitchModeParamId, 630, 730, true }, { "SCALE", kPitchScaleParamId, 630, 756, true },
    { "LISTEN", kFieldListenModeParamId, 630, 782, true },
    { "TRACE", kTraceParamId, 896, 78, true }, { "RADIUS", kScanRadiusParamId, 896, 104, false },
    { "ASPECT", kScanAspectParamId, 896, 130, false }, { "ROTATE", kScanRotationParamId, 896, 156, false },
    { "WARP", kScanWarpParamId, 896, 182, false },
    { "SIDES", kPolygonSidesParamId, 896, 130, false }, { "ROUND", kPolygonRoundParamId, 896, 156, false },
    { "STAR", kPolygonStarParamId, 896, 182, false }, { "SKEW", kPolygonSkewParamId, 896, 208, false },
    { "XFADE", kTableXfadeParamId, 896, 234, false },
    { "LAW", kSelectionParamId, 896, 318, true }, { "JOIN", kTransitionParamId, 896, 344, true },
    { "XFER", kNeighborTransferParamId, 896, 370, false }, { "MEM", kSelectionMemoryParamId, 896, 396, false },
    { "MOTION", kMotionModeParamId, 896, 454, true },
    { "DENS", kFieldDensityParamId, 896, 480, false }, { "DUR", kFieldDurationParamId, 896, 506, false },
    { "REST", kFieldRestParamId, 896, 532, false }, { "CONT", kFieldContrastParamId, 896, 558, false },
    { "DEV", kRegionDeviationParamId, 896, 584, false }, { "MACRO", kMacroDurationParamId, 896, 610, false },
    { "AZ RATE", kAzimuthRateParamId, 896, 480, false }, { "EL RATE", kElevationRateParamId, 896, 506, false },
    { "RATE DEV", kRotationDeviationParamId, 896, 532, false },
    { "AZIM", kAzimuthParamId, 896, 694, false }, { "ELEV", kElevationParamId, 896, 720, false },
    { "DIST", kDistanceParamId, 896, 746, false }, { "SPACE", kSpatialSpreadParamId, 896, 772, false },
    { "FOLLOW", kSpatialFollowParamId, 896, 798, false },
};

const GuiRow* guiRow(clap_id param)
{
    for (const auto& row : kGuiRows) if (row.param == param) return &row;
    return nullptr;
}

int terrainPageForParam(clap_id param)
{
    if (param == kTerrainFormParamId || param == kTerrainFacetParamId
        || param == kTerrainBevelParamId || param == kTerrainOrientationParamId) return 0;
    if (param == kSkinParamId || param == kTerrainDepthParamId || param == kTerrainRoughnessParamId
        || param == kTerrainFoldParamId || param == kTerrainReliefParamId) return 1;
    if (param == kTerrainTerraceParamId || param == kTerrainTerraceStepsParamId || param == kTerrainRidgeParamId
        || param == kTerrainErosionParamId || param == kTerrainDomainWarpParamId || param == kTerrainTwistParamId) return 2;
    if (param == kInterpretationParamId || param == kInterpretationMixParamId) return 3;
    return -1;
}

bool guiRowVisible(const GuiRow& row, s3g::AmbiWaveTerrainMotionMode motion, int terrainPage,
                   s3g::AmbiWaveTerrainTrace trace)
{
    const bool fieldOnly = row.param == kFieldDensityParamId || row.param == kFieldDurationParamId
        || row.param == kFieldRestParamId || row.param == kFieldContrastParamId
        || row.param == kRegionDeviationParamId || row.param == kMacroDurationParamId
        || row.param == kSelectionParamId || row.param == kTransitionParamId
        || row.param == kNeighborTransferParamId || row.param == kSelectionMemoryParamId;
    const bool rotateOnly = row.param == kAzimuthRateParamId || row.param == kElevationRateParamId
        || row.param == kRotationDeviationParamId;
    const int page = terrainPageForParam(row.param);
    const bool polygonOnly = row.param == kPolygonSidesParamId || row.param == kPolygonRoundParamId
        || row.param == kPolygonStarParamId || row.param == kPolygonSkewParamId;
    const bool curvedScanOnly = row.param == kScanAspectParamId || row.param == kScanRotationParamId
        || row.param == kScanWarpParamId;
    if (page >= 0 && page != terrainPage) return false;
    if (polygonOnly) return trace == s3g::AmbiWaveTerrainTrace::Polygon;
    if (curvedScanOnly && trace == s3g::AmbiWaveTerrainTrace::Polygon) return false;
    if (fieldOnly) return motion == s3g::AmbiWaveTerrainMotionMode::Field;
    if (rotateOnly) return motion == s3g::AmbiWaveTerrainMotionMode::Rotate;
    return true;
}

GuiToolbox guiToolboxForParam(clap_id param)
{
    if (param == kOutputParamId || param == kOrderParamId) return GuiToolbox::Output;
    if (param == kModeParamId || param == kVoicesParamId
        || param == kBaseNoteParamId || param == kPitchSpreadParamId
        || param == kTuneParamId || param == kDetuneParamId) return GuiToolbox::Engine;
    if (terrainPageForParam(param) >= 0) return GuiToolbox::Terrain;
    if (param == kAttackParamId || param == kDecayParamId
        || param == kSustainParamId || param == kReleaseParamId) return GuiToolbox::Envelope;
    if (param == kPitchModeParamId || param == kPitchScaleParamId
        || param == kFieldListenModeParamId) return GuiToolbox::PitchListen;
    if (param == kTraceParamId || param == kScanRadiusParamId
        || param == kScanAspectParamId || param == kScanRotationParamId
        || param == kScanWarpParamId || param == kPolygonSidesParamId
        || param == kPolygonRoundParamId || param == kPolygonStarParamId
        || param == kPolygonSkewParamId || param == kTableXfadeParamId) return GuiToolbox::Scan;
    if (param == kSelectionParamId || param == kTransitionParamId
        || param == kNeighborTransferParamId || param == kSelectionMemoryParamId) return GuiToolbox::Selection;
    if (param == kMotionModeParamId || param == kFieldDensityParamId
        || param == kFieldDurationParamId || param == kFieldRestParamId
        || param == kFieldContrastParamId || param == kRegionDeviationParamId
        || param == kMacroDurationParamId || param == kAzimuthRateParamId
        || param == kElevationRateParamId || param == kRotationDeviationParamId) return GuiToolbox::Motion;
    return GuiToolbox::Projection;
}

struct WaveGuiLayout {
    s3g::gui_layout::Rect output {};
    s3g::gui_layout::Rect engine {};
    s3g::gui_layout::Rect terrain {};
    s3g::gui_layout::Rect envelope {};
    s3g::gui_layout::Rect pitchListen {};
    s3g::gui_layout::Rect scan {};
    s3g::gui_layout::Rect selection {};
    s3g::gui_layout::Rect motion {};
    s3g::gui_layout::Rect projection {};
};

WaveGuiLayout waveGuiLayout(s3g::AmbiWaveTerrainMotionMode motion,
                            int terrainPage,
                            s3g::AmbiWaveTerrainTrace trace)
{
    namespace layout = s3g::gui_layout;
    const uint32_t terrainRows[] { 4u, 5u, 6u, 2u };
    const uint32_t page = static_cast<uint32_t>(std::clamp(terrainPage, 0, 3));
    WaveGuiLayout result {};
    result.output = { 630.0, 42.0, 250.0, layout::toolboxHeightForRows(2u) };
    result.engine = { 630.0, 134.0, 250.0, layout::toolboxHeightForRows(6u) };
    result.terrain = { 630.0,
        result.engine.y + result.engine.height + layout::kStandardMetrics.panelGap,
        250.0,
        layout::toolboxHeightForRows(terrainRows[page]) };
    result.envelope = { 630.0,
        result.terrain.y + result.terrain.height + layout::kStandardMetrics.panelGap,
        250.0, layout::toolboxHeightForRows(4u) };
    result.pitchListen = { 630.0,
        result.envelope.y + result.envelope.height + layout::kStandardMetrics.panelGap,
        250.0, layout::toolboxHeightForRows(3u) };

    const bool polygon = trace == s3g::AmbiWaveTerrainTrace::Polygon;
    const bool field = motion == s3g::AmbiWaveTerrainMotionMode::Field;
    result.scan = { 896.0, 42.0, 246.0,
        layout::toolboxHeightForRows(polygon ? 7u : 6u) };
    double nextY = result.scan.y + result.scan.height + layout::kStandardMetrics.panelGap;
    if (field) {
        result.selection = { 896.0, nextY, 246.0,
            layout::toolboxHeightForRows(4u) };
        nextY += result.selection.height + layout::kStandardMetrics.panelGap;
    }
    result.motion = { 896.0, nextY, 246.0,
        layout::toolboxHeightForRows(field ? 7u : 4u) };
    nextY += result.motion.height + layout::kStandardMetrics.panelGap;
    result.projection = { 896.0, nextY, 246.0,
        layout::toolboxHeightForRows(5u) };
    return result;
}

const s3g::gui_layout::Rect& waveToolboxRect(const WaveGuiLayout& layout,
                                             GuiToolbox toolbox)
{
    switch (toolbox) {
    case GuiToolbox::Output: return layout.output;
    case GuiToolbox::Engine: return layout.engine;
    case GuiToolbox::Terrain: return layout.terrain;
    case GuiToolbox::Envelope: return layout.envelope;
    case GuiToolbox::PitchListen: return layout.pitchListen;
    case GuiToolbox::Scan: return layout.scan;
    case GuiToolbox::Selection: return layout.selection;
    case GuiToolbox::Motion: return layout.motion;
    case GuiToolbox::Projection: return layout.projection;
    }
    return layout.output;
}

double effectiveGuiRowY(const GuiRow& row,
                         const WaveGuiLayout& layout,
                         s3g::AmbiWaveTerrainMotionMode motion,
                         int terrainPage,
                         s3g::AmbiWaveTerrainTrace trace)
{
    const GuiToolbox toolbox = guiToolboxForParam(row.param);
    uint32_t visibleRow = 0u;
    for (const auto& candidate : kGuiRows) {
        if (candidate.param == row.param) break;
        if (guiToolboxForParam(candidate.param) == toolbox
            && guiRowVisible(candidate, motion, terrainPage, trace)) {
            ++visibleRow;
        }
    }
    return static_cast<double>(s3g::gui_layout::toolboxRowY(
        waveToolboxRect(layout, toolbox).y, visibleRow));
}


class Editor final : public s3g::portable_gui::routing::Canvas {
public:

    Plugin* _plugin;

    clap_id _dragParam;
    clap_id _openMenuParam;
    uint32_t _menuItemCount;
    int _hoverMenuItem;
    uint32_t _selectedVoice;
    int _viewMode;
    int _terrainPage;
    bool _dragView;
    bool _viewDidDrag;
    int _pendingVoice;
    drawing::Point _lastDragPoint;
    double _viewAzDeg;
    double _viewElDeg;
    double _viewZoom;
    char _titlePresetName[64];
    s3g::AmbiWaveTerrainParams _paramsSnapshot;
    s3g::AmbiWaveTerrainEncoder _displayEngine;

#include "s3g_map_encoder_editor.inc"
Editor(Plugin& instance) : Canvas(kGuiWidth, kGuiHeight) {
  auto* plugin = &instance;

        _plugin = plugin;  _dragParam = 0; _openMenuParam = 0; _menuItemCount = 0u;
        _hoverMenuItem = -1; _selectedVoice = 0u; _viewMode = 0; _terrainPage = 0;
        _dragView = false; _viewDidDrag = false; _pendingVoice = -1;
        _viewAzDeg = 90.0; _viewElDeg = 0.0; _viewZoom = 1.0;
        _paramsSnapshot = publishedParamsSnapshot(*plugin);
        _displayEngine.prepare(plugin ? plugin->sampleRate : 48000.0);
        _displayEngine.setParams(_paramsSnapshot);
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CURRENT");
        

}
~Editor() override {
  stopRefresh();

}
void refreshControlSnapshot()
{
    if (!_plugin) return;
    _paramsSnapshot = publishedParamsSnapshot(*_plugin);
    _displayEngine.setParams(_paramsSnapshot);
}

drawing::Rect fieldPanelRect()
{ return drawing::makeRect(18, 42, 596, 500); }

drawing::Rect fieldRect()
{ return drawing::makeRect(34, 78, 564, 442); }

drawing::Rect wavePanelRect()
{ return drawing::makeRect(18, 554, 596, 254); }

drawing::Rect terrainProfileRect()
{ return drawing::makeRect(34, 588, 564, 88); }

drawing::Rect waveRect()
{ return drawing::makeRect(34, 692, 564, 94); }

drawing::Rect viewButtonRect(int index)
{
    const drawing::Rect panel = fieldPanelRect();
    return drawing::makeRect(drawing::maxX(panel) - 10.0 - (3 - index) * 44.0, panel.origin.y + 3.0, 38.0, 16.0);
}

drawing::Rect zoomButtonRect(int index)
{
    const drawing::Rect panel = fieldPanelRect();
    return drawing::makeRect(drawing::maxX(panel) - 200.0 + index * 28.0, panel.origin.y + 3.0, 22.0, 16.0);
}

drawing::Rect terrainTabRect(int index)
{
    return drawing::makeRect(704.0 + index * 42.0, 341.0, 38.0, 16.0);
}

void setViewPreset(int mode)
{
    _viewMode = mode;
    if (mode == 0) { _viewAzDeg = 90.0; _viewElDeg = 0.0; }
    else if (mode == 1) { _viewAzDeg = 90.0; _viewElDeg = 90.0; }
    else { _viewAzDeg = 38.0; _viewElDeg = 32.0; }
    setNeedsDisplay(true);
}

drawing::Point project_rect_depth(s3g::Vec3 point, drawing::Rect rect, double* depth)
{
    const double scale = std::min(rect.size.width, rect.size.height) * 0.36 * std::clamp(_viewZoom, 0.55, 2.4);
    const float az = static_cast<float>(_viewAzDeg * s3g::kPi / 180.0);
    const float el = static_cast<float>(_viewElDeg * s3g::kPi / 180.0);
    const float ca = std::cos(az), sa = std::sin(az), ce = std::cos(el), se = std::sin(el);
    const float x1 = ca * point.x - sa * point.y;
    const float y1 = sa * point.x + ca * point.y;
    const float y2 = ce * y1 + se * point.z;
    const float z2 = -se * y1 + ce * point.z;
    if (depth) *depth = z2;
    return drawing::makePoint(drawing::midX(rect) + x1 * scale, drawing::midY(rect) - y2 * scale);
}

s3g::Vec3 worldPoint(s3g::AmbiWaveTerrainPoint point)
{
    const auto direction = s3g::directionFromAed(point.azimuthDeg, point.elevationDeg);
    return { direction.x * point.distance, direction.y * point.distance, direction.z * point.distance };
}

s3g::AmbiWaveTerrainPoint displaySurfacePointU_v(float u, float v)
{
    // The rendered terrain is the complete spherical parameter domain.
    // SPACE and CENTER project that domain into the outgoing Ambisonic field;
    // applying them to the mesh itself leaves an open, apparently flat seam.
    return _displayEngine.terrainDomainPoint(u, v);
}

s3g::AmbiWaveTerrainRegion snapshotRegion_next(uint32_t voice, bool next)
{
    s3g::AmbiWaveTerrainRegion region {};
    if (next) {
        region.u = _plugin->guiNextRegionU[voice].load(std::memory_order_relaxed);
        region.v = _plugin->guiNextRegionV[voice].load(std::memory_order_relaxed);
        region.radius = _plugin->guiNextRadius[voice].load(std::memory_order_relaxed);
        region.aspect = _plugin->guiNextAspect[voice].load(std::memory_order_relaxed);
        region.rotation = _plugin->guiNextRotation[voice].load(std::memory_order_relaxed);
        region.trace = static_cast<s3g::AmbiWaveTerrainTrace>(_plugin->guiNextTrace[voice].load(std::memory_order_relaxed));
    } else {
        region.u = _plugin->guiCurrentRegionU[voice].load(std::memory_order_relaxed);
        region.v = _plugin->guiCurrentRegionV[voice].load(std::memory_order_relaxed);
        region.radius = _plugin->guiCurrentRadius[voice].load(std::memory_order_relaxed);
        region.aspect = _plugin->guiCurrentAspect[voice].load(std::memory_order_relaxed);
        region.rotation = _plugin->guiCurrentRotation[voice].load(std::memory_order_relaxed);
        region.trace = static_cast<s3g::AmbiWaveTerrainTrace>(_plugin->guiCurrentTrace[voice].load(std::memory_order_relaxed));
    }
    return region;
}

s3g::AmbiWaveTerrainRegion activeRegion(uint32_t voice)
{
    const auto current = snapshotRegion_next(voice, false);
    const auto next = snapshotRegion_next(voice, true);
    const float transition = _plugin->guiTransition[voice].load(std::memory_order_relaxed);
    float deltaU = next.u - current.u;
    if (deltaU > 0.5f) deltaU -= 1.0f;
    else if (deltaU < -0.5f) deltaU += 1.0f;
    s3g::AmbiWaveTerrainRegion region {};
    region.u = current.u + deltaU * transition;
    region.u -= std::floor(region.u);
    region.v = s3g::lerp(current.v, next.v, transition);
    region.radius = s3g::lerp(current.radius, next.radius, transition);
    region.aspect = s3g::lerp(current.aspect, next.aspect, transition);
    region.rotation = s3g::lerp(current.rotation, next.rotation, transition);
    region.trace = transition < 0.5f ? current.trace : next.trace;
    return region;
}

std::array<float, 2> contourUv_phase(s3g::AmbiWaveTerrainRegion region, float phase)
{
    return _displayEngine.contourPoint(region, phase);
}

void drawVoiceField_style(drawing::Attrs  attrs, const drawing::Style& style)
{
    const drawing::Rect panel = fieldPanelRect(), field = fieldRect();
    drawing::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    drawing::drawPanelHeader("TERRAIN VOICE FIELD", true, panel.origin.x, panel.origin.y, panel.size.width, 21, attrs, style);
    static drawing::String  labels[] = { "TOP", "SIDE", "3/4" };
    for (int index = 0; index < 3; ++index) drawing::drawToolboxHeaderButton(viewButtonRect(index), panel, labels[index], index == _viewMode, attrs, style);
    drawing::drawToolboxHeaderButton(zoomButtonRect(0), panel, "-", false, attrs, style);
    drawing::drawToolboxHeaderButton(zoomButtonRect(1), panel, "+", false, attrs, style);
    drawing::setFill(drawing::calibratedColor(0x090909)); drawing::rectFill(field); drawing::setStroke(style.grid); drawing::frameRect(field);
    drawing::GraphicsScope graphics(*drawing::current->context); drawing::clipRect(drawing::inset(field, 1, 1));
    struct Facet { std::array<drawing::Point, 4> p {}; double depth = 0.0; VSTGUI::CColor color {}; };
    std::vector<Facet> facets; facets.reserve(32u * 16u);
    for (uint32_t lat = 0; lat < 16u; ++lat) {
        const float v0 = static_cast<float>(lat) / 16.0f, v1 = static_cast<float>(lat + 1u) / 16.0f;
        for (uint32_t lon = 0; lon < 32u; ++lon) {
            const float u0 = static_cast<float>(lon) / 32.0f, u1 = static_cast<float>(lon + 1u) / 32.0f;
            const float uc = (u0 + u1) * 0.5f, vc = (v0 + v1) * 0.5f;
            const float h = _displayEngine.terrainHeight(uc, vc);
            const float visibleHeight = h * _paramsSnapshot.terrainDepth * _paramsSnapshot.terrainRelief;
            const std::array<s3g::AmbiWaveTerrainPoint, 4> shell {
                displaySurfacePointU_v(u0, v0),
                displaySurfacePointU_v(u1, v0),
                displaySurfacePointU_v(u1, v1),
                displaySurfacePointU_v(u0, v1)
            };
            Facet facet {};
            for (uint32_t corner = 0; corner < 4u; ++corner) { double d = 0; facet.p[corner] = project_rect_depth(worldPoint(shell[corner]), field, &d); facet.depth += d * 0.25; }
            const double light = std::clamp<double>(0.155 + visibleHeight * 0.065 + (1.0 - vc) * 0.025, 0.08, 0.26);
            facet.color = drawing::calibratedWhite(light, 0.96);
            facets.push_back(facet);
        }
    }
    std::sort(facets.begin(), facets.end(), [](const Facet& a, const Facet& b) { return a.depth > b.depth; });
    for (const auto& facet : facets) {
        drawing::Path  path = drawing::Path::bezierPath();
        (path).moveToPoint(facet.p[0]);
        for (int corner = 1; corner < 4; ++corner) (path).lineToPoint(facet.p[corner]);
        (path).closePath();
        drawing::setFill(facet.color);
        (path).fill();
        drawing::setStroke(drawing::calibratedWhite(0.62, 0.075));
        (path).setLineWidth(0.28);
        (path).stroke();
    }
    const uint32_t voices = std::clamp<uint32_t>(_paramsSnapshot.voices, 1u, s3g::kAmbiWaveTerrainMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u); _plugin->guiSelectedVoice.store(_selectedVoice, std::memory_order_relaxed);
    for (int pass = 0; pass < 2; ++pass) {
        for (uint32_t voice = 0; voice < voices; ++voice) {
            const bool selected = voice == _selectedVoice;
            if (selected != (pass == 1)) continue;
            const bool active = _plugin->guiFieldActive[voice].load(std::memory_order_relaxed) != 0u;
            const double activityAlpha = active ? 1.0 : 0.24;
            const uint32_t segments = selected ? 112u : 30u;
            const auto current = snapshotRegion_next(voice, false);
            const auto next = snapshotRegion_next(voice, true);
            const auto region = activeRegion(voice);
            const auto drawPath = [&](const s3g::AmbiWaveTerrainRegion& pathRegion, VSTGUI::CColor  color, double alpha, double width) {
                drawing::Path  scanPath = drawing::Path::bezierPath();
                for (uint32_t index = 0; index <= segments; ++index) {
                    const auto uv = contourUv_phase(pathRegion, static_cast<float>(index) / static_cast<float>(segments));
                    const drawing::Point point = project_rect_depth(worldPoint(displaySurfacePointU_v(uv[0], uv[1])), field, nullptr);
                    if (index == 0u) (scanPath).moveToPoint(point); else (scanPath).lineToPoint(point);
                }
                drawing::setStroke(drawing::withAlpha(color, alpha * activityAlpha));
                (scanPath).setLineWidth(width);
                (scanPath).stroke();
            };
            if (selected) {
                drawPath(current, drawing::calibratedRGB(0.25, 0.72, 0.92, 1.0), 0.28, 0.72);
                drawPath(next, drawing::calibratedRGB(0.94, 0.40, 0.68, 1.0), 0.28, 0.72);
            }
            drawPath(region, drawing::calibratedWhite(0.94, 1.0), selected ? 0.94 : 0.30, selected ? 1.65 : 0.62);

            s3g::AmbiWaveTerrainPoint spatialPoint {};
            spatialPoint.azimuthDeg = _plugin->guiPointAzimuth[voice].load(std::memory_order_relaxed);
            spatialPoint.elevationDeg = _plugin->guiPointElevation[voice].load(std::memory_order_relaxed);
            spatialPoint.distance = _plugin->guiPointDistance[voice].load(std::memory_order_relaxed);
            const drawing::Point center = project_rect_depth(worldPoint(spatialPoint), field, nullptr);
            const float energy = _plugin->guiEnergy[voice].load(std::memory_order_relaxed);
            const double size = (selected ? 9.0 : 5.0) + std::clamp<double>(energy * 20.0f, 0.0, 4.0);
            const drawing::Rect marker = drawing::makeRect(center.x - size * 0.5, center.y - size * 0.5, size, size);
            drawing::setFill(drawing::calibratedWhite(0.98, activityAlpha));
            (drawing::Path::bezierPathWithOvalInRect(marker)).fill();
            drawing::setStroke(drawing::calibratedWhite(selected ? 0.06 : 0.34, activityAlpha));
            (drawing::Path::bezierPathWithOvalInRect(drawing::inset(marker, -1.0, -1.0))).stroke();
        }
    }
    graphics.restore();
    drawing::String voiceStatus;
    if (_paramsSnapshot.motionMode == s3g::AmbiWaveTerrainMotionMode::Rotate) {
        voiceStatus = drawing::stringFormat("VOICE %u  AZ %+.2f / EL %+.2f RPM", _selectedVoice + 1u,
            static_cast<double>(_paramsSnapshot.azimuthRateRpm), static_cast<double>(_paramsSnapshot.elevationRateRpm));
    } else {
        voiceStatus = drawing::stringFormat("VOICE %u  %@ / %@", _selectedVoice + 1u,
            drawing::stringFromUtf8(s3g::ambiWaveTerrainSelectionName(_paramsSnapshot.selection)),
            drawing::stringFromUtf8(s3g::ambiWaveTerrainTransitionName(_paramsSnapshot.transition)));
    }
    drawing::drawAtPoint_withAttributes(voiceStatus, drawing::makePoint(field.origin.x + 10, drawing::maxY(field) - 20), attrs);
}

void drawWaveform_style(drawing::Attrs  attrs, const drawing::Style& style)
{
    const drawing::Rect panel = wavePanelRect(), terrainRect = terrainProfileRect(), waveRect = this->waveRect();
    drawing::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    drawing::drawPanelHeader("TERRAIN TO WAVEFORM", true, panel.origin.x, panel.origin.y, panel.size.width, 21, attrs, style);
    const float transition = _plugin->guiTransition[_selectedVoice].load(std::memory_order_relaxed);
    const auto drawProfile = [&](drawing::Rect rect, bool topography) {
        drawing::setFill(drawing::calibratedColor(0x101010)); drawing::rectFill(rect); drawing::setStroke(style.grid); drawing::frameRect(rect);
        drawing::setStroke(drawing::calibratedWhite(0.24, 1.0)); drawing::Path  mid = drawing::Path::bezierPath();
        (mid).moveToPoint(drawing::makePoint(rect.origin.x + 10, drawing::midY(rect))); (mid).lineToPoint(drawing::makePoint(drawing::maxX(rect) - 10, drawing::midY(rect))); (mid).stroke();
        drawing::Path  a = drawing::Path::bezierPath(), b = drawing::Path::bezierPath(), mix = drawing::Path::bezierPath();
        for (uint32_t index = 0; index < 256u; ++index) {
            const float va = topography ? _plugin->guiTerrainA[index].load(std::memory_order_relaxed) : _plugin->guiTableA[index].load(std::memory_order_relaxed);
            const float vb = topography ? _plugin->guiTerrainB[index].load(std::memory_order_relaxed) : _plugin->guiTableB[index].load(std::memory_order_relaxed);
            const double x = rect.origin.x + 10.0 + static_cast<double>(index) / 255.0 * (rect.size.width - 20.0);
            const double ya = drawing::midY(rect) - va * rect.size.height * 0.31, yb = drawing::midY(rect) - vb * rect.size.height * 0.31;
            const double ym = drawing::midY(rect) - s3g::lerp(va, vb, transition) * rect.size.height * 0.31;
            if (index == 0u) { (a).moveToPoint(drawing::makePoint(x, ya)); (b).moveToPoint(drawing::makePoint(x, yb)); (mix).moveToPoint(drawing::makePoint(x, ym)); }
            else { (a).lineToPoint(drawing::makePoint(x, ya)); (b).lineToPoint(drawing::makePoint(x, yb)); (mix).lineToPoint(drawing::makePoint(x, ym)); }
        }
        drawing::setStroke(drawing::calibratedRGB(0.25, 0.72, 0.92, 0.30)); (a).setLineWidth(0.70); (a).stroke();
        drawing::setStroke(drawing::calibratedRGB(0.94, 0.40, 0.68, 0.30)); (b).setLineWidth(0.70); (b).stroke();
        drawing::setStroke(drawing::calibratedWhite(0.92, 0.94)); (mix).setLineWidth(1.30); (mix).stroke();
    };
    drawProfile(terrainRect, true);
    drawProfile(waveRect, false);
    drawing::drawAtPoint_withAttributes("TOPOGRAPHY", drawing::makePoint(terrainRect.origin.x + 8, terrainRect.origin.y + 5), attrs);
    drawing::drawAtPoint_withAttributes(drawing::stringFormat("OSCILLATOR / %@   %.1f HZ   A -> B %.0f%%",
        drawing::stringFromUtf8(s3g::ambiWaveTerrainInterpretationName(_paramsSnapshot.interpretation)),
        static_cast<double>(_plugin->guiFrequency[_selectedVoice].load(std::memory_order_relaxed)),
        static_cast<double>(transition * 100.0f)), drawing::makePoint(waveRect.origin.x + 8, waveRect.origin.y + 5), attrs);
}

drawing::String  valueText(clap_id param)
{
    double value = 0.0; paramsGetValue(&_plugin->plugin, param, &value); char text[64] {};
    paramsValueToText(&_plugin->plugin, param, value, text, sizeof(text)); return drawing::stringFromUtf8(text);
}

void drawControls_valueAttrs_style(drawing::Attrs  attrs, drawing::Attrs  valueAttrs, const drawing::Style& style)
{
    const auto layout = waveGuiLayout(
        _paramsSnapshot.motionMode, _terrainPage, _paramsSnapshot.trace);
    const auto drawToolbox = [&](drawing::String  title, const s3g::gui_layout::Rect& rect) {
        drawing::drawPanelFrame(
            rect.x, rect.y, rect.width, rect.height, style);
        drawing::drawPanelHeader(
            title, true, rect.x, rect.y, rect.width, 21, attrs, style);
    };
    drawToolbox("OUTPUT", layout.output);
    drawToolbox("SOURCE / ENGINE", layout.engine);
    drawToolbox("TERRAIN", layout.terrain);
    static drawing::String  terrainTabs[] = { "FORM", "SKIN", "WARP", "READ" };
    for (int index = 0; index < 4; ++index) {
        drawing::drawToolboxHeaderButton(terrainTabRect(index),
            drawing::cocoaRect(layout.terrain),
            terrainTabs[index], index == _terrainPage, attrs, style);
    }
    drawToolbox("ENVELOPE", layout.envelope);
    drawToolbox("PITCH / FIELD LISTENER", layout.pitchListen);
    drawToolbox("SCAN", layout.scan);
    if (_paramsSnapshot.motionMode == s3g::AmbiWaveTerrainMotionMode::Field) {
        drawToolbox("SELECTION", layout.selection);
    }
    drawToolbox(
        _paramsSnapshot.motionMode == s3g::AmbiWaveTerrainMotionMode::Rotate
            ? "ROTATION" : "TIME FIELDS",
        layout.motion);
    drawToolbox("PROJECTION", layout.projection);
    for (const auto& row : kGuiRows) {
        if (!guiRowVisible(row, _paramsSnapshot.motionMode, _terrainPage, _paramsSnapshot.trace)) continue;
        const double y = effectiveGuiRowY(
            row, layout, _paramsSnapshot.motionMode, _terrainPage, _paramsSnapshot.trace);
        drawing::String  label = drawing::stringFromUtf8(row.label);
        if (row.menu) drawing::drawMenu(label, valueText(row.param), y, attrs, valueAttrs, style, row.panelX + 16, row.panelX + 108, 124);
        else {
            double value = 0.0; paramsGetValue(&_plugin->plugin, row.param, &value); const ParamDef* def = paramDef(row.param);
            const double norm = def ? (row.param == kAzimuthParamId
                ? s3g::aedAzimuthSliderNorm(static_cast<float>(value))
                : std::clamp<double>((value - def->min)
                    / std::max(0.000001, def->max - def->min), 0.0, 1.0))
                : 0.0;
            drawing::drawSlider(label, valueText(row.param), norm, y, attrs, valueAttrs, style, row.panelX + 16, row.panelX + 108, row.panelX + 200, 82);
        }
    }
}

drawing::String  const* menuItems_count_selected(clap_id param, uint32_t* count, int* selected)
{
    static drawing::String  mode[] = { "FREE", "MIDI", "BOTH" };
    static drawing::String  order[] = { "1OA", "2OA", "3OA", "4OA", "5OA", "6OA", "7OA" };
    static drawing::String  form[] = { "SPHERE", "TETRA", "CUBE", "OCTA", "DODECA", "ICOSA" };
    static drawing::String  skin[] = { "HARMONIC", "FBM", "CELL", "VOT", "RIDGES", "DUNES", "CRATERS", "TECTONIC" };
    static drawing::String  read[] = { "HEIGHT", "EDGE", "CURVE", "BLEND", "GRADIENT", "RIDGE", "VALLEY", "NORMAL", "CROSS" };
    static drawing::String  trace[] = { "ORBIT", "LISSAJOUS", "ROSETTE", "FOLD", "POLYGON" };
    static drawing::String  law[] = { "RANDOM", "SERIES", "WEIGHT", "TENDENCY", "MARKOV", "WALK" };
    static drawing::String  join[] = { "LINK", "MERGE", "VARY" };
    static drawing::String  pitch[] = { "NOTE", "TRAVEL" };
    static std::array<drawing::String , s3g::kAmbiWaveTerrainPitchScaleCount> scale = [] {
        std::array<drawing::String , s3g::kAmbiWaveTerrainPitchScaleCount> items {};
        items[0] = "FREE";
        for (uint32_t menuIndex = 0u;
             menuIndex < s3g::kMusicalScaleCount; ++menuIndex) {
            const uint32_t scaleValue =
                s3g::musicalScaleValueForMenuIndex(menuIndex);
            items[menuIndex + 1u] = drawing::stringFromUtf8(s3g::musicalScaleDefinition(scaleValue).name);
        }
        return items;
    }();
    static drawing::String  motion[] = { "FIELD", "ROTATE" };
    static drawing::String  listener[] = { "OFF", "FOLLOW", "COUNTER", "BALANCE" };
    double value = 0.0; paramsGetValue(&_plugin->plugin, param, &value); *selected = static_cast<int>(std::lround(value));
    if (param == kModeParamId) { *count = 3; return mode; }
    if (param == kOrderParamId) { *count = 7; *selected -= 1; return order; }
    if (param == kTerrainFormParamId) { *count = 6; return form; }
    if (param == kSkinParamId) { *count = 8; return skin; }
    if (param == kInterpretationParamId) { *count = 9; return read; }
    if (param == kTraceParamId) { *count = 5; return trace; }
    if (param == kSelectionParamId) { *count = 6; return law; }
    if (param == kPitchModeParamId) { *count = 2; return pitch; }
    if (param == kPitchScaleParamId) {
        const uint32_t scaleValue =
            static_cast<uint32_t>(std::lround(value));
        *selected = scaleValue == 0u ? 0
            : static_cast<int>(
                s3g::musicalScaleMenuIndexForValue(scaleValue - 1u) + 1u);
        *count = s3g::kAmbiWaveTerrainPitchScaleCount;
        return scale.data();
    }
    if (param == kMotionModeParamId) { *count = 2; return motion; }
    if (param == kFieldListenModeParamId) { *count = 4; return listener; }
    *count = 3; return join;
}

drawing::Rect openMenuRect()
{
    const GuiRow* row = guiRow(_openMenuParam); if (!row) return drawing::Rect{};
    const auto layout = waveGuiLayout(
        _paramsSnapshot.motionMode, _terrainPage, _paramsSnapshot.trace);
    const double rowY = effectiveGuiRowY(
        *row, layout, _paramsSnapshot.motionMode, _terrainPage, _paramsSnapshot.trace);
    const uint32_t columns =
        _openMenuParam == kPitchScaleParamId ? 4u : 1u;
    const uint32_t rows = (_menuItemCount + columns - 1u) / columns;
    const double columnWidth =
        _openMenuParam == kPitchScaleParamId ? 180.0 : 124.0;
    const double width = columnWidth * columns;
    const double height = 18.0 * rows;
    double y = rowY + 17;
    if (y + height > bounds().size.height - 4.0) y = rowY - height;
    const double preferredX = row->panelX + 108.0;
    const double x = std::clamp(
        preferredX, 4.0, bounds().size.width - width - 4.0);
    return drawing::makeRect(x, y, width, height);
}

int openMenuHit(drawing::Point point)
{
    const drawing::Rect rect = openMenuRect();
    if (_openMenuParam != kPitchScaleParamId) {
        return drawing::dropdownHitIndex(
            point, rect, 18.0, _menuItemCount);
    }
    return drawing::multiColumnDropdownHitIndex(
        point, rect, 18.0, _menuItemCount, 4u);
}

void drawOpenMenu_style(drawing::Attrs  attrs, const drawing::Style& style)
{
    if (!_openMenuParam) return; int selected = 0; uint32_t count = 0; drawing::String  const* items = menuItems_count_selected(_openMenuParam, &count, &selected);
    _menuItemCount = count;
    const drawing::Rect rect = openMenuRect();
    if (_openMenuParam != kPitchScaleParamId) {
        drawing::drawDropdownMenu(
            rect, 18.0, items, count, selected, _hoverMenuItem, attrs, style);
        return;
    }
    drawing::drawMultiColumnDropdownMenu(
        rect, 18.0, items, count, 4u, selected, _hoverMenuItem,
        attrs, style);
}

void drawOriginal(drawing::Rect dirty)
{
    (void)dirty; refreshControlSnapshot(); const auto style = drawing::softTextStyle(); drawing::setFill(style.bg); drawing::rectFill(bounds());
    drawing::Attrs  attrs = drawing::softLabelAttrs(), values = drawing::softValueAttrs(), title = drawing::softTitleAttrs();
    drawing::drawEncoderTitleBand(
        "s3g AMBI ENCODER WAVE TERRAIN",
        drawing::stringFromUtf8(_titlePresetName),
        drawing::peakDbText(
            _plugin->outputPeak.load(std::memory_order_relaxed)),
        drawing::encoderTitleBand(1158.0, 828.0),
        title, attrs, values, style);
    drawVoiceField_style(attrs, style); drawWaveform_style(attrs, style); drawControls_valueAttrs_style(attrs, values, style); drawOpenMenu_style(attrs, style);
}

int hitVoice(drawing::Point point)
{
    if (!drawing::contains(point, fieldRect())) return -1;
    int best = -1;
    double distance = 12.0;
    const drawing::Rect field = fieldRect();
    const uint32_t voices = std::clamp<uint32_t>(_paramsSnapshot.voices, 1u, s3g::kAmbiWaveTerrainMaxVoices);
    for (uint32_t voice = 0; voice < voices; ++voice) {
        const auto region = activeRegion(voice);
        auto uv = contourUv_phase(region, 0.0f);
        drawing::Point previous = project_rect_depth(worldPoint(displaySurfacePointU_v(uv[0], uv[1])), field, nullptr);
        for (uint32_t index = 1; index <= 36u; ++index) {
            uv = contourUv_phase(region, static_cast<float>(index) / 36.0f);
            const drawing::Point current = project_rect_depth(worldPoint(displaySurfacePointU_v(uv[0], uv[1])), field, nullptr);
            const double vx = current.x - previous.x, vy = current.y - previous.y;
            const double lengthSquared = vx * vx + vy * vy;
            const double amount = lengthSquared > 0.000001
                ? std::clamp(((point.x - previous.x) * vx + (point.y - previous.y) * vy) / lengthSquared, 0.0, 1.0) : 0.0;
            const double nearestX = previous.x + vx * amount, nearestY = previous.y + vy * amount;
            const double candidate = std::hypot(point.x - nearestX, point.y - nearestY);
            if (candidate < distance) { distance = candidate; best = static_cast<int>(voice); }
            previous = current;
        }
    }
    return best;
}

void setParam_point(clap_id param, drawing::Point point)
{
    const GuiRow* row = guiRow(param); const ParamDef* def = paramDef(param); if (!row || !def) return;
    const double norm = std::clamp((static_cast<double>(point.x) - (row->panelX + 108.0)) / 82.0, 0.0, 1.0);
    double value = param == kAzimuthParamId
        ? s3g::aedAzimuthFromSliderNorm(static_cast<float>(norm))
        : def->min + norm * (def->max - def->min);
    if (def->stepped) value = std::round(value);
    editValue(param, value); setNeedsDisplay(true);
}

void mouseDown(drawing::Event* event)
{
    refreshControlSnapshot();
    const drawing::Point point = convertPoint_fromView((event)->locationInWindow(), nullptr);
    const auto titleBand = drawing::encoderTitleBand(1158.0, 828.0);
    if (drawing::contains(point, drawing::cocoaRect(titleBand.presetMenu))) {
        const uint32_t order = _paramsSnapshot.order;
        const float output = _paramsSnapshot.outputGainDb;
        const auto listenMode = _paramsSnapshot.fieldListenMode;
        auto initial = s3g::AmbiWaveTerrainParams {};
        initial.order = order;
        initial.outputGainDb = output;
        initial.fieldListenMode = listenMode;
        if (!queueGuiState(*_plugin, initial)) beep();
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "INIT");
        setNeedsDisplay(true);
        return;
    }
    if (drawing::contains(point, drawing::cocoaRect(titleBand.loadButton))) {
        presetDialog(false);
        return;
    }
    if (drawing::contains(point, drawing::cocoaRect(titleBand.saveButton))) {
        presetDialog(true);
        return;
    }
    if (drawing::contains(point, drawing::cocoaRect(titleBand.randomButton))) {
        auto randomUnit = [] {
            return static_cast<double>(randomBits()) / 4294967295.0;
        };
        auto randomized = _paramsSnapshot;
        (void)stageParam(randomized, kModeParamId, randomBounded(3u));
        (void)stageParam(randomized, kBaseNoteParamId, 28.0 + randomUnit() * 36.0);
        (void)stageParam(randomized, kPitchSpreadParamId, 5.0 + randomUnit() * 34.0);
        (void)stageParam(randomized, kDetuneParamId, 2.0 + randomUnit() * 32.0);
        (void)stageParam(randomized, kSkinParamId, randomBounded(8u));
        (void)stageParam(randomized, kTerrainDepthParamId, 0.24 + randomUnit() * 0.72);
        (void)stageParam(randomized, kTerrainRoughnessParamId, 0.08 + randomUnit() * 0.80);
        (void)stageParam(randomized, kTerrainFoldParamId, randomUnit() * 0.72);
        (void)stageParam(randomized, kTerrainReliefParamId, 0.18 + randomUnit() * 0.78);
        (void)stageParam(randomized, kTraceParamId, randomBounded(5u));
        (void)stageParam(randomized, kInterpretationParamId, randomBounded(9u));
        (void)stageParam(randomized, kPitchScaleParamId,
            randomBounded(s3g::kAmbiWaveTerrainPitchScaleCount));
        (void)stageParam(randomized, kSelectionParamId, randomBounded(6u));
        (void)stageParam(randomized, kTransitionParamId, randomBounded(3u));
        (void)stageParam(randomized, kMotionModeParamId, randomBounded(2u));
        if (!queueGuiState(*_plugin, randomized)) beep();
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "RANDOM");
        setNeedsDisplay(true);
        return;
    }
    if (_openMenuParam) {
        const int hit = openMenuHit(point);
        if (hit >= 0) {
            double value = hit;
            if (_openMenuParam == kOrderParamId) {
                value = hit + 1;
            } else if (_openMenuParam == kPitchScaleParamId) {
                value = hit == 0 ? 0.0
                    : static_cast<double>(
                        s3g::musicalScaleValueForMenuIndex(
                            static_cast<uint32_t>(hit - 1)) + 1u);
            }
            editValue(_openMenuParam, value);
        }
        _openMenuParam = 0; _hoverMenuItem = -1; setNeedsDisplay(true); return;
    }
    for (int index = 0; index < 4; ++index) if (drawing::contains(point, terrainTabRect(index))) { _terrainPage = index; setNeedsDisplay(true); return; }
    for (int index = 0; index < 3; ++index) if (drawing::contains(point, viewButtonRect(index))) { setViewPreset(index); return; }
    for (int index = 0; index < 2; ++index) if (drawing::contains(point, zoomButtonRect(index))) { _viewZoom = std::clamp(_viewZoom + (index ? 0.15 : -0.15), 0.55, 2.4); setNeedsDisplay(true); return; }
    if (drawing::contains(point, fieldRect())) {
        _pendingVoice = hitVoice(point);
        _dragView = true;
        _viewDidDrag = false;
        _lastDragPoint = point;
        return;
    }
    const auto layout = waveGuiLayout(
        _paramsSnapshot.motionMode, _terrainPage, _paramsSnapshot.trace);
    for (const auto& row : kGuiRows) {
        if (!guiRowVisible(row, _paramsSnapshot.motionMode, _terrainPage, _paramsSnapshot.trace)) continue;
        const double rowY = effectiveGuiRowY(
            row, layout, _paramsSnapshot.motionMode, _terrainPage, _paramsSnapshot.trace);
        if (!drawing::contains(point, drawing::makeRect(row.panelX + 8, rowY - 8, 232, 24))) continue;
        if (row.menu) { _openMenuParam = row.param; int selected = 0; menuItems_count_selected(row.param, &_menuItemCount, &selected); _hoverMenuItem = -1; setNeedsDisplay(true); }
        else {
            double defaultValue = 0.0;
            if (drawing::sliderDoubleClickDefault(
                    event, &_plugin->plugin, row.param, &defaultValue)) {
                editValue(row.param, defaultValue);
                _dragParam = 0;
                setNeedsDisplay(true);
                return;
            }
            _dragParam = row.param;
            setParam_point(row.param, point);
        }
        return;
    }
}

void mouseDragged(drawing::Event* event)
{
    const drawing::Point point = convertPoint_fromView((event)->locationInWindow(), nullptr);
    if (_dragView) {
        const double dx = point.x - _lastDragPoint.x;
        const double dy = point.y - _lastDragPoint.y;
        if (std::hypot(dx, dy) >= 0.5) {
            _viewDidDrag = true;
            _pendingVoice = -1;
            _viewAzDeg += dx * 0.42;
            _viewElDeg = std::clamp(_viewElDeg + dy * 0.34, -85.0, 85.0);
            _viewMode = -1;
            _lastDragPoint = point;
            setNeedsDisplay(true);
        }
        return;
    }
    if (_dragParam) setParam_point(_dragParam, point);
}

void mouseUp(drawing::Event* event)
{
    (void)event;
    if (_dragView && !_viewDidDrag && _pendingVoice >= 0) {
        _selectedVoice = static_cast<uint32_t>(_pendingVoice);
        _plugin->guiSelectedVoice.store(_selectedVoice, std::memory_order_relaxed);
        setNeedsDisplay(true);
    }
    _dragParam = 0;
    _dragView = false;
    _viewDidDrag = false;
    _pendingVoice = -1;
}

void mouseMoved(drawing::Event* event)
{
    if (!_openMenuParam) return; const drawing::Point point = convertPoint_fromView((event)->locationInWindow(), nullptr);
    const int hover = openMenuHit(point);
    if (hover != _hoverMenuItem) { _hoverMenuItem = hover; setNeedsDisplay(true); }
}
};
} // namespace map_encoder_canvas
s3g::portable_gui::foundation::ContentView* makePortableCanvas(Plugin& p) {
  return new map_encoder_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
