// Original Cocoa geometry, camera math and hit maps retained.
namespace complex_processor_canvas {
namespace drawing = s3g::portable_gui::complex_drawing;
namespace foundation = s3g::portable_gui::foundation;
constexpr auto kImprintOutputPanel =
    s3g::gui_layout::imprintPanel(s3g::gui_layout::PanelRole::Output, 42.0, 2u);
constexpr s3g::gui_layout::Panel kImprintSourcePanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Source,
    {616.0, 134.0, 272.0, 126.0},
    36.0,
    26.0,
    0u,
};
constexpr auto kImprintProcessPanel = s3g::gui_layout::imprintPanel(
    s3g::gui_layout::PanelRole::Engine, 272.0, 5u);
constexpr std::array kImprintPanels{kImprintOutputPanel, kImprintSourcePanel,
                                    kImprintProcessPanel};
static_assert(s3g::gui_layout::validateColumn(
    kImprintPanels, s3g::gui_layout::kImprintFamilyLayout.canvas));

float linearToSrgb(float value) {
  const float x = std::clamp(value, 0.0f, 1.0f);
  return x <= 0.0031308f ? x * 12.92f
                         : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

VSTGUI::CColor imprintPointColor(float azimuthDeg, float elevationDeg) {
  const float hue = std::fmod(azimuthDeg / 360.0f + 1.0f, 1.0f);
  const float light = std::clamp((elevationDeg + 90.0f) / 180.0f, 0.34f, 0.78f);
  const float chroma = 0.24f;
  const float a = std::cos(hue * 2.0f * s3g::kPi) * chroma;
  const float b = std::sin(hue * 2.0f * s3g::kPi) * chroma;
  const float l3 = light + 0.3963377774f * a + 0.2158037573f * b;
  const float m3 = light - 0.1055613458f * a - 0.0638541728f * b;
  const float s3 = light - 0.0894841775f * a - 1.2914855480f * b;
  const float l = l3 * l3 * l3;
  const float m = m3 * m3 * m3;
  const float s = s3 * s3 * s3;
  const float red =
      linearToSrgb(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s);
  const float green =
      linearToSrgb(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s);
  const float blue =
      linearToSrgb(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);
  return drawing::calibratedRGB(red, green, blue, 0.88);
}

drawing::String compactFileName(const std::string &name) {
  std::vector<size_t> boundary;
  for (size_t i = 0; i < name.size(); ++i)
    if ((static_cast<unsigned char>(name[i]) & 0xc0u) != 0x80u)
      boundary.push_back(i);
  return boundary.size() <= 27u
             ? name
             : "..." + name.substr(boundary[boundary.size() - 24u]);
}

struct ImprintAtlasEntry {
  const char *title;
  const char *resource;
};

constexpr std::array<ImprintAtlasEntry, 19> kImprintAtlas{{
    {"ARCH / CONCRETE GALLERY", "architecture_concrete_gallery"},
    {"ARCH / WOOD STUDIO", "architecture_wood_studio"},
    {"CAVE / LIMESTONE POCKET", "cave_limestone_pocket"},
    {"CAVE / ICE GROTTO", "cave_ice_grotto"},
    {"CAVERN / DEEP STONE VAULT", "cavern_deep_stone_vault"},
    {"CAVERN / WATER CHAMBER", "cavern_water_chamber"},
    {"TUNNEL / BRICK BEND", "tunnel_brick_bend"},
    {"TUNNEL / METAL CONDUIT", "tunnel_metal_conduit"},
    {"CANYON / DRY SLOT", "canyon_dry_slot"},
    {"CANYON / POROUS GORGE", "canyon_porous_gorge"},
    {"CLEAR / FOREST RING", "clearing_forest_ring"},
    {"CLEAR / WATER MEADOW", "clearing_water_meadow"},
    {"ECHO / STONE FLUTTER", "echo_stone_flutter_gallery"},
    {"ECHO / METAL AXIAL TUNNEL", "echo_metal_axial_tunnel"},
    {"ECHO / TWIN CHAMBERS", "echo_twin_concrete_chambers"},
    {"ECHO / PERIMETER CIRCUIT", "echo_stone_perimeter_circuit"},
    {"ECHO / IMPOSSIBLE RELAY", "echo_impossible_relay"},
    {"ABSTRACT / FOLDED CHAMBER", "abstract_folded_chamber"},
    {"ABSTRACT / IMPOSSIBLE NETWORK", "abstract_impossible_network"},
}};

drawing::Rect imprintAtlasMenuRect() {
  return drawing::makeRect(kImprintSourcePanel.frame.x, 78.0,
                           kImprintSourcePanel.frame.width,
                           18.0 * static_cast<double>(kImprintAtlas.size()));
}

class Editor final : public s3g::portable_gui::routing::Canvas {
public:
  using Canvas = s3g::portable_gui::routing::Canvas;

  Plugin *_plugin;
  clap_id _dragParam;
  bool _orderMenuOpen;
  int _orderMenuHover;
  bool _atlasMenuOpen;
  int _atlasMenuHover;
  int _viewMode;
  double _viewAzimuthDeg;
  double _viewElevationDeg;
  double _viewZoom;
  bool _dragView;
  drawing::Point _lastDragPoint;
  char _titlePresetName[64];
#include "s3g_complex_processor_imprint_editor.inc"
  void storeViewState() {
    if (!_plugin)
      return;
    _plugin->guiViewMode = _viewMode;
    _plugin->guiViewAzimuthDeg = _viewAzimuthDeg;
    _plugin->guiViewElevationDeg = _viewElevationDeg;
    _plugin->guiViewZoom = _viewZoom;
  }

  void setStatus(const std::string &status) {
    std::lock_guard<std::mutex> lock(_plugin->stateMutex);
    _plugin->status = status;
  }

  void drawSlider_value_norm_row_panel_attrs_style(
      drawing::String name, drawing::String value, double norm, uint32_t row,
      const s3g::gui_layout::Panel &panel, drawing::Attrs attrs,
      drawing::Style &style) {
    drawing::drawProcessorSlider(
        name, value, norm, s3g::gui_layout::rowY(panel, row), panel.frame.x,
        panel.frame.width, attrs, attrs, style);
  }

  drawing::Rect viewButtonRect(int index) {
    const drawing::Rect panel =
        drawing::cocoaRect(s3g::gui_layout::kImprintFamilyLayout.fieldPanel);
    const double width = 38.0;
    const double gap = 5.0;
    const double x = drawing::maxX(panel) - 10.0 -
                     (3.0 - static_cast<double>(index)) * width -
                     (2.0 - static_cast<double>(index)) * gap;
    return drawing::makeRect(x, panel.origin.y + 4.0, width, 13.0);
  }

  drawing::Rect zoomButtonRect(int index) {
    const double width = 18.0;
    const double gap = 4.0;
    const double viewStart = viewButtonRect(0).origin.x;
    const double x = viewStart - 12.0 -
                     (2.0 - static_cast<double>(index)) * width -
                     (1.0 - static_cast<double>(index)) * gap;
    return drawing::makeRect(
        x, s3g::gui_layout::kImprintFamilyLayout.fieldPanel.y + 4.0, width,
        13.0);
  }

  void setViewPreset(int mode) {
    _viewMode = mode;
    if (mode == 0) {
      _viewAzimuthDeg = 0.0;
      _viewElevationDeg = 0.0;
    } else if (mode == 1) {
      _viewAzimuthDeg = 0.0;
      _viewElevationDeg = 90.0;
    } else {
      _viewAzimuthDeg = 35.0;
      _viewElevationDeg = 34.0;
    }
    storeViewState();
    setNeedsDisplay(true);
  }

  void drawRoom_rect_attrs(const GuiSnapshot &snapshot, drawing::Rect field,
                           drawing::Attrs attrs) {
    drawing::setFill(drawing::color(0x101010));
    drawing::rectFill(field);
    drawing::setStroke(drawing::color(0x4d4d4d));
    drawing::frameRect(field);

    std::vector<std::array<float, 2>> polygon = snapshot.polygon;
    if (polygon.size() < 3u) {
      polygon = {{0.0f, 0.0f},
                 {snapshot.roomWidth, 0.0f},
                 {snapshot.roomWidth, snapshot.roomDepth},
                 {0.0f, snapshot.roomDepth}};
    }

    float minX = polygon[0][0];
    float maxX = polygon[0][0];
    float minY = polygon[0][1];
    float maxY = polygon[0][1];
    float minZ = 0.0f;
    float maxZ = std::max(0.5f, snapshot.roomHeight);
    auto includePoint = [&](float x, float y, float z) {
      minX = std::min(minX, x);
      maxX = std::max(maxX, x);
      minY = std::min(minY, y);
      maxY = std::max(maxY, y);
      minZ = std::min(minZ, z);
      maxZ = std::max(maxZ, z);
    };
    for (const auto &point : polygon)
      includePoint(point[0], point[1], 0.0f);
    for (const auto &point : snapshot.ceilingProfile)
      includePoint(point[0], minY, point[1]);
    for (const auto &branch : snapshot.branches) {
      for (const auto &point : branch.polygon) {
        includePoint(point[0], point[1], branch.baseHeight);
        includePoint(point[0], point[1], branch.baseHeight + branch.height);
      }
    }
    for (const auto &portal : snapshot.portals) {
      for (const auto &point : portal.outline)
        includePoint(point[0], point[1], point[2]);
    }
    includePoint(snapshot.listenerX, snapshot.listenerY, snapshot.listenerZ);
    for (uint32_t i = 0; i < snapshot.profileCount; ++i) {
      const auto &profile = snapshot.profiles[i];
      includePoint(profile.sourceX, profile.sourceY, profile.sourceZ);
    }

    const float centerX = (minX + maxX) * 0.5f;
    const float centerY = (minY + maxY) * 0.5f;
    const float centerZ = (minZ + maxZ) * 0.5f;
    const float worldSpan =
        std::max({0.001f, maxX - minX, maxY - minY, maxZ - minZ});
    const float azimuth = static_cast<float>(_viewAzimuthDeg * M_PI / 180.0);
    const float elevation =
        static_cast<float>(_viewElevationDeg * M_PI / 180.0);
    const float ca = std::cos(azimuth);
    const float sa = std::sin(azimuth);
    const float ce = std::cos(elevation);
    const float se = std::sin(elevation);
    auto rotatePoint = [&](float x, float y, float z) {
      const float nx = (x - centerX) / worldSpan;
      const float ny = (y - centerY) / worldSpan;
      const float nz = (z - centerZ) / worldSpan;
      const float x1 = ca * nx - sa * ny;
      const float y1 = sa * nx + ca * ny;
      const float y2 = ce * y1 + se * nz;
      const float z2 = -se * y1 + ce * nz;
      return std::array<float, 3>{x1, y2, z2};
    };
    float projectedMinX = std::numeric_limits<float>::max();
    float projectedMaxX = std::numeric_limits<float>::lowest();
    float projectedMinY = std::numeric_limits<float>::max();
    float projectedMaxY = std::numeric_limits<float>::lowest();
    for (float x : {minX, maxX}) {
      for (float y : {minY, maxY}) {
        for (float z : {minZ, maxZ}) {
          const auto rotated = rotatePoint(x, y, z);
          projectedMinX = std::min(projectedMinX, rotated[0]);
          projectedMaxX = std::max(projectedMaxX, rotated[0]);
          projectedMinY = std::min(projectedMinY, rotated[1]);
          projectedMaxY = std::max(projectedMaxY, rotated[1]);
        }
      }
    }
    const double scale =
        std::min((field.size.width - 42.0) /
                     std::max(0.001f, projectedMaxX - projectedMinX),
                 (field.size.height - 42.0) /
                     std::max(0.001f, projectedMaxY - projectedMinY)) *
        std::clamp(_viewZoom, 0.55, 2.20);
    auto projectPoint = [&](float x, float y, float z,
                            double *depth = nullptr) {
      const auto rotated = rotatePoint(x, y, z);
      const float projectedCenterX = (projectedMinX + projectedMaxX) * 0.5f;
      const float projectedCenterY = (projectedMinY + projectedMaxY) * 0.5f;
      if (depth)
        *depth = static_cast<double>(rotated[2]);
      return drawing::makePoint(
          drawing::midX(field) +
              static_cast<double>(rotated[0] - projectedCenterX) * scale,
          drawing::midY(field) -
              static_cast<double>(rotated[1] - projectedCenterY) * scale);
    };
    auto ceilingAt = [&](float x) {
      if (snapshot.ceilingProfile.size() < 2u)
        return snapshot.roomHeight;
      if (x <= snapshot.ceilingProfile.front()[0])
        return snapshot.ceilingProfile.front()[1];
      if (x >= snapshot.ceilingProfile.back()[0])
        return snapshot.ceilingProfile.back()[1];
      for (size_t i = 1; i < snapshot.ceilingProfile.size(); ++i) {
        const auto &a = snapshot.ceilingProfile[i - 1u];
        const auto &b = snapshot.ceilingProfile[i];
        if (x > b[0])
          continue;
        const float amount = (x - a[0]) / std::max(0.0001f, b[0] - a[0]);
        return a[1] + (b[1] - a[1]) * amount;
      }
      return snapshot.roomHeight;
    };

    drawing::GraphicsScope graphics(*context);
    drawing::clipRect(drawing::inset(field, 1.0, 1.0));
    auto drawGeometry = [&](const std::vector<std::array<float, 2>> &points,
                            float baseHeight, float fixedHeight,
                            bool variableHeight, VSTGUI::CColor stroke,
                            VSTGUI::CColor fill) {
      if (points.size() < 3u)
        return;
      auto heightAt = [&](float x) {
        return variableHeight ? ceilingAt(x) : baseHeight + fixedHeight;
      };
      drawing::Path roof = drawing::Path::bezierPath();
      roof.moveToPoint(
          projectPoint(points[0][0], points[0][1], heightAt(points[0][0])));
      for (size_t i = 1; i < points.size(); ++i) {
        roof.lineToPoint(
            projectPoint(points[i][0], points[i][1], heightAt(points[i][0])));
      }
      roof.closePath();
      drawing::setFill(fill);
      roof.fill();

      drawing::Path edges = drawing::Path::bezierPath();
      const size_t verticalStride = std::max<size_t>(1u, points.size() / 8u);
      for (size_t i = 0; i < points.size(); ++i) {
        const auto &a = points[i];
        const auto &b = points[(i + 1u) % points.size()];
        edges.moveToPoint(projectPoint(a[0], a[1], baseHeight));
        edges.lineToPoint(projectPoint(b[0], b[1], baseHeight));
        edges.moveToPoint(projectPoint(a[0], a[1], heightAt(a[0])));
        edges.lineToPoint(projectPoint(b[0], b[1], heightAt(b[0])));
        if (i % verticalStride == 0u) {
          edges.moveToPoint(projectPoint(a[0], a[1], baseHeight));
          edges.lineToPoint(projectPoint(a[0], a[1], heightAt(a[0])));
        }
      }
      drawing::setStroke(stroke);
      edges.setLineWidth(1.1);
      edges.stroke();
    };

    drawGeometry(polygon, 0.0f, snapshot.roomHeight, true,
                 drawing::color(0x858585, 0.78),
                 drawing::color(0x747474, 0.075));
    for (const auto &branch : snapshot.branches) {
      int color = 0x66746e;
      if (branch.family == "room")
        color = 0x727572;
      else if (branch.family == "cavern")
        color = 0x60766f;
      else if (branch.family == "tunnel")
        color = 0x707269;
      else if (branch.family == "canyon")
        color = 0x767063;
      else if (branch.family == "clearing")
        color = 0x647665;
      else if (branch.family == "abstract")
        color = 0x726a78;
      drawGeometry(branch.polygon, branch.baseHeight,
                   std::max(0.1f, branch.height), false,
                   drawing::color(color, 0.70), drawing::color(color, 0.055));
    }

    for (const auto &portal : snapshot.portals) {
      if (portal.outline.size() < 3u)
        continue;
      drawing::Path path = drawing::Path::bezierPath();
      path.moveToPoint(projectPoint(portal.outline[0][0], portal.outline[0][1],
                                    portal.outline[0][2]));
      for (size_t index = 1; index < portal.outline.size(); ++index) {
        path.lineToPoint(projectPoint(portal.outline[index][0],
                                      portal.outline[index][1],
                                      portal.outline[index][2]));
      }
      path.closePath();
      drawing::setFill(drawing::color(0x101010, 0.72));
      path.fill();
      drawing::setStroke(drawing::color(0xb8dce6, 0.82));
      const double dash[] = {4.0, 3.0};
      path.setLineDash_count_phase(dash, 2, 0.0);
      path.setLineWidth(1.15);
      path.stroke();
    }

    const drawing::Point listener = projectPoint(
        snapshot.listenerX, snapshot.listenerY, snapshot.listenerZ);
    float maximumListenEnvelope = 0.0f;
    for (uint32_t i = 0u; i < snapshot.profileCount; ++i) {
      maximumListenEnvelope =
          std::max(maximumListenEnvelope, snapshot.profiles[i].listenEnvelope);
    }
    const bool listening =
        viewParams.fieldListenMode != s3g::AmbiFieldListenMode::Off;
    for (uint32_t i = 0; i < snapshot.profileCount; ++i) {
      const auto &profile = snapshot.profiles[i];
      const drawing::Point source =
          projectPoint(profile.sourceX, profile.sourceY, profile.sourceZ);
      const float heard =
          maximumListenEnvelope > 1.0e-7f
              ? std::clamp(profile.listenEnvelope / maximumListenEnvelope, 0.0f,
                           1.0f)
              : 0.0f;
      drawing::Path listenPath = drawing::Path::bezierPath();
      listenPath.moveToPoint(listener);
      listenPath.lineToPoint(source);
      drawing::setStroke(
          drawing::color(listening ? 0x9aa9ad : 0x6a6a6a,
                         listening ? 0.24 + heard * 0.62 : 0.42));
      listenPath.setLineWidth(listening ? 0.8 + heard * 1.35 : 1.0);
      listenPath.stroke();
      const double halfSize = listening ? 4.5 + heard * 2.5 : 5.0;
      drawing::setFill(
          imprintPointColor(profile.azimuthDeg, profile.elevationDeg));
      drawing::rectFill(drawing::makeRect(source.x - halfSize,
                                          source.y - halfSize, halfSize * 2.0,
                                          halfSize * 2.0));
      drawing::setStroke(drawing::color(0x111111, 0.8));
      drawing::frameRect(drawing::makeRect(source.x - halfSize,
                                           source.y - halfSize, halfSize * 2.0,
                                           halfSize * 2.0));
      drawing::String label =
          listening
              ? drawing::stringFormat("%u  %.2fx", i + 1u, profile.listenWeight)
              : drawing::stringFormat("%u", i + 1u);
      drawing::drawAtPoint_withAttributes(
          label, drawing::makePoint(source.x + halfSize + 3.0, source.y - 7.0),
          attrs);
    }
    drawing::setFill(drawing::color(0xb0b0b0));
    drawing::rectFill(
        drawing::makeRect(listener.x - 4.0, listener.y - 4.0, 8.0, 8.0));
    drawing::setStroke(drawing::color(0x202020));
    drawing::frameRect(
        drawing::makeRect(listener.x - 4.0, listener.y - 4.0, 8.0, 8.0));

    if (snapshot.profileCount == 0u) {
      drawing::String empty = "LOAD AN IMPRINT .S3GIMPRINT";
      const drawing::Size size = drawing::sizeWithAttributes(empty, attrs);
      drawing::drawAtPoint_withAttributes(
          empty,
          drawing::makePoint(drawing::midX(field) - size.width * 0.5,
                             drawing::midY(field) - 6.0),
          attrs);
    }
    graphics.restore();
  }

  void drawOriginal(drawing::Rect dirtyRect) {
    (void)dirtyRect;
    drawing::Style style;
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs text = drawing::softLabelAttrs();
    drawing::Attrs value = drawing::softValueAttrs();
    const GuiSnapshot snapshot = guiSnapshot(*_plugin);
    const float peak = _plugin->outputPeak.exchange(
        _plugin->outputPeak.load(std::memory_order_relaxed) * 0.92f,
        std::memory_order_relaxed);
    drawing::String peakText = drawing::peakDbText(peak);
    const auto titleBand = s3g::gui_layout::imprintTitleBand(
        s3g::gui_layout::kImprintFamilyLayout.canvas);
    drawing::drawImprintTitleBand("s3g PROCESSOR AMBI IMPRINT 64CH",
                                  drawing::stringFromUtf8(_titlePresetName),
                                  peakText, titleBand, style);

    const drawing::Rect fieldPanel =
        drawing::cocoaRect(s3g::gui_layout::kImprintFamilyLayout.fieldPanel);
    const drawing::Rect imprintPanel =
        drawing::cocoaRect(kImprintSourcePanel.frame);
    const drawing::Rect processPanel =
        drawing::cocoaRect(kImprintProcessPanel.frame);
    const drawing::Rect outputPanel =
        drawing::cocoaRect(kImprintOutputPanel.frame);
    drawing::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y,
                            fieldPanel.size.width, fieldPanel.size.height,
                            style);
    drawing::drawPanelHeader("IMPRINT FIELD", true, fieldPanel.origin.x,
                             fieldPanel.origin.y, fieldPanel.size.width, 21,
                             text, style);
    static drawing::String viewLabels[] = {"TOP", "SIDE", "3/4"};
    for (int i = 0; i < 3; ++i) {
      drawing::drawToolboxHeaderButton(viewButtonRect(i), fieldPanel,
                                       viewLabels[i], i == _viewMode, value,
                                       style);
    }
    static drawing::String zoomLabels[] = {"-", "+"};
    for (int i = 0; i < 2; ++i) {
      drawing::drawToolboxHeaderButton(zoomButtonRect(i), fieldPanel,
                                       zoomLabels[i], false, value, style);
    }
    drawRoom_rect_attrs(
        snapshot,
        drawing::cocoaRect(s3g::gui_layout::kImprintFamilyLayout.field), value);

    drawing::drawPanelFrame(outputPanel.origin.x, outputPanel.origin.y,
                            outputPanel.size.width, outputPanel.size.height,
                            style);
    drawing::drawPanelHeader("OUTPUT", true, outputPanel.origin.x,
                             outputPanel.origin.y, outputPanel.size.width, 21,
                             text, style);
    drawSlider_value_norm_row_panel_attrs_style(
        "OUT", drawing::stringFormat("%+.1f DB", viewParams.outputGainDb),
        (viewParams.outputGainDb + 60.0f) / 72.0f, 0, kImprintOutputPanel,
        value, style);
    drawing::drawToggle(
        "BYP", viewParams.bypass,
        s3g::gui_layout::rowY(kImprintOutputPanel, 1u), text, value, style,
        s3g::gui_layout::processorLabelX(kImprintOutputPanel.frame.x),
        s3g::gui_layout::processorControlX(kImprintOutputPanel.frame.x),
        s3g::gui_layout::processorMenuWidth(kImprintOutputPanel.frame.width));

    drawing::drawPanelFrame(imprintPanel.origin.x, imprintPanel.origin.y,
                            imprintPanel.size.width, imprintPanel.size.height,
                            style);
    drawing::drawPanelHeader("IMPRINT", true, imprintPanel.origin.x,
                             imprintPanel.origin.y, imprintPanel.size.width, 21,
                             text, style);
    const drawing::Rect imprintHeader =
        drawing::makeRect(imprintPanel.origin.x, imprintPanel.origin.y,
                          imprintPanel.size.width, 21.0);
    const drawing::Rect atlasButton = drawing::makeRect(
        imprintPanel.origin.x + 115.0, imprintPanel.origin.y + 2.0, 71.0, 17.0);
    const drawing::Rect imprintLoadButton = drawing::makeRect(
        imprintPanel.origin.x + 191.0, imprintPanel.origin.y + 2.0, 68.0, 17.0);
    drawing::drawToolboxHeaderActionButton(atlasButton, imprintHeader, "ATLAS",
                                           value, style);
    drawing::drawToolboxHeaderActionButton(imprintLoadButton, imprintHeader,
                                           "LOAD", value, style);
    const double imprintLabelX = imprintPanel.origin.x + 12.0;
    const double imprintValueX = imprintPanel.origin.x + 60.0;
    drawing::drawAtPoint_withAttributes(
        "FILE", drawing::makePoint(imprintLabelX, imprintPanel.origin.y + 35.0),
        text);
    drawing::drawAtPoint_withAttributes(
        compactFileName(snapshot.name),
        drawing::makePoint(imprintValueX, imprintPanel.origin.y + 35.0), value);
    drawing::drawAtPoint_withAttributes(
        "STAT", drawing::makePoint(imprintLabelX, imprintPanel.origin.y + 57.0),
        text);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFromUtf8(snapshot.status.c_str()),
        drawing::makePoint(imprintValueX, imprintPanel.origin.y + 57.0), value);
    drawing::drawAtPoint_withAttributes(
        "DIR", drawing::makePoint(imprintLabelX, imprintPanel.origin.y + 79.0),
        text);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("%u", snapshot.profileCount),
        drawing::makePoint(imprintValueX, imprintPanel.origin.y + 79.0), value);
    drawing::drawAtPoint_withAttributes(
        "LEN",
        drawing::makePoint(imprintPanel.origin.x + 122.0,
                           imprintPanel.origin.y + 79.0),
        text);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("%.2f S", snapshot.duration),
        drawing::makePoint(imprintPanel.origin.x + 162.0,
                           imprintPanel.origin.y + 79.0),
        value);
    drawing::drawAtPoint_withAttributes(
        "SPACE",
        drawing::makePoint(imprintLabelX, imprintPanel.origin.y + 101.0), text);
    drawing::String family = drawing::uppercaseString(
        drawing::stringFromUtf8(snapshot.family.c_str()));
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat(
            "%@ %.1f x %.1f x %.1f", !family.empty() ? family : "ROOM",
            snapshot.roomWidth, snapshot.roomDepth, snapshot.roomHeight),
        drawing::makePoint(imprintValueX, imprintPanel.origin.y + 101.0),
        value);

    drawing::drawPanelFrame(processPanel.origin.x, processPanel.origin.y,
                            processPanel.size.width, processPanel.size.height,
                            style);
    drawing::drawPanelHeader("PROCESS", true, processPanel.origin.x,
                             processPanel.origin.y, processPanel.size.width, 21,
                             text, style);
    drawing::drawProcessorMenu(
        "ORD", drawing::stringFormat("%uOA", viewParams.order),
        s3g::gui_layout::rowY(kImprintProcessPanel, 0u),
        kImprintProcessPanel.frame.x, kImprintProcessPanel.frame.width, text,
        value, style);
    drawSlider_value_norm_row_panel_attrs_style(
        "MIX", drawing::stringFormat("%.0f%%", viewParams.mix * 100.0f),
        viewParams.mix, 1, kImprintProcessPanel, value, style);
    drawSlider_value_norm_row_panel_attrs_style(
        "FOC", drawing::stringFormat("%.0f%%", viewParams.focus * 100.0f),
        viewParams.focus, 2, kImprintProcessPanel, value, style);
    drawSlider_value_norm_row_panel_attrs_style(
        "WID", drawing::stringFormat("%.2f", viewParams.width),
        viewParams.width / 1.5f, 3, kImprintProcessPanel, value, style);
    drawing::drawAtPoint_withAttributes(
        "LST",
        drawing::makePoint(
            s3g::gui_layout::processorLabelX(kImprintProcessPanel.frame.x),
            s3g::gui_layout::rowY(kImprintProcessPanel, 4u) - 2.0),
        text);
    static drawing::String listenLabels[] = {"OFF", "FOL", "CTR", "BAL"};
    const uint32_t listenMode =
        static_cast<uint32_t>(viewParams.fieldListenMode);
    for (uint32_t mode = 0u; mode < 4u; ++mode) {
      const drawing::Rect button = drawing::makeRect(
          s3g::gui_layout::processorControlX(kImprintProcessPanel.frame.x) +
              mode * 38.0,
          s3g::gui_layout::rowY(kImprintProcessPanel, 4u) - 4.0, 35.0, 18.0);
      drawing::drawHeaderButton(button, processPanel, listenLabels[mode],
                                mode == listenMode, value, style);
    }

    if (_orderMenuOpen) {
      drawing::String orderItems[] = {"1OA", "2OA", "3OA", "4OA",
                                      "5OA", "6OA", "7OA"};
      const auto orderBox =
          s3g::gui_layout::menuBoxRect(kImprintProcessPanel, 0u);
      drawing::drawDropdownMenu(
          drawing::makeRect(orderBox.x, orderBox.y + orderBox.height + 3.0,
                            orderBox.width, 126.0),
          18, orderItems, 7u, static_cast<int>(viewParams.order) - 1,
          _orderMenuHover, value, style);
    }
    if (_atlasMenuOpen) {
      drawing::String atlasItems[kImprintAtlas.size()];
      for (size_t i = 0; i < kImprintAtlas.size(); ++i)
        atlasItems[i] = drawing::stringFromUtf8(kImprintAtlas[i].title);
      drawing::drawDropdownMenu(imprintAtlasMenuRect(), 18, atlasItems,
                                static_cast<uint32_t>(kImprintAtlas.size()), -1,
                                _atlasMenuHover, value, style);
    }
  }

  void updateSliderAtPoint(drawing::Point point) {
    const auto &panel =
        _dragParam == kParamOutput ? kImprintOutputPanel : kImprintProcessPanel;
    const double norm = std::clamp(
        (point.x - s3g::gui_layout::processorControlX(panel.frame.x)) /
            s3g::gui_layout::processorTrackWidth(panel.frame.width),
        0.0, 1.0);
    switch (_dragParam) {
    case kParamMix:
      setParam_value(kParamMix, norm);
      break;
    case kParamFocus:
      setParam_value(kParamFocus, norm);
      break;
    case kParamWidth:
      setParam_value(kParamWidth, norm * 1.5);
      break;
    case kParamOutput:
      setParam_value(kParamOutput, -60.0 + norm * 72.0);
      break;
    default:
      break;
    }
  }

  void mouseDown(drawing::Event *event) {
    const drawing::Point point =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    const auto titleBand = s3g::gui_layout::imprintTitleBand(
        s3g::gui_layout::kImprintFamilyLayout.canvas);
    if (handleTitleClick(point, titleBand)) {
      setNeedsDisplay(true);
      return;
    }
    if (_atlasMenuOpen) {
      const int hit = drawing::dropdownHitIndex(
          point, imprintAtlasMenuRect(), 18,
          static_cast<uint32_t>(kImprintAtlas.size()));
      _atlasMenuOpen = false;
      _atlasMenuHover = -1;
      if (hit >= 0)
        loadAtlasAtIndex(static_cast<size_t>(hit));
      setNeedsDisplay(true);
      return;
    }
    if (_orderMenuOpen) {
      const auto orderBox =
          s3g::gui_layout::menuBoxRect(kImprintProcessPanel, 0u);
      const int hit = drawing::dropdownHitIndex(
          point,
          drawing::makeRect(orderBox.x, orderBox.y + orderBox.height + 3.0,
                            orderBox.width, 126.0),
          18, 7u);
      _orderMenuOpen = false;
      _orderMenuHover = -1;
      if (hit >= 0)
        setParam_value(kParamOrder, hit + 1);
      setNeedsDisplay(true);
      return;
    }
    const drawing::Rect imprintLoadButton =
        drawing::makeRect(kImprintSourcePanel.frame.x + 191.0,
                          kImprintSourcePanel.frame.y + 2.0, 68.0, 17.0);
    const drawing::Rect atlasButton =
        drawing::makeRect(kImprintSourcePanel.frame.x + 115.0,
                          kImprintSourcePanel.frame.y + 2.0, 71.0, 17.0);
    if (drawing::contains(point, imprintLoadButton)) {
      loadImprint();
      return;
    }
    if (drawing::contains(point, atlasButton)) {
      _atlasMenuOpen = true;
      _atlasMenuHover = -1;
      _orderMenuOpen = false;
      setNeedsDisplay(true);
      return;
    }
    for (int i = 0; i < 3; ++i) {
      if (drawing::contains(point, viewButtonRect(i))) {
        setViewPreset(i);
        return;
      }
    }
    for (int i = 0; i < 2; ++i) {
      if (drawing::contains(point, zoomButtonRect(i))) {
        _viewZoom = std::clamp(_viewZoom * (i == 0 ? 0.86 : 1.16), 0.55, 2.20);
        storeViewState();
        setNeedsDisplay(true);
        return;
      }
    }
    if (drawing::contains(point,
                          drawing::cocoaRect(s3g::gui_layout::menuBoxRect(
                              kImprintProcessPanel, 0u)))) {
      _orderMenuOpen = true;
      setNeedsDisplay(true);
      return;
    }
    for (uint32_t mode = 0u; mode < 4u; ++mode) {
      if (drawing::contains(
              point, drawing::makeRect(
                         s3g::gui_layout::processorControlX(
                             kImprintProcessPanel.frame.x) +
                             mode * 38.0,
                         s3g::gui_layout::rowY(kImprintProcessPanel, 4u) - 4.0,
                         35.0, 18.0))) {
        setParam_value(kParamFieldListen, mode);
        return;
      }
    }
    if (drawing::contains(point,
                          drawing::cocoaRect(s3g::gui_layout::sliderHitRect(
                              kImprintOutputPanel, 1u)))) {
      setParam_value(kParamBypass, viewParams.bypass ? 0.0 : 1.0);
      return;
    }
    struct Row {
      clap_id param;
      const s3g::gui_layout::Panel *panel;
      uint32_t row;
    };
    const Row rows[] = {
        {kParamOutput, &kImprintOutputPanel, 0u},
        {kParamMix, &kImprintProcessPanel, 1u},
        {kParamFocus, &kImprintProcessPanel, 2u},
        {kParamWidth, &kImprintProcessPanel, 3u},
    };
    for (const auto &row : rows) {
      if (!drawing::contains(
              point, drawing::cocoaRect(
                         s3g::gui_layout::sliderHitRect(*row.panel, row.row))))
        continue;
      double defaultValue = 0.0;
      if (drawing::sliderDoubleClickDefault(event, &_plugin->plugin, row.param,
                                            &defaultValue)) {
        setParam_value(row.param, defaultValue);
        _dragParam = CLAP_INVALID_ID;
      } else {
        _dragParam = row.param;
        updateSliderAtPoint(point);
      }
      return;
    }
    if (drawing::contains(
            point,
            drawing::cocoaRect(s3g::gui_layout::kImprintFamilyLayout.field))) {
      _dragView = true;
      _lastDragPoint = point;
    }
  }

  void mouseDragged(drawing::Event *event) {
    const drawing::Point point =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    if (_dragView) {
      _viewAzimuthDeg += (point.x - _lastDragPoint.x) * 0.62;
      _viewElevationDeg = std::clamp(
          _viewElevationDeg + (point.y - _lastDragPoint.y) * 0.52, -89.0, 89.0);
      while (_viewAzimuthDeg > 180.0)
        _viewAzimuthDeg -= 360.0;
      while (_viewAzimuthDeg < -180.0)
        _viewAzimuthDeg += 360.0;
      _viewMode = -1;
      _lastDragPoint = point;
      storeViewState();
      setNeedsDisplay(true);
      return;
    }
    if (_dragParam != CLAP_INVALID_ID)
      updateSliderAtPoint(point);
  }

  void mouseUp(drawing::Event *event) {
    (void)event;
    _dragParam = CLAP_INVALID_ID;
    _dragView = false;
    storeViewState();
  }

  void mouseMoved(drawing::Event *event) {
    const drawing::Point point =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    if (_atlasMenuOpen) {
      _atlasMenuHover = drawing::dropdownHitIndex(
          point, imprintAtlasMenuRect(), 18,
          static_cast<uint32_t>(kImprintAtlas.size()));
    } else if (_orderMenuOpen) {
      const auto orderBox =
          s3g::gui_layout::menuBoxRect(kImprintProcessPanel, 0u);
      _orderMenuHover = drawing::dropdownHitIndex(
          point,
          drawing::makeRect(orderBox.x, orderBox.y + orderBox.height + 3.0,
                            orderBox.width, 126.0),
          18, 7u);
    } else {
      return;
    }
    setNeedsDisplay(true);
  }

  void mouseExited(drawing::Event *event) {
    (void)event;
    if (_orderMenuHover != -1) {
      _orderMenuHover = -1;
      setNeedsDisplay(true);
    }
    if (_atlasMenuHover != -1) {
      _atlasMenuHover = -1;
      setNeedsDisplay(true);
    }
  }

  void scrollWheel(drawing::Event *event) {
    const drawing::Point point =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    if (!drawing::contains(
            point,
            drawing::cocoaRect(s3g::gui_layout::kImprintFamilyLayout.field))) {
      (void)0;
      return;
    }
    _viewZoom = std::clamp(
        _viewZoom * std::exp(-event->scrollingDeltaY() * 0.035), 0.55, 2.20);
    storeViewState();
    setNeedsDisplay(true);
  }
};
} // namespace complex_processor_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new complex_processor_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
