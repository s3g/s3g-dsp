// Literal Cocoa room fields, reflection paths, layout and pointer algorithms.
namespace input_encoder_canvas {
namespace drawing = s3g::portable_gui::input_encoder_drawing;
namespace foundation = s3g::portable_gui::foundation;
using namespace VSTGUI;
using namespace s3g::portable_gui::routing;
using Kind = s3g::clap_gui::ParamEventKind;
constexpr const char *pluginName = "Ambi Ray Encoder";
constexpr const char *pluginTitle = "s3g AMBI ENCODER RAY";
constexpr clap_id preservedOutput = kParamOutput;
constexpr const auto &stateExtension = stateExt;
double routingReadValue(const Plugin &p, clap_id id) { return getParam(p, id); }
void routingPublish(Plugin &p,
                    const std::vector<std::pair<clap_id, double>> &values) {
  for (auto [id, v] : values)
    applyParam(p, id, v);
}
void routingApply(Plugin &p, clap_id id, double v) { applyParam(p, id, v); }
double routingRawValue(Plugin &p, clap_id id) { return getParam(p, id); }
void routingInitialize(Plugin &p) {
  p.plugin.plugin_data = &p;
  init(&p.plugin);
}
#define S3G_ROUTING_CUSTOM_ACCESS 1
#include "s3g_routing_parameter_editor.inc"
#undef S3G_ROUTING_CUSTOM_ACCESS
drawing::String compactFileName(const std::string &name) {
  std::vector<size_t> starts;
  for (size_t i = 0; i < name.size(); ++i)
    if ((uint8_t(name[i]) & 0xc0) != 0x80)
      starts.push_back(i);
  return starts.size() <= 27 ? name
                             : "..." + name.substr(starts[starts.size() - 24]);
}
struct RayAtlasEntry {
  const char *title;
  const char *resource;
};

constexpr std::array<RayAtlasEntry, 19> kRayAtlas{{
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

struct GuiWorldBounds {
  float minimumX = 0.0f;
  float maximumX = 8.0f;
  float minimumY = 0.0f;
  float maximumY = 10.0f;
  float minimumZ = 0.0f;
  float maximumZ = 3.0f;
};

GuiWorldBounds guiWorldBounds(const GuiSnapshot &snapshot) {
  GuiWorldBounds bounds;
  bounds.minimumX = snapshot.room.navigationMinimumMetres.x;
  bounds.maximumX = snapshot.room.navigationMaximumMetres.x;
  bounds.minimumY = snapshot.room.navigationMinimumMetres.y;
  bounds.maximumY = snapshot.room.navigationMaximumMetres.y;
  bounds.minimumZ = std::min(0.0f, snapshot.room.navigationMinimumMetres.z);
  bounds.maximumZ = std::max(snapshot.room.heightMetres,
                             snapshot.room.navigationMaximumMetres.z);
  auto include = [&](float x, float y, float z) {
    bounds.minimumX = std::min(bounds.minimumX, x);
    bounds.maximumX = std::max(bounds.maximumX, x);
    bounds.minimumY = std::min(bounds.minimumY, y);
    bounds.maximumY = std::max(bounds.maximumY, y);
    bounds.minimumZ = std::min(bounds.minimumZ, z);
    bounds.maximumZ = std::max(bounds.maximumZ, z);
  };
  for (const auto &point : snapshot.room.polygon)
    include(point[0], point[1], 0.0f);
  for (const auto &point : snapshot.room.ceilingProfile)
    include(point[0], 0.0f, point[1]);
  for (const auto &branch : snapshot.branches) {
    for (const auto &point : branch.polygon) {
      include(point[0], point[1], branch.baseHeight);
      include(point[0], point[1], branch.baseHeight + branch.height);
    }
  }
  include(snapshot.listener.x, snapshot.listener.y, snapshot.listener.z);
  include(snapshot.source.x, snapshot.source.y, snapshot.source.z);
  for (const auto &cell : snapshot.cells)
    include(cell.x, cell.y, cell.z);
  for (const auto &reflection : snapshot.reflections) {
    if (reflection.hasBouncePosition)
      include(reflection.bouncePositionMetres.x,
              reflection.bouncePositionMetres.y,
              reflection.bouncePositionMetres.z);
  }
  if (bounds.maximumX < bounds.minimumX + 0.1f)
    bounds.maximumX = bounds.minimumX + 0.1f;
  if (bounds.maximumY < bounds.minimumY + 0.1f)
    bounds.maximumY = bounds.minimumY + 0.1f;
  if (bounds.maximumZ < bounds.minimumZ + 0.1f)
    bounds.maximumZ = bounds.minimumZ + 0.1f;
  const float padX = (bounds.maximumX - bounds.minimumX) * 0.05f;
  const float padY = (bounds.maximumY - bounds.minimumY) * 0.05f;
  const float padZ = (bounds.maximumZ - bounds.minimumZ) * 0.07f;
  bounds.minimumX -= padX;
  bounds.maximumX += padX;
  bounds.minimumY -= padY;
  bounds.maximumY += padY;
  bounds.minimumZ -= padZ;
  bounds.maximumZ += padZ;
  return bounds;
}

drawing::Rect topFieldRect() { return drawing::makeRect(24, 68, 330, 452); }
drawing::Rect sideFieldRect() { return drawing::makeRect(366, 68, 330, 452); }
drawing::Rect fieldPlotRect(drawing::Rect field) {
  return drawing::makeRect(field.origin.x + 15, field.origin.y + 29,
                           field.size.width - 30, field.size.height - 44);
}
drawing::Rect orderMenuRect() { return drawing::makeRect(830, 113, 82, 126); }
drawing::Rect rayAtlasMenuRect() {
  return drawing::makeRect(724, 148, 304,
                           18.0 * static_cast<double>(kRayAtlas.size()));
}
drawing::Rect sourceModeRect() { return drawing::makeRect(934, 244, 38, 17); }
drawing::Rect listenerModeRect() { return drawing::makeRect(976, 244, 40, 17); }
drawing::Rect fieldListenButtonRect(uint32_t mode) {
  return drawing::makeRect(872 + static_cast<double>(mode) * 37.0, 406, 34, 17);
}

drawing::Point projectFieldPosition(const GuiSnapshot &snapshot,
                                    s3g::Vec3 position, int view) {
  const GuiWorldBounds bounds = guiWorldBounds(snapshot);
  const drawing::Rect plot =
      fieldPlotRect(view == 1 ? topFieldRect() : sideFieldRect());
  const float nx = (position.x - bounds.minimumX) /
                   std::max(0.001f, bounds.maximumX - bounds.minimumX);
  const float vertical =
      view == 2 ? (position.z - bounds.minimumZ) /
                      std::max(0.001f, bounds.maximumZ - bounds.minimumZ)
                : (position.y - bounds.minimumY) /
                      std::max(0.001f, bounds.maximumY - bounds.minimumY);
  return drawing::makePoint(plot.origin.x + nx * plot.size.width,
                            plot.origin.y +
                                (1.0f - vertical) * plot.size.height);
}

class Editor final : public ParameterEditor {
public:
  explicit Editor(Plugin &instance) : ParameterEditor(instance) {
    auto *plugin = &p;
    _plugin = plugin;
    _dragParam = CLAP_INVALID_ID;
    _positionDragView = 0;
    _editListener = false;
    _atlasMenuOpen = false;
    _atlasMenuHover = -1;
    _orderMenuOpen = false;
    _orderMenuHover = -1;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CURRENT");
    service();
  }
  ~Editor() override { stopRefresh(); }
  void stopRefresh() override {
    ParameterEditor::stopRefresh();
    pointerEditing = false;
    _dragParam = CLAP_INVALID_ID;
    _positionDragView = 0;
    _atlasMenuOpen = false;
    _orderMenuOpen = false;
  }
  double refreshPeriodMilliseconds() const override { return 1000. / 30.; }
  void service() override { guiParams = snapshotEncoderParams(p); }
  void setParam_value(clap_id id, double v) {
    if (pointerEditing) {
      if (begin(id))
        change(id, v);
    } else
      set(id, v);
    service();
    invalid();
  }
  void guiApplyParam(Plugin &, clap_id id, double v) { setParam_value(id, v); }
  drawing::Rect bounds() const {
    return drawing::makeRect(0, 0, kGuiWidth, kGuiHeight);
  }
  drawing::Point convertPoint_fromView(drawing::Point pt,
                                       std::nullptr_t) const {
    return pt;
  }
  void setNeedsDisplay(bool) { invalid(); }
  void displayIfNeeded() { invalid(); }
  bool pointerDown(MouseDownEvent &e) override {
    if (!e.buttonState.isLeft())
      return false;
    service();
    pointerEditing = true;
    drawing::Event event{{e.mousePosition.x, e.mousePosition.y},
                         int(e.clickCount)};
    mouseDown(&event);
    return true;
  }
  void pointerMove(MouseMoveEvent &e) override {
    drawing::Event event{{e.mousePosition.x, e.mousePosition.y}};
    if (e.buttonState.isLeft())
      mouseDragged(&event);
    else
      mouseMoved(&event);
    invalid();
    e.consumed = true;
  }
  void pointerUp(MouseUpEvent &e) override {
    drawing::Event event{{e.mousePosition.x, e.mousePosition.y}};
    mouseUp(&event);
    end();
    pointerEditing = false;
    invalid();
  }
  bool pointerEditing = false;
  EncoderParams guiParams{};
  std::mt19937 randomGenerator{std::random_device{}()};

  Plugin *_plugin;
  clap_id _dragParam;
  int _positionDragView;
  bool _editListener;
  bool _atlasMenuOpen;
  int _atlasMenuHover;
  bool _orderMenuOpen;
  int _orderMenuHover;
  char _titlePresetName[64];
  void setStatus(const std::string &status) {
    std::lock_guard<std::mutex> lock(_plugin->stateMutex);
    _plugin->status = status;
  }
  void drawSlider_value_norm_y_attrs_style(drawing::String name,
                                           drawing::String value, double norm,
                                           double y, drawing::Attrs attrs,
                                           drawing::Style &style) {
    drawing::drawSlider(name, value, norm, y, drawing::softLabelAttrs(), attrs,
                        style, 742, 830, 990, 145);
  }
  void drawField_rect_side_attrs(const GuiSnapshot &snapshot,
                                 drawing::Rect field, bool side,
                                 drawing::Attrs attrs) {
    drawing::setFill(drawing::calibratedColor(0x0d1011));
    drawing::rectFill(field);
    drawing::setStroke(drawing::calibratedColor(0x4b5052));
    drawing::frameRect(field);
    drawing::String label =
        side ? "SIDE  PATHS / ARRIVALS  X / Z" : "TOP  PATHS / ARRIVALS  X / Y";
    drawing::drawAtPoint_withAttributes(
        label, drawing::makePoint(field.origin.x + 9, field.origin.y + 8),
        attrs);

    const GuiWorldBounds bounds = guiWorldBounds(snapshot);
    const drawing::Rect plot = fieldPlotRect(field);
    auto project = [&](float x, float y, float z) {
      const float nx = (x - bounds.minimumX) /
                       std::max(0.001f, bounds.maximumX - bounds.minimumX);
      const float vertical =
          side ? (z - bounds.minimumZ) /
                     std::max(0.001f, bounds.maximumZ - bounds.minimumZ)
               : (y - bounds.minimumY) /
                     std::max(0.001f, bounds.maximumY - bounds.minimumY);
      return drawing::makePoint(plot.origin.x + nx * plot.size.width,
                                plot.origin.y +
                                    (1.0f - vertical) * plot.size.height);
    };
    auto drawInwardArrow = [&](drawing::Point from, drawing::Point to,
                               VSTGUI::CColor color) {
      const double dx = to.x - from.x;
      const double dy = to.y - from.y;
      const double length = std::sqrt(dx * dx + dy * dy);
      if (length < 12.0)
        return;
      const double ux = dx / length;
      const double uy = dy / length;
      const drawing::Point tip =
          drawing::makePoint(from.x + dx * 0.78, from.y + dy * 0.78);
      const drawing::Point base =
          drawing::makePoint(tip.x - ux * 6.0, tip.y - uy * 6.0);
      drawing::Path arrow = drawing::Path::bezierPath();
      (arrow).moveToPoint(
          drawing::makePoint(base.x - uy * 3.0, base.y + ux * 3.0));
      (arrow).lineToPoint(tip);
      (arrow).lineToPoint(
          drawing::makePoint(base.x + uy * 3.0, base.y - ux * 3.0));
      (arrow).setLineWidth(1.0);
      drawing::setStroke(color);
      (arrow).stroke();
    };

    drawing::GraphicsScope plotScope0(*context);
    drawing::clipRect(drawing::inset(plot, -1.0, -1.0));
    drawing::setStroke(drawing::calibratedColor(0x24292b));
    for (int index = 1; index < 5; ++index) {
      const double x =
          plot.origin.x + plot.size.width * static_cast<double>(index) / 5.0;
      const double y =
          plot.origin.y + plot.size.height * static_cast<double>(index) / 5.0;
      drawing::Path::strokeLineFromPoint_toPoint(
          drawing::makePoint(x, plot.origin.y),
          drawing::makePoint(x, drawing::maxY(plot)));
      drawing::Path::strokeLineFromPoint_toPoint(
          drawing::makePoint(plot.origin.x, y),
          drawing::makePoint(drawing::maxX(plot), y));
    }

    if (!side) {
      auto drawPolygon = [&](const std::vector<std::array<float, 2>> &polygon,
                             VSTGUI::CColor fill, VSTGUI::CColor stroke) {
        if (polygon.size() < 3u)
          return;
        drawing::Path path = drawing::Path::bezierPath();
        (path).moveToPoint(project(polygon[0][0], polygon[0][1], 0.0f));
        for (size_t index = 1u; index < polygon.size(); ++index)
          (path).lineToPoint(
              project(polygon[index][0], polygon[index][1], 0.0f));
        (path).closePath();
        drawing::setFill(fill);
        (path).fill();
        drawing::setStroke(stroke);
        (path).setLineWidth(1.1);
        (path).stroke();
      };
      drawPolygon(snapshot.room.polygon,
                  drawing::calibratedColor(0x788084, 0.08),
                  drawing::calibratedColor(0x899194, 0.76));
      for (const auto &branch : snapshot.branches)
        drawPolygon(branch.polygon, drawing::calibratedColor(0x668b78, 0.08),
                    drawing::calibratedColor(0x78ad91, 0.62));
    } else {
      drawing::Path roomPath = drawing::Path::bezierPath();
      (roomPath).moveToPoint(project(bounds.minimumX, 0.0f, 0.0f));
      (roomPath).lineToPoint(project(bounds.maximumX, 0.0f, 0.0f));
      if (snapshot.room.ceilingProfile.size() >= 2u) {
        for (auto iterator = snapshot.room.ceilingProfile.rbegin();
             iterator != snapshot.room.ceilingProfile.rend(); ++iterator)
          (roomPath).lineToPoint(project((*iterator)[0], 0.0f, (*iterator)[1]));
      } else {
        (roomPath).lineToPoint(
            project(bounds.maximumX, 0.0f, snapshot.room.heightMetres));
        (roomPath).lineToPoint(
            project(bounds.minimumX, 0.0f, snapshot.room.heightMetres));
      }
      (roomPath).closePath();
      drawing::setFill(drawing::calibratedColor(0x788084, 0.08));
      (roomPath).fill();
      drawing::setStroke(drawing::calibratedColor(0x899194, 0.76));
      (roomPath).stroke();
      for (const auto &branch : snapshot.branches) {
        if (branch.polygon.empty())
          continue;
        float minimumX = branch.polygon.front()[0];
        float maximumX = minimumX;
        for (const auto &point : branch.polygon) {
          minimumX = std::min(minimumX, point[0]);
          maximumX = std::max(maximumX, point[0]);
        }
        drawing::Rect branchRect = drawing::makeRect(
            project(minimumX, 0.0f, branch.baseHeight + branch.height).x,
            project(0.0f, 0.0f, branch.baseHeight + branch.height).y,
            project(maximumX, 0.0f, 0.0f).x - project(minimumX, 0.0f, 0.0f).x,
            project(0.0f, 0.0f, branch.baseHeight).y -
                project(0.0f, 0.0f, branch.baseHeight + branch.height).y);
        drawing::Path branchPath =
            drawing::Path::bezierPathWithRect(branchRect);
        drawing::setFill(drawing::calibratedColor(0x668b78, 0.07));
        (branchPath).fill();
        drawing::setStroke(drawing::calibratedColor(0x78ad91, 0.54));
        (branchPath).setLineWidth(1.0);
        (branchPath).stroke();
      }
    }

    const auto &navMin = snapshot.room.navigationMinimumMetres;
    const auto &navMax = snapshot.room.navigationMaximumMetres;
    drawing::Path navigation = drawing::Path::bezierPath();
    if (side) {
      (navigation)
          .appendBezierPathWithRect(drawing::makeRect(
              project(navMin.x, 0.0f, navMax.z).x,
              project(0.0f, 0.0f, navMax.z).y,
              project(navMax.x, 0.0f, 0.0f).x - project(navMin.x, 0.0f, 0.0f).x,
              project(0.0f, 0.0f, navMin.z).y -
                  project(0.0f, 0.0f, navMax.z).y));
    } else {
      (navigation)
          .appendBezierPathWithRect(drawing::makeRect(
              project(navMin.x, navMax.y, 0.0f).x,
              project(0.0f, navMax.y, 0.0f).y,
              project(navMax.x, 0.0f, 0.0f).x - project(navMin.x, 0.0f, 0.0f).x,
              project(0.0f, navMin.y, 0.0f).y -
                  project(0.0f, navMax.y, 0.0f).y));
    }
    const double dash[] = {4.0, 3.0};
    (navigation).setLineDash_count_phase(dash, 2, 0.0);
    drawing::setStroke(drawing::calibratedColor(0x7199a1, 0.48));
    (navigation).stroke();

    for (uint32_t index = 0u; index < snapshot.cells.size(); ++index) {
      const auto &cell = snapshot.cells[index];
      const drawing::Point point = project(cell.x, cell.y, cell.z);
      drawing::setFill(drawing::calibratedColor(
          index == snapshot.nearestCell ? 0xa7d3d9 : 0x5f7377,
          index == snapshot.nearestCell ? 0.94 : 0.54));
      const double radius = index == snapshot.nearestCell ? 2.6 : 1.6;
      drawing::rectFill(drawing::makeRect(point.x - radius, point.y - radius,
                                          radius * 2.0, radius * 2.0));
    }

    const drawing::Point listener =
        project(snapshot.listener.x, snapshot.listener.y, snapshot.listener.z);
    const drawing::Point source =
        project(snapshot.source.x, snapshot.source.y, snapshot.source.z);
    const float worldSpan = side ? std::max(bounds.maximumX - bounds.minimumX,
                                            bounds.maximumZ - bounds.minimumZ)
                                 : std::max(bounds.maximumX - bounds.minimumX,
                                            bounds.maximumY - bounds.minimumY);
    for (const auto &reflection : snapshot.reflections) {
      const float alpha =
          s3g::clamp(std::abs(reflection.gain) * 3.2f, 0.14f, 0.58f);
      if (reflection.hasBouncePosition) {
        const drawing::Point bounce =
            project(reflection.bouncePositionMetres.x,
                    reflection.bouncePositionMetres.y,
                    reflection.bouncePositionMetres.z);
        VSTGUI::CColor pathColor = drawing::calibratedColor(0x87c1ac, alpha);
        drawing::Path path = drawing::Path::bezierPath();
        (path).moveToPoint(source);
        (path).lineToPoint(bounce);
        (path).lineToPoint(listener);
        (path).setLineWidth(1.0);
        drawing::setStroke(pathColor);
        (path).stroke();
        drawing::setFill(
            drawing::calibratedColor(0xd3aa65, std::max(0.42f, alpha)));
        (drawing::Path::bezierPathWithOvalInRect(
             drawing::makeRect(bounce.x - 2.2, bounce.y - 2.2, 4.4, 4.4)))
            .fill();
        drawInwardArrow(bounce, listener, pathColor);
      } else {
        const float length =
            worldSpan * (0.14f + 0.32f * s3g::clamp(reflection.delayMs / 220.0f,
                                                    0.0f, 1.0f));
        const s3g::Vec3 worldDirection =
            s3g::ambi_ray_detail::worldDirectionFromAed(
                reflection.azimuthDeg, reflection.elevationDeg);
        const float dx = worldDirection.x * length;
        const float dy = worldDirection.y * length;
        const float dz = worldDirection.z * length;
        const drawing::Point end =
            project(snapshot.listener.x + dx, snapshot.listener.y + dy,
                    snapshot.listener.z + dz);
        VSTGUI::CColor arrivalColor = drawing::calibratedColor(0x7fb9c2, alpha);
        drawing::Path arrival = drawing::Path::bezierPath();
        (arrival).moveToPoint(end);
        (arrival).lineToPoint(listener);
        const double arrivalDash[] = {3.0, 3.0};
        (arrival).setLineDash_count_phase(arrivalDash, 2, 0.0);
        (arrival).setLineWidth(1.0);
        drawing::setStroke(arrivalColor);
        (arrival).stroke();
        drawInwardArrow(end, listener, arrivalColor);
      }
    }
    VSTGUI::CColor directColor = drawing::calibratedColor(0xb8b8b8, 0.62);
    drawing::setStroke(directColor);
    drawing::Path::strokeLineFromPoint_toPoint(source, listener);
    drawInwardArrow(source, listener, directColor);
    const bool markersOverlap =
        std::hypot(source.x - listener.x, source.y - listener.y) < 16.0;
    if (markersOverlap) {
      drawing::Path listenerMarker = drawing::Path::bezierPathWithOvalInRect(
          drawing::makeRect(listener.x - 8.0, listener.y - 8.0, 16.0, 16.0));
      (listenerMarker).setLineWidth(1.5);
      drawing::setStroke(drawing::calibratedColor(0xb7b7b7));
      (listenerMarker).stroke();
      drawing::setFill(drawing::calibratedColor(0x90d3dc));
      drawing::rectFill(
          drawing::makeRect(source.x - 4.0, source.y - 4.0, 8.0, 8.0));
    } else {
      drawing::setFill(drawing::calibratedColor(0xb7b7b7));
      drawing::rectFill(
          drawing::makeRect(listener.x - 4.0, listener.y - 4.0, 8.0, 8.0));
      drawing::setFill(drawing::calibratedColor(0x90d3dc));
      drawing::rectFill(
          drawing::makeRect(source.x - 6.0, source.y - 6.0, 12.0, 12.0));
      drawing::setStroke(drawing::calibratedColor(0x101213));
      drawing::frameRect(
          drawing::makeRect(source.x - 6.0, source.y - 6.0, 12.0, 12.0));
    }
    const drawing::Point selectedPosition = _editListener ? listener : source;
    const double selectedRadius =
        markersOverlap ? 12.0 : _editListener ? 8.0 : 10.0;
    drawing::Path selection = drawing::Path::bezierPathWithOvalInRect(
        drawing::makeRect(selectedPosition.x - selectedRadius,
                          selectedPosition.y - selectedRadius,
                          selectedRadius * 2.0, selectedRadius * 2.0));
    (selection).setLineWidth(1.4);
    drawing::setStroke(drawing::calibratedColor(0xd8a24a, 0.92));
    (selection).stroke();
    if (markersOverlap) {
      drawing::drawAtPoint_withAttributes(
          "L/S", drawing::makePoint(source.x + 14.0, source.y - 8.0), attrs);
    } else {
      drawing::drawAtPoint_withAttributes(
          "L", drawing::makePoint(listener.x + 7.0, listener.y - 8.0), attrs);
      drawing::drawAtPoint_withAttributes(
          "S", drawing::makePoint(source.x + 9.0, source.y - 8.0), attrs);
    }
    plotScope0.restore();
  }
  void paint() override {
    drawing::DrawingScope scope(*this, context, font, titleFont, &error);
    service();

    drawing::Style style;
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs text = drawing::softLabelAttrs();
    drawing::Attrs value = drawing::softValueAttrs();
    const GuiSnapshot snapshot = guiSnapshot(*_plugin);
    const auto params = guiParams;
    const float peak = _plugin->outputPeak.exchange(
        _plugin->outputPeak.load(std::memory_order_relaxed) * 0.92f,
        std::memory_order_relaxed);
    drawing::String status = drawing::stringFormat(
        "MONO > %uOA  ·  %@", params.order, drawing::peakDbText(peak));
    drawing::drawEncoderTitleBand(
        "s3g AMBI ENCODER RAY", drawing::stringFromUtf8(_titlePresetName),
        status, drawing::encoderTitleBand(kGuiWidth, kGuiHeight),
        drawing::softTitleAttrs(), text, value, style);

    std::optional<VSTGUI::CDrawContext::Transform> contentTransform;
    contentTransform.emplace(*context, VSTGUI::CGraphicsTransform{}.translate(
                                           0., kContentTranslation));

    const drawing::Rect fieldPanel = drawing::makeRect(12, 34, 700, 574);
    const drawing::Rect outputPanel = drawing::makeRect(724, 34, 304, 80);
    const drawing::Rect mapPanel = drawing::makeRect(724, 126, 304, 104);
    const drawing::Rect positionPanel = drawing::makeRect(724, 242, 304, 150);
    const drawing::Rect roomPanel = drawing::makeRect(724, 404, 304, 186);
    drawing::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y,
                            fieldPanel.size.width, fieldPanel.size.height,
                            style);
    drawing::drawPanelHeader("RAY FIELD", true, fieldPanel.origin.x,
                             fieldPanel.origin.y, fieldPanel.size.width, 21,
                             text, style);
    drawField_rect_side_attrs(snapshot, topFieldRect(), false, value);
    drawField_rect_side_attrs(snapshot, sideFieldRect(), true, value);
    drawing::drawAtPoint_withAttributes("SRC", drawing::makePoint(28, 539),
                                        text);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("%.2f, %.2f, %.2f m", snapshot.source.x,
                              snapshot.source.y, snapshot.source.z),
        drawing::makePoint(76, 539), value);
    drawing::drawAtPoint_withAttributes("LIS", drawing::makePoint(366, 539),
                                        text);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("%.2f, %.2f, %.2f m", snapshot.listener.x,
                              snapshot.listener.y, snapshot.listener.z),
        drawing::makePoint(414, 539), value);
    drawing::drawAtPoint_withAttributes("CELL", drawing::makePoint(28, 564),
                                        text);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("%u / %zu   %.2f m", snapshot.nearestCell + 1u,
                              snapshot.cells.size(), snapshot.nearestDistance),
        drawing::makePoint(76, 564), value);
    drawing::drawAtPoint_withAttributes("FIELD", drawing::makePoint(366, 564),
                                        text);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat(
            "%zu rays / %.1f x %.1f x %.1f m", snapshot.reflections.size(),
            snapshot.room.widthMetres, snapshot.room.depthMetres,
            snapshot.room.heightMetres),
        drawing::makePoint(414, 564), value);
    drawing::drawAtPoint_withAttributes("ROOM", drawing::makePoint(28, 589),
                                        text);
    drawing::String family = drawing::uppercaseString(
        drawing::stringFromUtf8(snapshot.family.c_str()));
    drawing::drawAtPoint_withAttributes((family.empty() ? "ROOM" : family),
                                        drawing::makePoint(76, 589), value);

    drawing::drawPanelFrame(outputPanel.origin.x, outputPanel.origin.y,
                            outputPanel.size.width, outputPanel.size.height,
                            style);
    drawing::drawPanelHeader("OUTPUT", true, outputPanel.origin.x,
                             outputPanel.origin.y, outputPanel.size.width, 21,
                             text, style);
    drawSlider_value_norm_y_attrs_style(
        "OUT", drawing::stringFormat("%+.1f dB", params.outputGainDb),
        (params.outputGainDb + 60.0f) / 72.0f, 70, value, style);
    drawing::drawMenu("ORDER", drawing::stringFormat("%uOA", params.order), 96,
                      text, value, style, 742, 830, 82);
    drawing::drawToggle("BYP", params.bypassRoom, 96, text, value, style, 930,
                        974, 42);

    drawing::drawPanelFrame(mapPanel.origin.x, mapPanel.origin.y,
                            mapPanel.size.width, mapPanel.size.height, style);
    drawing::drawPanelHeader("MAP", true, mapPanel.origin.x, mapPanel.origin.y,
                             mapPanel.size.width, 21, text, style);
    drawing::drawToolboxHeaderActionButton(drawing::makeRect(879, 128, 64, 17),
                                           drawing::makeRect(724, 126, 304, 21),
                                           "ATLAS", value, style);
    drawing::drawToolboxHeaderActionButton(drawing::makeRect(951, 128, 64, 17),
                                           drawing::makeRect(724, 126, 304, 21),
                                           "LOAD", value, style);
    drawing::drawAtPoint_withAttributes("FILE", drawing::makePoint(742, 160),
                                        text);
    drawing::drawAtPoint_withAttributes(compactFileName(snapshot.name),
                                        drawing::makePoint(790, 160), value);
    drawing::drawAtPoint_withAttributes("STAT", drawing::makePoint(742, 183),
                                        text);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFromUtf8(snapshot.status.c_str()),
        drawing::makePoint(790, 183), value);
    drawing::drawAtPoint_withAttributes("CELLS", drawing::makePoint(742, 206),
                                        text);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("%zu", snapshot.cells.size()),
        drawing::makePoint(790, 206), value);

    drawing::drawPanelFrame(positionPanel.origin.x, positionPanel.origin.y,
                            positionPanel.size.width, positionPanel.size.height,
                            style);
    drawing::drawPanelHeader("POSITION", true, positionPanel.origin.x,
                             positionPanel.origin.y, positionPanel.size.width,
                             21, text, style);
    const drawing::Rect positionHeader =
        drawing::makeRect(positionPanel.origin.x, positionPanel.origin.y,
                          positionPanel.size.width, 21);
    drawing::drawToolboxHeaderButton(sourceModeRect(), positionHeader, "SRC",
                                     !_editListener, value, style);
    drawing::drawToolboxHeaderButton(listenerModeRect(), positionHeader, "LIS",
                                     _editListener, value, style);
    const s3g::Vec3 editPosition =
        _editListener ? snapshot.listener : snapshot.source;
    const float editX = _editListener ? params.listenerX : params.sourceX;
    const float editY = _editListener ? params.listenerY : params.sourceY;
    const float editZ = _editListener ? params.listenerZ : params.sourceZ;
    drawSlider_value_norm_y_attrs_style(
        "X", drawing::stringFormat("%.2f m", editPosition.x), editX, 278, value,
        style);
    drawSlider_value_norm_y_attrs_style(
        "Y", drawing::stringFormat("%.2f m", editPosition.y), editY, 302, value,
        style);
    drawSlider_value_norm_y_attrs_style(
        "Z", drawing::stringFormat("%.2f m", editPosition.z), editZ, 326, value,
        style);
    drawSlider_value_norm_y_attrs_style(
        "MOVE", drawing::stringFormat("%.0f ms", params.movementMs),
        (params.movementMs - 10.0f) / 490.0f, 350, value, style);
    drawSlider_value_norm_y_attrs_style(
        "DOP", drawing::stringFormat("%.0f%%", params.doppler * 100.0f),
        params.doppler / 2.0f, 374, value, style);

    drawing::drawPanelFrame(roomPanel.origin.x, roomPanel.origin.y,
                            roomPanel.size.width, roomPanel.size.height, style);
    drawing::drawPanelHeader("ROOM FIELD", true, roomPanel.origin.x,
                             roomPanel.origin.y, roomPanel.size.width, 21, text,
                             style);
    drawing::drawAtPoint_withAttributes("LST", drawing::makePoint(842, 407),
                                        text);
    static drawing::String listenLabels[] = {"OFF", "FOL", "CTR", "BAL"};
    const uint32_t listenMode = static_cast<uint32_t>(params.fieldListenMode);
    for (uint32_t mode = 0u; mode < 4u; ++mode) {
      drawing::drawHeaderButton(fieldListenButtonRect(mode), roomPanel,
                                listenLabels[mode], mode == listenMode, value,
                                style);
    }
    drawSlider_value_norm_y_attrs_style(
        "DIR", drawing::stringFormat("%.0f%%", params.direct * 100.0f),
        params.direct / 1.5f, 440, value, style);
    drawSlider_value_norm_y_attrs_style(
        "EAR", drawing::stringFormat("%.0f%%", params.early * 100.0f),
        params.early / 1.5f, 462, value, style);
    drawSlider_value_norm_y_attrs_style(
        "LAT", drawing::stringFormat("%.0f%%", params.late * 100.0f),
        params.late / 1.5f, 484, value, style);
    drawSlider_value_norm_y_attrs_style(
        "SIZE", drawing::stringFormat("%.2f", params.size),
        (params.size - 0.5f) / 1.5f, 506, value, style);
    drawSlider_value_norm_y_attrs_style(
        "SCAT", drawing::stringFormat("%.0f%%", params.scatter * 100.0f),
        params.scatter, 528, value, style);
    drawSlider_value_norm_y_attrs_style(
        "WID", drawing::stringFormat("%.2f", params.width), params.width / 1.5f,
        550, value, style);
    drawSlider_value_norm_y_attrs_style(
        "AIR", drawing::stringFormat("%.0f%%", params.air * 100.0f), params.air,
        572, value, style);

    if (_orderMenuOpen) {
      drawing::String items[] = {"1OA", "2OA", "3OA", "4OA",
                                 "5OA", "6OA", "7OA"};
      drawing::drawDropdownMenu(orderMenuRect(), 18, items, 7u,
                                static_cast<int>(params.order) - 1,
                                _orderMenuHover, value, style);
    }
    if (_atlasMenuOpen) {
      drawing::String atlasItems[kRayAtlas.size()];
      for (size_t index = 0u; index < kRayAtlas.size(); ++index)
        atlasItems[index] = drawing::stringFromUtf8(kRayAtlas[index].title);
      drawing::drawDropdownMenu(rayAtlasMenuRect(), 18, atlasItems,
                                static_cast<uint32_t>(kRayAtlas.size()), -1,
                                _atlasMenuHover, value, style);
    }
    contentTransform.reset();
  }
  void resetParam(clap_id param) {
    const s3g::Vec3 sourceDefault =
        s3g::AmbiRayEncoder::normalizedSourcePosition(
            _plugin->descriptor,
            _plugin->descriptor.defaultSourcePositionMetres);
    const s3g::Vec3 listenerDefault =
        s3g::AmbiRayEncoder::normalizedListenerPosition(
            _plugin->descriptor, _plugin->descriptor.listenerPositionMetres);
    switch (param) {
    case kParamSourceX:
      setParam_value(param, sourceDefault.x);
      break;
    case kParamSourceY:
      setParam_value(param, sourceDefault.y);
      break;
    case kParamSourceZ:
      setParam_value(param, sourceDefault.z);
      break;
    case kParamListenerX:
      setParam_value(param, listenerDefault.x);
      break;
    case kParamListenerY:
      setParam_value(param, listenerDefault.y);
      break;
    case kParamListenerZ:
      setParam_value(param, listenerDefault.z);
      break;
    case kParamMovement:
      setParam_value(param, 60.0);
      break;
    case kParamDoppler:
      setParam_value(param, 0.50);
      break;
    case kParamDirect:
      setParam_value(param, 1.0);
      break;
    case kParamEarly:
      setParam_value(param, 0.72);
      break;
    case kParamLate:
      setParam_value(param, 0.42);
      break;
    case kParamSize:
      setParam_value(param, 1.0);
      break;
    case kParamScatter:
      setParam_value(param, 0.45);
      break;
    case kParamWidth:
      setParam_value(param, 1.0);
      break;
    case kParamAir:
      setParam_value(param, 0.20);
      break;
    case kParamOutput:
      setParam_value(param, -6.0);
      break;
    default:
      break;
    }
  }
  void updateSliderAtPoint(drawing::Point point) {
    const double norm = std::clamp((point.x - 830.0) / 145.0, 0.0, 1.0);
    switch (_dragParam) {
    case kParamSourceX:
      setParam_value(_dragParam, norm);
      break;
    case kParamSourceY:
      setParam_value(_dragParam, norm);
      break;
    case kParamSourceZ:
      setParam_value(_dragParam, norm);
      break;
    case kParamListenerX:
      setParam_value(_dragParam, norm);
      break;
    case kParamListenerY:
      setParam_value(_dragParam, norm);
      break;
    case kParamListenerZ:
      setParam_value(_dragParam, norm);
      break;
    case kParamMovement:
      setParam_value(_dragParam, 10.0 + norm * 490.0);
      break;
    case kParamDoppler:
      setParam_value(_dragParam, norm * 2.0);
      break;
    case kParamDirect:
      setParam_value(_dragParam, norm * 1.5);
      break;
    case kParamEarly:
      setParam_value(_dragParam, norm * 1.5);
      break;
    case kParamLate:
      setParam_value(_dragParam, norm * 1.5);
      break;
    case kParamSize:
      setParam_value(_dragParam, 0.5 + norm * 1.5);
      break;
    case kParamScatter:
      setParam_value(_dragParam, norm);
      break;
    case kParamWidth:
      setParam_value(_dragParam, norm * 1.5);
      break;
    case kParamAir:
      setParam_value(_dragParam, norm);
      break;
    case kParamOutput:
      setParam_value(_dragParam, -60.0 + norm * 72.0);
      break;
    default:
      break;
    }
  }
  void updatePositionAtPoint_view(drawing::Point point, int view) {
    const GuiSnapshot snapshot = guiSnapshot(*_plugin);
    const GuiWorldBounds bounds = guiWorldBounds(snapshot);
    const drawing::Rect plot =
        fieldPlotRect(view == 1 ? topFieldRect() : sideFieldRect());
    const float xAmount = s3g::clamp(
        static_cast<float>((point.x - plot.origin.x) / plot.size.width), 0.0f,
        1.0f);
    const float verticalAmount = s3g::clamp(
        static_cast<float>(1.0 - (point.y - plot.origin.y) / plot.size.height),
        0.0f, 1.0f);
    const float worldX = s3g::lerp(bounds.minimumX, bounds.maximumX, xAmount);
    const auto &minimum = snapshot.room.navigationMinimumMetres;
    const auto &maximum = snapshot.room.navigationMaximumMetres;
    const clap_id xParam = _editListener ? kParamListenerX : kParamSourceX;
    const clap_id yParam = _editListener ? kParamListenerY : kParamSourceY;
    const clap_id zParam = _editListener ? kParamListenerZ : kParamSourceZ;
    setParam_value(xParam, (worldX - minimum.x) /
                               std::max(0.0001f, maximum.x - minimum.x));
    if (view == 1) {
      const float worldY =
          s3g::lerp(bounds.minimumY, bounds.maximumY, verticalAmount);
      setParam_value(yParam, (worldY - minimum.y) /
                                 std::max(0.0001f, maximum.y - minimum.y));
    } else {
      const float worldZ =
          s3g::lerp(bounds.minimumZ, bounds.maximumZ, verticalAmount);
      setParam_value(zParam, (worldZ - minimum.z) /
                                 std::max(0.0001f, maximum.z - minimum.z));
    }
  }
  void mouseDown(drawing::Event *event) {
    drawing::Point point =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    const auto titleBand = drawing::encoderTitleBand(kGuiWidth, kGuiHeight);
    if (handleTitleClick(point, titleBand))
      return;
    if (drawing::contains(point, drawing::cocoaRect(titleBand.randomButton))) {
      auto randomUnit = [this] {
        return static_cast<double>(randomGenerator()) / 4294967295.0;
      };
      setParam_value(kParamSourceX, 0.12 + randomUnit() * 0.76);
      setParam_value(kParamSourceY, 0.12 + randomUnit() * 0.76);
      setParam_value(kParamSourceZ, 0.12 + randomUnit() * 0.76);
      setParam_value(kParamDirect, 0.55 + randomUnit() * 0.75);
      setParam_value(kParamEarly, 0.25 + randomUnit() * 0.90);
      setParam_value(kParamLate, 0.18 + randomUnit() * 0.82);
      setParam_value(kParamSize, 0.60 + randomUnit() * 1.25);
      setParam_value(kParamScatter, 0.12 + randomUnit() * 0.76);
      setParam_value(kParamWidth, 0.35 + randomUnit() * 1.10);
      setParam_value(kParamAir, randomUnit() * 0.62);
      std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "RANDOM");
      setNeedsDisplay(true);
      return;
    }
    point.y -= kContentTranslation;
    if (_atlasMenuOpen) {
      const int hit =
          drawing::dropdownHitIndex(point, rayAtlasMenuRect(), 18,
                                    static_cast<uint32_t>(kRayAtlas.size()));
      _atlasMenuOpen = false;
      _atlasMenuHover = -1;
      if (hit >= 0)
        loadAtlasAtIndex(static_cast<unsigned>(hit));
      setNeedsDisplay(true);
      return;
    }
    if (_orderMenuOpen) {
      const int hit = drawing::dropdownHitIndex(point, orderMenuRect(), 18, 7u);
      _orderMenuOpen = false;
      _orderMenuHover = -1;
      if (hit >= 0)
        setParam_value(kParamOrder, hit + 1);
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, drawing::makeRect(879, 128, 64, 17))) {
      _atlasMenuOpen = true;
      _atlasMenuHover = -1;
      _orderMenuOpen = false;
      _orderMenuHover = -1;
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, drawing::makeRect(951, 128, 64, 17))) {
      loadRayField();
      return;
    }
    if (drawing::contains(point, drawing::makeRect(830, 95, 82, 18))) {
      _orderMenuOpen = true;
      _atlasMenuOpen = false;
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, drawing::makeRect(974, 95, 42, 18))) {
      setParam_value(kParamBypass, guiParams.bypassRoom ? 0.0 : 1.0);
      return;
    }
    for (uint32_t mode = 0u; mode < 4u; ++mode) {
      if (drawing::contains(point, fieldListenButtonRect(mode))) {
        setParam_value(kParamFieldListen, mode);
        return;
      }
    }
    if (drawing::contains(point, sourceModeRect())) {
      _editListener = false;
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, listenerModeRect())) {
      _editListener = true;
      setNeedsDisplay(true);
      return;
    }
    auto beginPositionDrag = [&](int view) {
      const GuiSnapshot snapshot = guiSnapshot(*_plugin);
      const drawing::Point sourcePoint =
          projectFieldPosition(snapshot, snapshot.source, view);
      const drawing::Point listenerPoint =
          projectFieldPosition(snapshot, snapshot.listener, view);
      const double sourceDistance =
          std::hypot(point.x - sourcePoint.x, point.y - sourcePoint.y);
      const double listenerDistance =
          std::hypot(point.x - listenerPoint.x, point.y - listenerPoint.y);
      if (sourceDistance <= 14.0 && listenerDistance > 14.0)
        _editListener = false;
      else if (listenerDistance <= 14.0 && sourceDistance > 14.0)
        _editListener = true;
      _positionDragView = view;
      updatePositionAtPoint_view(point, view);
    };
    if (drawing::contains(point, fieldPlotRect(topFieldRect()))) {
      beginPositionDrag(1);
      return;
    }
    if (drawing::contains(point, fieldPlotRect(sideFieldRect()))) {
      beginPositionDrag(2);
      return;
    }
    struct Row {
      clap_id param;
      double y;
    };
    const clap_id xParam = _editListener ? kParamListenerX : kParamSourceX;
    const clap_id yParam = _editListener ? kParamListenerY : kParamSourceY;
    const clap_id zParam = _editListener ? kParamListenerZ : kParamSourceZ;
    const Row rows[] = {
        {kParamOutput, 70},  {xParam, 278},         {yParam, 302},
        {zParam, 326},       {kParamMovement, 350}, {kParamDoppler, 374},
        {kParamDirect, 440}, {kParamEarly, 462},    {kParamLate, 484},
        {kParamSize, 506},   {kParamScatter, 528},  {kParamWidth, 550},
        {kParamAir, 572}};
    for (const auto &row : rows) {
      if (!drawing::contains(point, drawing::makeRect(734, row.y - 4, 286, 18)))
        continue;
      if (event->clickCount() >= 2) {
        resetParam(row.param);
        return;
      }
      _dragParam = row.param;
      updateSliderAtPoint(point);
      return;
    }
  }
  void mouseDragged(drawing::Event *event) {
    drawing::Point point =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    point.y -= kContentTranslation;
    if (_positionDragView != 0) {
      updatePositionAtPoint_view(point, _positionDragView);
      return;
    }
    if (_dragParam != CLAP_INVALID_ID)
      updateSliderAtPoint(point);
  }
  void mouseUp(drawing::Event *event) {
    (void)event;
    _dragParam = CLAP_INVALID_ID;
    _positionDragView = 0;
  }
  void mouseMoved(drawing::Event *event) {
    drawing::Point point =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    point.y -= kContentTranslation;
    if (_atlasMenuOpen) {
      _atlasMenuHover =
          drawing::dropdownHitIndex(point, rayAtlasMenuRect(), 18,
                                    static_cast<uint32_t>(kRayAtlas.size()));
    } else if (_orderMenuOpen) {
      _orderMenuHover =
          drawing::dropdownHitIndex(point, orderMenuRect(), 18, 7u);
    } else {
      return;
    }
    setNeedsDisplay(true);
  }
  void mouseExited(drawing::Event *event) {
    (void)event;
    if (_atlasMenuHover == -1 && _orderMenuHover == -1)
      return;
    _atlasMenuHover = -1;
    _orderMenuHover = -1;
    setNeedsDisplay(true);
  }
#include "s3g_input_encoder_ray_files.inc"
  void markStateDirty() {
    if (p.host && p.host->get_extension) {
      auto *state = static_cast<const clap_host_state_t *>(
          p.host->get_extension(p.host, CLAP_EXT_STATE));
      if (state && state->mark_dirty)
        state->mark_dirty(p.host);
    }
  }
  bool handleTitleClick(drawing::Point pt,
                        const s3g::gui_layout::EncoderTitleBand &b) {
    if (drawing::contains(pt, drawing::cocoaRect(b.presetMenu))) {
      EncoderParams initial{};
      initial.order = guiParams.order;
      initial.outputGainDb = guiParams.outputGainDb;
      initial.listenerX = guiParams.listenerX;
      initial.listenerY = guiParams.listenerY;
      initial.listenerZ = guiParams.listenerZ;
      initial.fieldListenMode = guiParams.fieldListenMode;
      std::vector<std::pair<clap_id, double>> values;
      for (auto i : infos)
        values.emplace_back(i.id, readEncoderScalar(initial, i.id));
      end();
      if (batch(values))
        presetName = "INIT";
    } else if (drawing::contains(pt, drawing::cocoaRect(b.loadButton)))
      presetDialog(false);
    else if (drawing::contains(pt, drawing::cocoaRect(b.saveButton)))
      presetDialog(true);
    else
      return false;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                  presetName.c_str());
    service();
    invalid();
    return true;
  }
};
} // namespace input_encoder_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new input_encoder_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
