// Literal Cocoa room fields, reflection paths, layout and pointer algorithms.
namespace input_encoder_canvas {
namespace drawing = s3g::portable_gui::input_encoder_drawing;
namespace foundation = s3g::portable_gui::foundation;
using namespace VSTGUI;
using namespace s3g::portable_gui::routing;
using Kind = s3g::clap_gui::ParamEventKind;
constexpr const char *pluginName = "Ambi Ray Bilocation Encoder";
constexpr const char *pluginTitle = "s3g AMBI ENCODER RAY BILOCATION";
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
  p.fieldA = builtinField(false);
  p.fieldB = builtinField(true);
  p.pairName = "NEAR / FAR BUILT-IN";
  init(&p.plugin);
}
#define S3G_ROUTING_CUSTOM_ACCESS 1
#include "s3g_routing_parameter_editor.inc"
#undef S3G_ROUTING_CUSTOM_ACCESS
struct GuiFieldSnapshot {
  s3g::AmbiRayRoom room{};
  s3g::portable_gui::ray_field_files::VisualGeometry visual{};
  std::string name;
  std::vector<s3g::Vec3> cells;
  std::vector<s3g::AmbiRayReflection> reflections;
  s3g::Vec3 source{};
  s3g::Vec3 listener{};
};

struct GuiSnapshot {
  GuiFieldSnapshot fieldA;
  GuiFieldSnapshot fieldB;
  s3g::AmbiRayBilocationParams params{};
  std::string pairName;
  std::string status;
  int selectedPair = -1;
};

s3g::Vec3 normalizedPosition(const s3g::AmbiRayRoom &room,
                             s3g::Vec3 normalized) {
  const auto &minimum = room.navigationMinimumMetres;
  const auto &maximum = room.navigationMaximumMetres;
  return {s3g::lerp(minimum.x, maximum.x, normalized.x),
          s3g::lerp(minimum.y, maximum.y, normalized.y),
          s3g::lerp(minimum.z, maximum.z, normalized.z)};
}

GuiSnapshot guiSnapshot(Plugin &plugin) {
  std::lock_guard<std::mutex> lock(plugin.stateMutex);
  GuiSnapshot result;
  result.params = snapshotEncoderParams(plugin);
  result.pairName = plugin.pairName;
  result.status = plugin.status;
  result.selectedPair = plugin.selectedPair;
  const s3g::Vec3 sourceA{result.params.sourceX, result.params.sourceY,
                          result.params.sourceZ};
  const s3g::Vec3 sourceB =
      s3g::mapAmbiRayBilocationSource(sourceA, result.params.mapMode);
  const s3g::Vec3 listener{result.params.listenerX, result.params.listenerY,
                           result.params.listenerZ};
  auto build = [&](const FieldState &field, s3g::Vec3 sourceNormalized,
                   GuiFieldSnapshot &snapshot) {
    snapshot.room = field.descriptor.room;
    snapshot.visual = field.visual;
    snapshot.name = field.name;
    snapshot.source = normalizedPosition(snapshot.room, sourceNormalized);
    snapshot.listener = normalizedPosition(snapshot.room, listener);
    snapshot.cells.reserve(field.descriptor.cells.size());
    uint32_t nearest = 0u;
    float nearestDistance = std::numeric_limits<float>::max();
    for (uint32_t index = 0u; index < field.descriptor.cells.size(); ++index) {
      const auto &cell = field.descriptor.cells[index];
      snapshot.cells.push_back(cell.positionMetres);
      const float dx = snapshot.source.x - cell.positionMetres.x;
      const float dy = snapshot.source.y - cell.positionMetres.y;
      const float dz = snapshot.source.z - cell.positionMetres.z;
      const float distance = dx * dx + dy * dy + dz * dz;
      if (distance < nearestDistance) {
        nearestDistance = distance;
        nearest = index;
      }
    }
    if (nearest < field.descriptor.cells.size()) {
      const auto &reflections = field.descriptor.cells[nearest].reflections;
      const uint32_t count =
          std::min<uint32_t>(12u, static_cast<uint32_t>(reflections.size()));
      snapshot.reflections.assign(reflections.begin(),
                                  reflections.begin() + count);
    }
  };
  build(plugin.fieldA, sourceA, result.fieldA);
  build(plugin.fieldB, sourceB, result.fieldB);
  return result;
}

drawing::Rect fieldARect() { return drawing::makeRect(12, 34, 570, 560); }
drawing::Rect fieldBRect() { return drawing::makeRect(658, 34, 570, 560); }
drawing::Rect fieldPlanPlotRect(drawing::Rect field) {
  return drawing::makeRect(field.origin.x + 16, field.origin.y + 31,
                           field.size.width - 32, 326);
}
drawing::Rect fieldElevationPlotRect(drawing::Rect field) {
  return drawing::makeRect(field.origin.x + 16, field.origin.y + 377,
                           field.size.width - 32, 133);
}
drawing::Rect pairButtonRect() { return drawing::makeRect(590, 36, 60, 18); }
drawing::Rect pairMenuRect() {
  return drawing::makeRect(430, 58, 380,
                           20.0 * static_cast<double>(kPairPresets.size()));
}
drawing::Rect loadAButtonRect() { return drawing::makeRect(500, 36, 68, 18); }
drawing::Rect loadBButtonRect() { return drawing::makeRect(1146, 36, 68, 18); }
drawing::Rect sourceModeRect() { return drawing::makeRect(594, 78, 52, 20); }
drawing::Rect listenerModeRect() { return drawing::makeRect(594, 104, 52, 20); }
drawing::Rect placeTrackRect() { return drawing::makeRect(36, 616, 1168, 12); }
drawing::Rect spaceACharacterPanelRect() {
  return drawing::makeRect(12, 656, 386, 94);
}
drawing::Rect positionZPanelRect() {
  return drawing::makeRect(410, 656, 420, 94);
}
drawing::Rect spaceBCharacterPanelRect() {
  return drawing::makeRect(842, 656, 386, 94);
}
drawing::Rect membranePanelRect() {
  return drawing::makeRect(12, 762, 594, 124);
}
drawing::Rect outputPanelRect() {
  return drawing::makeRect(618, 762, 610, 124);
}
drawing::Rect fieldListenButtonRect(uint32_t mode) {
  return drawing::makeRect(678 + static_cast<double>(mode) * 36.0, 849, 32, 17);
}
drawing::Rect bypassButtonRect() { return drawing::makeRect(894, 849, 54, 15); }

struct SliderLayout {
  clap_id id = CLAP_INVALID_ID;
  const char *label = "";
  drawing::Rect row{};
  double labelWidth = 54.0;
};

const std::array<SliderLayout, 21> &sliderLayouts() {
  static const std::array<SliderLayout, 21> layouts{{
      {kParamSizeA, "SIZE", drawing::makeRect(26, 690, 172, 18)},
      {kParamScatterA, "SCAT", drawing::makeRect(26, 716, 172, 18)},
      {kParamWidthA, "WIDTH", drawing::makeRect(212, 690, 172, 18)},
      {kParamAirA, "AIR", drawing::makeRect(212, 716, 172, 18)},
      {kParamSourceZ, "SRC Z", drawing::makeRect(424, 702, 190, 18), 58.0},
      {kParamListenerZ, "LIS Z", drawing::makeRect(626, 702, 190, 18), 58.0},
      {kParamSizeB, "SIZE", drawing::makeRect(856, 690, 172, 18)},
      {kParamScatterB, "SCAT", drawing::makeRect(856, 716, 172, 18)},
      {kParamWidthB, "WIDTH", drawing::makeRect(1042, 690, 172, 18)},
      {kParamAirB, "AIR", drawing::makeRect(1042, 716, 172, 18)},
      {kParamPermeability, "PERMEABILITY", drawing::makeRect(26, 798, 276, 18),
       106.0},
      {kParamMemory, "MEMORY", drawing::makeRect(26, 824, 276, 18), 82.0},
      {kParamSeparation, "SEPARATION", drawing::makeRect(26, 850, 276, 18),
       82.0},
      {kParamMovement, "MOVEMENT", drawing::makeRect(318, 798, 274, 18), 82.0},
      {kParamDoppler, "DOPPLER", drawing::makeRect(318, 824, 274, 18), 82.0},
      {kParamOutput, "OUT", drawing::makeRect(632, 798, 180, 18)},
      {kParamOrder, "ORDER", drawing::makeRect(632, 824, 180, 18)},
      {kParamDirect, "DIRECT", drawing::makeRect(824, 798, 180, 18)},
      {kParamEarly, "EARLY", drawing::makeRect(824, 824, 180, 18)},
      {kParamLate, "LATE", drawing::makeRect(1016, 798, 198, 18)},
      {kParamMapMode, "MAP", drawing::makeRect(1016, 824, 198, 18)},
  }};
  return layouts;
}

bool isParameterMenu(clap_id id) {
  return id == kParamOrder || id == kParamMapMode;
}

const SliderLayout *layoutForParam(clap_id id) {
  for (const auto &layout : sliderLayouts()) {
    if (layout.id == id)
      return &layout;
  }
  return nullptr;
}

uint32_t parameterMenuItemCount(clap_id id) {
  return id == kParamOrder ? 7u : id == kParamMapMode ? 4u : 0u;
}

drawing::Rect parameterMenuBoxRect(clap_id id) {
  const auto *layout = layoutForParam(id);
  if (!layout)
    return drawing::Rect{};
  return drawing::makeRect(layout->row.origin.x + layout->labelWidth,
                           layout->row.origin.y + 2.0,
                           layout->row.size.width - layout->labelWidth, 15.0);
}

drawing::Rect parameterDropdownRect(clap_id id) {
  const auto *layout = layoutForParam(id);
  const uint32_t itemCount = parameterMenuItemCount(id);
  if (!layout || itemCount == 0u)
    return drawing::Rect{};
  constexpr double itemHeight = 18.0;
  const double height = itemHeight * static_cast<double>(itemCount);
  const drawing::Rect box = parameterMenuBoxRect(id);
  const double preferredY = drawing::maxY(box) + 2.0;
  const double y = preferredY + height <= kContentCoordinateHeight - 12.0
                       ? preferredY
                       : layout->row.origin.y - height - 2.0;
  return drawing::makeRect(box.origin.x, y, box.size.width, height);
}

double normalizedParamValue(clap_id id, const s3g::AmbiRayBilocationParams &p) {
  switch (id) {
  case kParamSourceZ:
    return p.sourceZ;
  case kParamListenerZ:
    return p.listenerZ;
  case kParamPermeability:
    return p.permeability;
  case kParamMemory:
    return p.memorySeconds / 12.0;
  case kParamSeparation:
    return p.separationDeg / 180.0;
  case kParamMovement:
    return (p.movementMs - 10.0) / 490.0;
  case kParamDoppler:
    return p.doppler / 2.0;
  case kParamSizeA:
    return (p.sizeA - 0.5) / 1.5;
  case kParamSizeB:
    return (p.sizeB - 0.5) / 1.5;
  case kParamScatterA:
    return p.scatterA;
  case kParamScatterB:
    return p.scatterB;
  case kParamWidthA:
    return p.widthA / 1.5;
  case kParamWidthB:
    return p.widthB / 1.5;
  case kParamAirA:
    return p.airA;
  case kParamAirB:
    return p.airB;
  case kParamDirect:
    return p.direct / 1.5;
  case kParamEarly:
    return p.early / 1.5;
  case kParamLate:
    return p.late / 1.5;
  case kParamOutput:
    return (p.outputGainDb + 60.0) / 72.0;
  case kParamOrder:
    return (p.order - 1.0) / 6.0;
  case kParamMapMode:
    return static_cast<uint32_t>(p.mapMode) / 3.0;
  default:
    return 0.0;
  }
}

double valueFromNormalized(clap_id id, double norm) {
  norm = std::clamp(norm, 0.0, 1.0);
  switch (id) {
  case kParamSourceZ:
  case kParamListenerZ:
  case kParamPermeability:
  case kParamScatterA:
  case kParamScatterB:
  case kParamAirA:
  case kParamAirB:
    return norm;
  case kParamMemory:
    return norm * 12.0;
  case kParamSeparation:
    return norm * 180.0;
  case kParamMovement:
    return 10.0 + norm * 490.0;
  case kParamDoppler:
    return norm * 2.0;
  case kParamSizeA:
  case kParamSizeB:
    return 0.5 + norm * 1.5;
  case kParamWidthA:
  case kParamWidthB:
  case kParamDirect:
  case kParamEarly:
  case kParamLate:
    return norm * 1.5;
  case kParamOutput:
    return -60.0 + norm * 72.0;
  case kParamOrder:
    return std::lround(1.0 + norm * 6.0);
  case kParamMapMode:
    return std::lround(norm * 3.0);
  default:
    return norm;
  }
}

drawing::String parameterText(clap_id id,
                              const s3g::AmbiRayBilocationParams &p) {
  char text[64]{};
  double value = 0.0;
  switch (id) {
  case kParamSourceZ:
    value = p.sourceZ;
    break;
  case kParamListenerZ:
    value = p.listenerZ;
    break;
  case kParamPermeability:
    value = p.permeability;
    break;
  case kParamMemory:
    value = p.memorySeconds;
    break;
  case kParamSeparation:
    value = p.separationDeg;
    break;
  case kParamMovement:
    value = p.movementMs;
    break;
  case kParamDoppler:
    value = p.doppler;
    break;
  case kParamSizeA:
    value = p.sizeA;
    break;
  case kParamSizeB:
    value = p.sizeB;
    break;
  case kParamScatterA:
    value = p.scatterA;
    break;
  case kParamScatterB:
    value = p.scatterB;
    break;
  case kParamWidthA:
    value = p.widthA;
    break;
  case kParamWidthB:
    value = p.widthB;
    break;
  case kParamAirA:
    value = p.airA;
    break;
  case kParamAirB:
    value = p.airB;
    break;
  case kParamDirect:
    value = p.direct;
    break;
  case kParamEarly:
    value = p.early;
    break;
  case kParamLate:
    value = p.late;
    break;
  case kParamOutput:
    value = p.outputGainDb;
    break;
  case kParamOrder:
    value = p.order;
    break;
  case kParamMapMode:
    value = static_cast<uint32_t>(p.mapMode);
    break;
  default:
    break;
  }
  paramsValueToText(nullptr, id, value, text, sizeof(text));
  return drawing::stringFromUtf8(text);
}

struct FieldBounds {
  float minX = 0.0f;
  float maxX = 1.0f;
  float minY = 0.0f;
  float maxY = 1.0f;
};

FieldBounds fieldBounds(const GuiFieldSnapshot &field) {
  FieldBounds bounds{field.room.navigationMinimumMetres.x,
                     field.room.navigationMaximumMetres.x,
                     field.room.navigationMinimumMetres.y,
                     field.room.navigationMaximumMetres.y};
  auto include = [&](float x, float y) {
    bounds.minX = std::min(bounds.minX, x);
    bounds.maxX = std::max(bounds.maxX, x);
    bounds.minY = std::min(bounds.minY, y);
    bounds.maxY = std::max(bounds.maxY, y);
  };
  for (const auto &point : field.room.polygon)
    include(point[0], point[1]);
  for (const auto &region : field.visual.regions)
    for (const auto &point : region.polygon)
      include(point[0], point[1]);
  const float padX = std::max(0.1f, (bounds.maxX - bounds.minX) * 0.06f);
  const float padY = std::max(0.1f, (bounds.maxY - bounds.minY) * 0.06f);
  bounds.minX -= padX;
  bounds.maxX += padX;
  bounds.minY -= padY;
  bounds.maxY += padY;
  return bounds;
}

drawing::Point projectPlanPosition(const GuiFieldSnapshot &field,
                                   drawing::Rect plot, s3g::Vec3 position) {
  const auto bounds = fieldBounds(field);
  const float x =
      (position.x - bounds.minX) / std::max(0.001f, bounds.maxX - bounds.minX);
  const float y =
      (position.y - bounds.minY) / std::max(0.001f, bounds.maxY - bounds.minY);
  return drawing::makePoint(plot.origin.x + x * plot.size.width,
                            plot.origin.y + (1.0f - y) * plot.size.height);
}

struct ElevationBounds {
  float minX = 0.0f;
  float maxX = 1.0f;
  float minZ = 0.0f;
  float maxZ = 1.0f;
};

ElevationBounds elevationBounds(const GuiFieldSnapshot &field) {
  ElevationBounds bounds{
      field.room.navigationMinimumMetres.x,
      field.room.navigationMaximumMetres.x,
      std::min(0.0f, field.room.navigationMinimumMetres.z),
      std::max(field.room.heightMetres, field.room.navigationMaximumMetres.z)};
  auto include = [&](float x, float z) {
    bounds.minX = std::min(bounds.minX, x);
    bounds.maxX = std::max(bounds.maxX, x);
    bounds.minZ = std::min(bounds.minZ, z);
    bounds.maxZ = std::max(bounds.maxZ, z);
  };
  for (const auto &point : field.room.ceilingProfile)
    include(point[0], point[1]);
  for (const auto &region : field.visual.regions) {
    for (const auto &point : region.polygon) {
      include(point[0], region.baseHeight);
      include(point[0], region.baseHeight + region.height);
    }
  }
  for (const auto &portal : field.visual.portals)
    include(portal.x, portal.z);
  for (const auto &cell : field.cells)
    include(cell.x, cell.z);
  for (const auto &reflection : field.reflections)
    if (reflection.hasBouncePosition)
      include(reflection.bouncePositionMetres.x,
              reflection.bouncePositionMetres.z);
  include(field.source.x, field.source.z);
  include(field.listener.x, field.listener.z);
  if (bounds.maxX < bounds.minX + 0.1f)
    bounds.maxX = bounds.minX + 0.1f;
  if (bounds.maxZ < bounds.minZ + 0.1f)
    bounds.maxZ = bounds.minZ + 0.1f;
  const float padX = std::max(0.1f, (bounds.maxX - bounds.minX) * 0.05f);
  const float padZ = std::max(0.1f, (bounds.maxZ - bounds.minZ) * 0.08f);
  bounds.minX -= padX;
  bounds.maxX += padX;
  bounds.minZ -= padZ;
  bounds.maxZ += padZ;
  return bounds;
}

drawing::Point projectElevationPosition(const GuiFieldSnapshot &field,
                                        drawing::Rect plot,
                                        s3g::Vec3 position) {
  const auto bounds = elevationBounds(field);
  const float x =
      (position.x - bounds.minX) / std::max(0.001f, bounds.maxX - bounds.minX);
  const float z =
      (position.z - bounds.minZ) / std::max(0.001f, bounds.maxZ - bounds.minZ);
  return drawing::makePoint(plot.origin.x + x * plot.size.width,
                            plot.origin.y + (1.0f - z) * plot.size.height);
}

class Editor final : public ParameterEditor {
public:
  explicit Editor(Plugin &instance) : ParameterEditor(instance) {
    auto *plugin = &p;
    _plugin = plugin;
    _dragParam = CLAP_INVALID_ID;
    _dragField = 0;
    _editListener = false;
    _pairMenuOpen = false;
    _pairMenuHover = -1;
    _openParameterMenu = CLAP_INVALID_ID;
    _parameterMenuHover = -1;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CURRENT");
    service();
  }
  ~Editor() override { stopRefresh(); }
  void stopRefresh() override {
    ParameterEditor::stopRefresh();
    pointerEditing = false;
    _dragParam = CLAP_INVALID_ID;
    _dragField = 0;
    _pairMenuOpen = false;
    _openParameterMenu = CLAP_INVALID_ID;
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
  int _dragField;
  bool _editListener;
  bool _pairMenuOpen;
  int _pairMenuHover;
  clap_id _openParameterMenu;
  int _parameterMenuHover;
  char _titlePresetName[64];
  void setStatus(const std::string &status) {
    std::lock_guard<std::mutex> lock(_plugin->stateMutex);
    _plugin->status = status;
  }
  void drawField_rect_source_listener_gain_label_attrs(
      const GuiFieldSnapshot &field, drawing::Rect rect,
      s3g::Vec3 sourcePosition, s3g::Vec3 listenerPosition, float gain,
      drawing::String label, drawing::Attrs attrs) {
    const drawing::Rect plan = fieldPlanPlotRect(rect);
    const drawing::Rect elevation = fieldElevationPlotRect(rect);
    VSTGUI::CColor sourceColor = drawing::calibratedColor(
        (label == "A") ? 0x69c7d2 : 0xd8a24a, 0.42 + gain * 0.58);

    drawing::setFill(drawing::calibratedColor(0x0b1012));
    drawing::rectFill(plan);
    drawing::rectFill(elevation);
    drawing::setStroke(drawing::calibratedColor(0x343b3e));
    drawing::frameRect(plan);
    drawing::frameRect(elevation);

    drawing::GraphicsScope plotScope0(*context);
    drawing::clipRect(drawing::inset(plan, 1, 1));
    auto drawPolygon = [&](const std::vector<std::array<float, 2>> &polygon,
                           VSTGUI::CColor color, double width) {
      if (polygon.size() < 2u)
        return;
      drawing::Path path = drawing::Path::bezierPath();
      (path).moveToPoint(projectPlanPosition(
          field, plan, {polygon[0][0], polygon[0][1], 0.0f}));
      for (size_t index = 1u; index < polygon.size(); ++index)
        (path).lineToPoint(projectPlanPosition(
            field, plan, {polygon[index][0], polygon[index][1], 0.0f}));
      (path).closePath();
      (path).setLineWidth(width);
      drawing::setStroke(color);
      (path).stroke();
    };
    for (const auto &region : field.visual.regions)
      drawPolygon(region.polygon, drawing::calibratedColor(0x526167, 0.48),
                  1.0);
    drawPolygon(field.room.polygon, drawing::calibratedColor(0xa9b1b3, 0.80),
                1.7);
    for (const auto &portal : field.visual.portals) {
      drawing::Point point = projectPlanPosition(field, plan, portal);
      drawing::setFill(drawing::calibratedColor(0xd8a24a, 0.72));
      drawing::rectFill(drawing::makeRect(point.x - 3, point.y - 3, 6, 6));
    }
    for (const auto &cell : field.cells) {
      drawing::Point point = projectPlanPosition(field, plan, cell);
      drawing::setFill(drawing::calibratedColor(0x607076, 0.64));
      drawing::rectFill(drawing::makeRect(point.x - 1.5, point.y - 1.5, 3, 3));
    }
    const drawing::Point sourcePoint =
        projectPlanPosition(field, plan, sourcePosition);
    const drawing::Point listenerPoint =
        projectPlanPosition(field, plan, listenerPosition);
    for (const auto &reflection : field.reflections) {
      if (!reflection.hasBouncePosition)
        continue;
      drawing::Point bounce =
          projectPlanPosition(field, plan, reflection.bouncePositionMetres);
      drawing::Path path = drawing::Path::bezierPath();
      (path).moveToPoint(sourcePoint);
      (path).lineToPoint(bounce);
      (path).lineToPoint(listenerPoint);
      (path).setLineWidth(0.75);
      drawing::setStroke(
          drawing::calibratedColor(0xd8a24a, 0.12 + gain * 0.34));
      (path).stroke();
    }
    drawing::Path direct = drawing::Path::bezierPath();
    (direct).moveToPoint(sourcePoint);
    (direct).lineToPoint(listenerPoint);
    (direct).setLineWidth(1.1);
    drawing::setStroke(drawing::calibratedColor(0xc2c6c7, 0.28 + gain * 0.55));
    (direct).stroke();
    drawing::setFill(drawing::calibratedColor(0xbcbcbc));
    drawing::rectFill(
        drawing::makeRect(listenerPoint.x - 4, listenerPoint.y - 4, 8, 8));
    drawing::setFill(sourceColor);
    drawing::rectFill(
        drawing::makeRect(sourcePoint.x - 7, sourcePoint.y - 7, 14, 14));
    plotScope0.restore();

    drawing::drawAtPoint_withAttributes(
        "XY / PLAN", drawing::makePoint(plan.origin.x + 7, plan.origin.y + 5),
        attrs);
    drawing::drawAtPoint_withAttributes(
        "XZ / ELEVATION  ·  DRAG FOR HEIGHT",
        drawing::makePoint(elevation.origin.x, elevation.origin.y - 17), attrs);

    drawing::GraphicsScope plotScope1(*context);
    drawing::clipRect(drawing::inset(elevation, 1, 1));
    const auto sideBounds = elevationBounds(field);
    auto sidePoint = [&](float x, float z) {
      return projectElevationPosition(field, elevation, {x, 0.0f, z});
    };

    drawing::setStroke(drawing::calibratedColor(0x24292b));
    for (int index = 1; index < 5; ++index) {
      const double x = elevation.origin.x +
                       elevation.size.width * static_cast<double>(index) / 5.0;
      const double y = elevation.origin.y +
                       elevation.size.height * static_cast<double>(index) / 5.0;
      drawing::Path::strokeLineFromPoint_toPoint(
          drawing::makePoint(x, elevation.origin.y),
          drawing::makePoint(x, drawing::maxY(elevation)));
      drawing::Path::strokeLineFromPoint_toPoint(
          drawing::makePoint(elevation.origin.x, y),
          drawing::makePoint(drawing::maxX(elevation), y));
    }

    drawing::Path roomSide = drawing::Path::bezierPath();
    (roomSide).moveToPoint(sidePoint(sideBounds.minX, 0.0f));
    (roomSide).lineToPoint(sidePoint(sideBounds.maxX, 0.0f));
    if (field.room.ceilingProfile.size() >= 2u) {
      for (auto iterator = field.room.ceilingProfile.rbegin();
           iterator != field.room.ceilingProfile.rend(); ++iterator)
        (roomSide).lineToPoint(sidePoint((*iterator)[0], (*iterator)[1]));
    } else {
      (roomSide).lineToPoint(
          sidePoint(sideBounds.maxX, field.room.heightMetres));
      (roomSide).lineToPoint(
          sidePoint(sideBounds.minX, field.room.heightMetres));
    }
    (roomSide).closePath();
    drawing::setFill(drawing::calibratedColor(0x788084, 0.09));
    (roomSide).fill();
    drawing::setStroke(drawing::calibratedColor(0xa9b1b3, 0.78));
    (roomSide).setLineWidth(1.4);
    (roomSide).stroke();

    for (const auto &region : field.visual.regions) {
      if (region.polygon.empty())
        continue;
      float minimumX = region.polygon.front()[0];
      float maximumX = minimumX;
      for (const auto &point : region.polygon) {
        minimumX = std::min(minimumX, point[0]);
        maximumX = std::max(maximumX, point[0]);
      }
      drawing::Point topLeft =
          sidePoint(minimumX, region.baseHeight + region.height);
      drawing::Point bottomRight = sidePoint(maximumX, region.baseHeight);
      drawing::Path regionSide = drawing::Path::bezierPathWithRect(
          drawing::makeRect(topLeft.x, topLeft.y,
                            std::max<double>(1.0, bottomRight.x - topLeft.x),
                            std::max<double>(1.0, bottomRight.y - topLeft.y)));
      drawing::setFill(drawing::calibratedColor(0x668b78, 0.07));
      (regionSide).fill();
      drawing::setStroke(drawing::calibratedColor(0x78ad91, 0.50));
      (regionSide).stroke();
    }

    const auto &navMin = field.room.navigationMinimumMetres;
    const auto &navMax = field.room.navigationMaximumMetres;
    drawing::Point navTopLeft = sidePoint(navMin.x, navMax.z);
    drawing::Point navBottomRight = sidePoint(navMax.x, navMin.z);
    drawing::Path navigation =
        drawing::Path::bezierPathWithRect(drawing::makeRect(
            navTopLeft.x, navTopLeft.y,
            std::max<double>(1.0, navBottomRight.x - navTopLeft.x),
            std::max<double>(1.0, navBottomRight.y - navTopLeft.y)));
    const double navigationDash[] = {4.0, 3.0};
    (navigation).setLineDash_count_phase(navigationDash, 2, 0.0);
    drawing::setStroke(drawing::calibratedColor(0x7199a1, 0.48));
    (navigation).stroke();

    const float depthRange = std::max(0.001f, navMax.y - navMin.y);
    for (const auto &cell : field.cells) {
      drawing::Point point = projectElevationPosition(field, elevation, cell);
      const float depth =
          s3g::clamp((cell.y - navMin.y) / depthRange, 0.0f, 1.0f);
      drawing::setFill(drawing::calibratedColor(0x607076, 0.24 + depth * 0.46));
      drawing::rectFill(drawing::makeRect(point.x - 1.5, point.y - 1.5, 3, 3));
    }
    for (const auto &portal : field.visual.portals) {
      drawing::Point point = projectElevationPosition(field, elevation, portal);
      drawing::setFill(drawing::calibratedColor(0xd8a24a, 0.68));
      drawing::rectFill(drawing::makeRect(point.x - 2.5, point.y - 2.5, 5, 5));
    }

    const drawing::Point sourceSide =
        projectElevationPosition(field, elevation, sourcePosition);
    const drawing::Point listenerSide =
        projectElevationPosition(field, elevation, listenerPosition);
    for (const auto &reflection : field.reflections) {
      if (!reflection.hasBouncePosition)
        continue;
      const drawing::Point bounce = projectElevationPosition(
          field, elevation, reflection.bouncePositionMetres);
      drawing::Path ray = drawing::Path::bezierPath();
      (ray).moveToPoint(sourceSide);
      (ray).lineToPoint(bounce);
      (ray).lineToPoint(listenerSide);
      (ray).setLineWidth(0.65);
      drawing::setStroke(
          drawing::calibratedColor(0xd8a24a, 0.10 + gain * 0.30));
      (ray).stroke();
    }
    drawing::Path heightRule = drawing::Path::bezierPath();
    (heightRule)
        .moveToPoint(drawing::makePoint(elevation.origin.x, sourceSide.y));
    (heightRule)
        .lineToPoint(
            drawing::makePoint(drawing::maxX(elevation), sourceSide.y));
    const double heightDash[] = {2.0, 4.0};
    (heightRule).setLineDash_count_phase(heightDash, 2, 0.0);
    drawing::setStroke(sourceColor);
    (heightRule).stroke();
    drawing::Path directSide = drawing::Path::bezierPath();
    (directSide).moveToPoint(sourceSide);
    (directSide).lineToPoint(listenerSide);
    (directSide).setLineWidth(1.0);
    drawing::setStroke(drawing::calibratedColor(0xc2c6c7, 0.25 + gain * 0.50));
    (directSide).stroke();
    drawing::setFill(drawing::calibratedColor(0xbcbcbc));
    drawing::rectFill(
        drawing::makeRect(listenerSide.x - 4, listenerSide.y - 4, 8, 8));
    drawing::setFill(sourceColor);
    drawing::rectFill(
        drawing::makeRect(sourceSide.x - 6, sourceSide.y - 6, 12, 12));
    plotScope1.restore();

    drawing::String sourceHeight =
        drawing::stringFormat("SRC %.2f m", sourcePosition.z);
    drawing::String listenerHeight =
        drawing::stringFormat("LIS %.2f m", listenerPosition.z);
    drawing::drawAtPoint_withAttributes(
        sourceHeight,
        drawing::makePoint(elevation.origin.x + 7, elevation.origin.y + 5),
        attrs);
    const double listenerWidth =
        drawing::sizeWithAttributes(listenerHeight, attrs).width;
    drawing::drawAtPoint_withAttributes(
        listenerHeight,
        drawing::makePoint(drawing::maxX(elevation) - listenerWidth - 7,
                           elevation.origin.y + 5),
        attrs);

    drawing::drawAtPoint_withAttributes(
        label, drawing::makePoint(rect.origin.x + 12, rect.origin.y + 5),
        attrs);
    drawing::String name = drawing::stringFromUtf8(field.name.c_str());
    drawing::drawAtPoint_withAttributes(
        name, drawing::makePoint(rect.origin.x + 34, rect.origin.y + 5), attrs);
    drawing::String details = drawing::stringFormat(
        "%zu CELLS  %.1f x %.1f x %.1f m  PRES %.0f%%", field.cells.size(),
        field.room.widthMetres, field.room.depthMetres, field.room.heightMetres,
        gain * 100.0f);
    drawing::drawAtPoint_withAttributes(
        details,
        drawing::makePoint(rect.origin.x + 16, drawing::maxY(rect) - 27),
        attrs);
  }
  void drawSlider_params_labelAttrs_valueAttrs_style(
      const SliderLayout &layout, const s3g::AmbiRayBilocationParams &params,
      drawing::Attrs labelAttrs, drawing::Attrs valueAttrs,
      const drawing::Style &style) {
    drawing::String label = drawing::stringFromUtf8(layout.label);
    drawing::drawAtPoint_withAttributes(
        label, drawing::makePoint(layout.row.origin.x, layout.row.origin.y + 1),
        labelAttrs);
    drawing::Rect track = drawing::makeRect(
        layout.row.origin.x + layout.labelWidth, layout.row.origin.y + 4,
        layout.row.size.width - layout.labelWidth - 54.0, 9);
    drawing::setFill(style.strip);
    drawing::rectFill(track);
    drawing::setStroke(style.grid);
    drawing::frameRect(track);
    const double norm = std::clamp(
        static_cast<double>(normalizedParamValue(layout.id, params)), 0.0, 1.0);
    drawing::Rect fill = drawing::inset(track, 1, 1);
    fill.size.width = std::max<double>(1.0, fill.size.width * norm);
    drawing::setFill(
        (layout.id == kParamSizeA || layout.id == kParamScatterA ||
                 layout.id == kParamWidthA || layout.id == kParamAirA
             ? drawing::calibratedColor(0x69c7d2, 0.72)
             : layout.id == kParamSizeB || layout.id == kParamScatterB ||
                       layout.id == kParamWidthB || layout.id == kParamAirB
                   ? drawing::calibratedColor(0xd8a24a, 0.72)
                   : style.fill));
    drawing::rectFill(fill);
    const double handleX = std::clamp(
        track.origin.x + track.size.width * norm - 1.5, track.origin.x + 1.0,
        track.origin.x + track.size.width - 4.0);
    drawing::setFill(style.text);
    drawing::rectFill(drawing::makeRect(handleX, track.origin.y - 2.0, 3.0,
                                        track.size.height + 4.0));
    drawing::drawBoundedRightText(
        parameterText(layout.id, params),
        drawing::makeRect(drawing::maxX(layout.row) - 48.0,
                          layout.row.origin.y + 1.0, 42.0, 15.0),
        valueAttrs);
  }
  void paint() override {
    drawing::DrawingScope scope(*this, context, font, titleFont, &error);
    service();

    drawing::Style style;
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs labelAttrs = drawing::softLabelAttrs();
    drawing::Attrs valueAttrs = drawing::softValueAttrs();
    const GuiSnapshot snapshot = guiSnapshot(*_plugin);
    const auto gains = s3g::ambiRayBilocationGains(
        snapshot.params.place, snapshot.params.permeability);
    drawing::String peakText = drawing::peakDbText(
        _plugin->outputPeak.load(std::memory_order_relaxed));
    drawing::String status = drawing::stringFormat(
        "%@   %@   %@", drawing::stringFromUtf8(snapshot.pairName.c_str()),
        drawing::stringFromUtf8(snapshot.status.c_str()), peakText);
    drawing::drawEncoderTitleBand(
        "s3g AMBI ENCODER RAY BILOCATION",
        drawing::stringFromUtf8(_titlePresetName), status,
        drawing::encoderTitleBand(kGuiWidth, kGuiHeight),
        drawing::softTitleAttrs(), labelAttrs, valueAttrs, style);

    std::optional<VSTGUI::CDrawContext::Transform> contentTransform;
    contentTransform.emplace(*context, VSTGUI::CGraphicsTransform{}.translate(
                                           0., kContentTranslation));

    drawing::drawPanelFrame(12, 34, 570, 560, style);
    drawing::drawPanelFrame(658, 34, 570, 560, style);
    drawField_rect_source_listener_gain_label_attrs(
        snapshot.fieldA, fieldARect(), snapshot.fieldA.source,
        snapshot.fieldA.listener, gains.a, "A", valueAttrs);
    drawField_rect_source_listener_gain_label_attrs(
        snapshot.fieldB, fieldBRect(), snapshot.fieldB.source,
        snapshot.fieldB.listener, gains.b, "B", valueAttrs);

    const drawing::Rect elevationA = fieldElevationPlotRect(fieldARect());
    const drawing::Rect elevationB = fieldElevationPlotRect(fieldBRect());
    const double sourceYA =
        projectElevationPosition(snapshot.fieldA, elevationA,
                                 snapshot.fieldA.source)
            .y;
    const double sourceYB =
        projectElevationPosition(snapshot.fieldB, elevationB,
                                 snapshot.fieldB.source)
            .y;
    drawing::Path zLink = drawing::Path::bezierPath();
    (zLink).moveToPoint(
        drawing::makePoint(drawing::maxX(elevationA), sourceYA));
    (zLink).lineToPoint(drawing::makePoint(elevationB.origin.x, sourceYB));
    const double zDash[] = {3.0, 3.0};
    (zLink).setLineDash_count_phase(zDash, 2, 0.0);
    (zLink).setLineWidth(1.2);
    drawing::setStroke(drawing::calibratedColor(0xbba56f, 0.62));
    (zLink).stroke();
    drawing::setFill(drawing::calibratedColor(0x69c7d2, 0.88));
    drawing::rectFill(
        drawing::makeRect(drawing::maxX(elevationA) - 2, sourceYA - 2, 4, 4));
    drawing::setFill(drawing::calibratedColor(0xd8a24a, 0.88));
    drawing::rectFill(
        drawing::makeRect(elevationB.origin.x - 2, sourceYB - 2, 4, 4));
    drawing::String zMode =
        snapshot.params.mapMode == s3g::AmbiRayBilocationMapMode::Counter
            ? "Z COUNTER"
            : "Z LINK";
    const double zModeWidth =
        drawing::sizeWithAttributes(zMode, labelAttrs).width;
    drawing::drawAtPoint_withAttributes(
        zMode,
        drawing::makePoint(620.0 - zModeWidth * 0.5,
                           (sourceYA + sourceYB) * 0.5 - 16.0),
        labelAttrs);

    drawing::drawToolboxHeaderActionButton(loadAButtonRect(),
                                           drawing::makeRect(12, 34, 570, 21),
                                           "LOAD A", valueAttrs, style);
    drawing::drawToolboxHeaderActionButton(loadBButtonRect(),
                                           drawing::makeRect(658, 34, 570, 21),
                                           "LOAD B", valueAttrs, style);
    drawing::drawToolboxHeaderActionButton(pairButtonRect(),
                                           drawing::makeRect(584, 34, 72, 21),
                                           "PAIR", valueAttrs, style);
    drawing::drawHeaderButton(sourceModeRect(),
                              drawing::makeRect(584, 76, 72, 22), "SRC",
                              !_editListener, valueAttrs, style);
    drawing::drawHeaderButton(listenerModeRect(),
                              drawing::makeRect(584, 102, 72, 22), "LIS",
                              _editListener, valueAttrs, style);
    drawing::drawAtPoint_withAttributes("TWO", drawing::makePoint(608, 178),
                                        labelAttrs);
    drawing::drawAtPoint_withAttributes("PLACES", drawing::makePoint(594, 194),
                                        labelAttrs);
    drawing::drawAtPoint_withAttributes("ONE", drawing::makePoint(607, 230),
                                        labelAttrs);
    drawing::drawAtPoint_withAttributes("SOURCE", drawing::makePoint(592, 246),
                                        labelAttrs);
    drawing::setFill(drawing::calibratedColor(0x69c7d2, gains.a));
    drawing::rectFill(drawing::makeRect(606, 286, 10, 122 * gains.a));
    drawing::setFill(drawing::calibratedColor(0xd8a24a, gains.b));
    drawing::rectFill(drawing::makeRect(624, 286, 10, 122 * gains.b));

    drawing::setFill(style.strip);
    drawing::rectFill(placeTrackRect());
    drawing::setStroke(style.grid);
    drawing::frameRect(placeTrackRect());
    const double seamHalf =
        10.0 + snapshot.params.permeability *
                   (placeTrackRect().size.width * 0.48 - 10.0);
    drawing::setFill(drawing::calibratedColor(0x796f62, 0.28));
    drawing::rectFill(
        drawing::makeRect(drawing::midX(placeTrackRect()) - seamHalf,
                          placeTrackRect().origin.y + 1, seamHalf * 2,
                          placeTrackRect().size.height - 2));
    drawing::setFill(drawing::calibratedColor(0x69c7d2, 0.72));
    drawing::rectFill(drawing::makeRect(
        placeTrackRect().origin.x + 1, placeTrackRect().origin.y + 1,
        (placeTrackRect().size.width - 2) * (1.0 - snapshot.params.place),
        placeTrackRect().size.height - 2));
    const double markerX = placeTrackRect().origin.x +
                           snapshot.params.place * placeTrackRect().size.width;
    drawing::setFill(drawing::calibratedColor(0xf0f0f0));
    drawing::rectFill(drawing::makeRect(markerX - 2,
                                        placeTrackRect().origin.y - 4, 4,
                                        placeTrackRect().size.height + 8));
    drawing::drawAtPoint_withAttributes("A", drawing::makePoint(18, 613),
                                        labelAttrs);
    drawing::drawAtPoint_withAttributes(
        "PLACE / TRANSIT / OVERLAP", drawing::makePoint(532, 634), labelAttrs);
    drawing::drawAtPoint_withAttributes("B", drawing::makePoint(1213, 613),
                                        labelAttrs);

    const drawing::Rect spaceACharacterPanel = spaceACharacterPanelRect();
    const drawing::Rect positionZPanel = positionZPanelRect();
    const drawing::Rect spaceBCharacterPanel = spaceBCharacterPanelRect();
    const drawing::Rect membranePanel = membranePanelRect();
    const drawing::Rect outputPanel = outputPanelRect();
    drawing::drawPanelFrame(spaceACharacterPanel.origin.x,
                            spaceACharacterPanel.origin.y,
                            spaceACharacterPanel.size.width,
                            spaceACharacterPanel.size.height, style);
    drawing::drawPanelHeader(
        "SPACE A CHARACTER", true, spaceACharacterPanel.origin.x,
        spaceACharacterPanel.origin.y, spaceACharacterPanel.size.width, 21,
        labelAttrs, style);
    drawing::drawPanelFrame(positionZPanel.origin.x, positionZPanel.origin.y,
                            positionZPanel.size.width,
                            positionZPanel.size.height, style);
    drawing::drawPanelHeader("POSITION Z", true, positionZPanel.origin.x,
                             positionZPanel.origin.y, positionZPanel.size.width,
                             21, labelAttrs, style);
    drawing::drawPanelFrame(spaceBCharacterPanel.origin.x,
                            spaceBCharacterPanel.origin.y,
                            spaceBCharacterPanel.size.width,
                            spaceBCharacterPanel.size.height, style);
    drawing::drawPanelHeader(
        "SPACE B CHARACTER", true, spaceBCharacterPanel.origin.x,
        spaceBCharacterPanel.origin.y, spaceBCharacterPanel.size.width, 21,
        labelAttrs, style);
    drawing::drawPanelFrame(membranePanel.origin.x, membranePanel.origin.y,
                            membranePanel.size.width, membranePanel.size.height,
                            style);
    drawing::drawPanelHeader("BILOCATION MEMBRANE", true,
                             membranePanel.origin.x, membranePanel.origin.y,
                             membranePanel.size.width, 21, labelAttrs, style);
    drawing::drawPanelFrame(outputPanel.origin.x, outputPanel.origin.y,
                            outputPanel.size.width, outputPanel.size.height,
                            style);
    drawing::drawPanelHeader("OUTPUT / ROOM FIELD", true, outputPanel.origin.x,
                             outputPanel.origin.y, outputPanel.size.width, 21,
                             labelAttrs, style);
    for (const auto &layout : sliderLayouts()) {
      if (isParameterMenu(layout.id)) {
        drawing::String label = drawing::stringFromUtf8(layout.label);
        drawing::String value = parameterText(layout.id, snapshot.params);
        if (layout.id == kParamMapMode)
          value = drawing::uppercaseString(value);
        drawing::drawMenu(label, value, layout.row.origin.y + 3.0, labelAttrs,
                          valueAttrs, style, layout.row.origin.x,
                          layout.row.origin.x + layout.labelWidth,
                          layout.row.size.width - layout.labelWidth);
      } else {
        drawSlider_params_labelAttrs_valueAttrs_style(
            layout, snapshot.params, labelAttrs, valueAttrs, style);
      }
    }
    drawing::drawAtPoint_withAttributes("LST", drawing::makePoint(632, 851),
                                        labelAttrs);
    static drawing::String listenLabels[] = {"OFF", "FOL", "CTR", "BAL"};
    const uint32_t listenMode =
        static_cast<uint32_t>(snapshot.params.fieldListenMode);
    for (uint32_t mode = 0u; mode < 4u; ++mode) {
      drawing::drawHeaderButton(fieldListenButtonRect(mode), outputPanel,
                                listenLabels[mode], mode == listenMode,
                                valueAttrs, style);
    }
    drawing::drawToggle("BYP", snapshot.params.bypassRoom, 850, labelAttrs,
                        valueAttrs, style, 850, 894, 54);

    if (_pairMenuOpen) {
      drawing::String names[kPairPresets.size()];
      for (size_t index = 0u; index < kPairPresets.size(); ++index)
        names[index] = drawing::stringFromUtf8(kPairPresets[index].title);
      drawing::drawDropdownMenu(
          pairMenuRect(), 20, names, static_cast<uint32_t>(kPairPresets.size()),
          snapshot.selectedPair, _pairMenuHover, valueAttrs, style);
    }
    if (_openParameterMenu != CLAP_INVALID_ID) {
      if (_openParameterMenu == kParamOrder) {
        drawing::String items[] = {"1OA", "2OA", "3OA", "4OA",
                                   "5OA", "6OA", "7OA"};
        drawing::drawDropdownMenu(parameterDropdownRect(_openParameterMenu),
                                  18.0, items, 7u,
                                  static_cast<int>(snapshot.params.order) - 1,
                                  _parameterMenuHover, valueAttrs, style);
      } else if (_openParameterMenu == kParamMapMode) {
        drawing::String items[] = {"LINKED", "MIRROR X", "MIRROR Y", "COUNTER"};
        drawing::drawDropdownMenu(parameterDropdownRect(_openParameterMenu),
                                  18.0, items, 4u,
                                  static_cast<int>(snapshot.params.mapMode),
                                  _parameterMenuHover, valueAttrs, style);
      }
    }
    contentTransform.reset();
  }
  void updateSlider_point(clap_id param, drawing::Point point) {
    if (param == kParamPlace) {
      const double norm =
          (point.x - placeTrackRect().origin.x) / placeTrackRect().size.width;
      setParam_value(param, std::clamp(norm, 0.0, 1.0));
      return;
    }
    for (const auto &layout : sliderLayouts()) {
      if (layout.id != param)
        continue;
      const drawing::Rect track = drawing::makeRect(
          layout.row.origin.x + layout.labelWidth, layout.row.origin.y,
          layout.row.size.width - layout.labelWidth - 54.0,
          layout.row.size.height);
      const double norm =
          (point.x - track.origin.x) / std::max<double>(1.0, track.size.width);
      setParam_value(param, valueFromNormalized(param, norm));
      return;
    }
  }
  void updateMapPosition_field(drawing::Point point, int fieldIndex) {
    const GuiSnapshot snapshot = guiSnapshot(*_plugin);
    const bool isElevation = fieldIndex >= 3;
    const bool isFieldA = fieldIndex == 1 || fieldIndex == 3;
    const GuiFieldSnapshot &field =
        isFieldA ? snapshot.fieldA : snapshot.fieldB;
    const drawing::Rect fieldRect = isFieldA ? fieldARect() : fieldBRect();
    const drawing::Rect plot = isElevation ? fieldElevationPlotRect(fieldRect)
                                           : fieldPlanPlotRect(fieldRect);
    const auto planBounds = fieldBounds(field);
    const auto sideBounds = elevationBounds(field);
    const float minimumX = isElevation ? sideBounds.minX : planBounds.minX;
    const float maximumX = isElevation ? sideBounds.maxX : planBounds.maxX;
    const float worldX =
        s3g::lerp(minimumX, maximumX,
                  s3g::clamp(static_cast<float>((point.x - plot.origin.x) /
                                                plot.size.width),
                             0.0f, 1.0f));
    const auto &minimum = field.room.navigationMinimumMetres;
    const auto &maximum = field.room.navigationMaximumMetres;
    s3g::Vec3 normalized{
        (worldX - minimum.x) / std::max(0.0001f, maximum.x - minimum.x), 0.0f};
    normalized.x = s3g::clamp(normalized.x, 0.0f, 1.0f);
    if (isElevation) {
      const float worldZ = s3g::lerp(
          sideBounds.minZ, sideBounds.maxZ,
          s3g::clamp(static_cast<float>(1.0 - (point.y - plot.origin.y) /
                                                  plot.size.height),
                     0.0f, 1.0f));
      normalized.z = s3g::clamp((worldZ - minimum.z) /
                                    std::max(0.0001f, maximum.z - minimum.z),
                                0.0f, 1.0f);
    } else {
      const float worldY = s3g::lerp(
          planBounds.minY, planBounds.maxY,
          s3g::clamp(static_cast<float>(1.0 - (point.y - plot.origin.y) /
                                                  plot.size.height),
                     0.0f, 1.0f));
      normalized.y = s3g::clamp((worldY - minimum.y) /
                                    std::max(0.0001f, maximum.y - minimum.y),
                                0.0f, 1.0f);
    }
    if (!_editListener && !isFieldA)
      normalized =
          s3g::mapAmbiRayBilocationSource(normalized, snapshot.params.mapMode);
    setParam_value(_editListener ? kParamListenerX : kParamSourceX,
                   normalized.x);
    setParam_value(_editListener
                       ? (isElevation ? kParamListenerZ : kParamListenerY)
                       : (isElevation ? kParamSourceZ : kParamSourceY),
                   isElevation ? normalized.z : normalized.y);
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
      setParam_value(kParamPlace, randomUnit());
      setParam_value(kParamPermeability, 0.18 + randomUnit() * 0.76);
      setParam_value(kParamMemory, 0.25 + randomUnit() * 7.75);
      setParam_value(kParamSeparation, 25.0 + randomUnit() * 140.0);
      setParam_value(kParamMapMode, std::uniform_int_distribution<unsigned>(
                                        0, 3)(randomGenerator));
      setParam_value(kParamDirect, 0.55 + randomUnit() * 0.75);
      setParam_value(kParamEarly, 0.25 + randomUnit() * 0.90);
      setParam_value(kParamLate, 0.18 + randomUnit() * 0.82);
      setParam_value(kParamSizeA, 0.60 + randomUnit() * 1.25);
      setParam_value(kParamSizeB, 0.60 + randomUnit() * 1.25);
      setParam_value(kParamScatterA, 0.10 + randomUnit() * 0.78);
      setParam_value(kParamScatterB, 0.10 + randomUnit() * 0.78);
      setParam_value(kParamWidthA, 0.30 + randomUnit() * 1.10);
      setParam_value(kParamWidthB, 0.30 + randomUnit() * 1.10);
      std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "RANDOM");
      setNeedsDisplay(true);
      return;
    }
    point.y -= kContentTranslation;
    if (_pairMenuOpen) {
      const int hit =
          drawing::dropdownHitIndex(point, pairMenuRect(), 20,
                                    static_cast<uint32_t>(kPairPresets.size()));
      _pairMenuOpen = false;
      _pairMenuHover = -1;
      if (hit >= 0)
        loadPairAtIndex(static_cast<unsigned>(hit));
      setNeedsDisplay(true);
      return;
    }
    if (_openParameterMenu != CLAP_INVALID_ID) {
      const clap_id openMenu = _openParameterMenu;
      const uint32_t itemCount = parameterMenuItemCount(openMenu);
      const int hit = drawing::dropdownHitIndex(
          point, parameterDropdownRect(openMenu), 18.0, itemCount);
      _openParameterMenu = CLAP_INVALID_ID;
      _parameterMenuHover = -1;
      if (hit >= 0) {
        setParam_value(openMenu, openMenu == kParamOrder
                                     ? static_cast<double>(hit + 1)
                                     : static_cast<double>(hit));
      }
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, pairButtonRect())) {
      _openParameterMenu = CLAP_INVALID_ID;
      _parameterMenuHover = -1;
      _pairMenuOpen = true;
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, loadAButtonRect())) {
      loadField(1);
      return;
    }
    if (drawing::contains(point, loadBButtonRect())) {
      loadField(2);
      return;
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
    if (drawing::contains(point, drawing::inset(bypassButtonRect(), -4, -3))) {
      setParam_value(kParamBypass, guiParams.bypassRoom ? 0.0 : 1.0);
      return;
    }
    for (uint32_t mode = 0u; mode < 4u; ++mode) {
      if (drawing::contains(point, fieldListenButtonRect(mode))) {
        setParam_value(kParamFieldListen, mode);
        return;
      }
    }
    if (drawing::contains(point, drawing::inset(placeTrackRect(), -8, -8))) {
      double defaultValue = 0.0;
      if (drawing::sliderDoubleClickDefault(event, &_plugin->plugin,
                                            kParamPlace, &defaultValue)) {
        setParam_value(kParamPlace, defaultValue);
        _dragParam = CLAP_INVALID_ID;
        return;
      }
      _dragParam = kParamPlace;
      updateSlider_point(_dragParam, point);
      return;
    }
    const drawing::Rect planA = fieldPlanPlotRect(fieldARect());
    const drawing::Rect planB = fieldPlanPlotRect(fieldBRect());
    const drawing::Rect elevationA = fieldElevationPlotRect(fieldARect());
    const drawing::Rect elevationB = fieldElevationPlotRect(fieldBRect());
    if (drawing::contains(point, planA) || drawing::contains(point, planB) ||
        drawing::contains(point, elevationA) ||
        drawing::contains(point, elevationB)) {
      _dragField = drawing::contains(point, planA)
                       ? 1
                       : drawing::contains(point, planB)
                             ? 2
                             : drawing::contains(point, elevationA) ? 3 : 4;
      updateMapPosition_field(point, _dragField);
      return;
    }
    for (const auto &layout : sliderLayouts()) {
      if (!drawing::contains(point, drawing::inset(layout.row, -2, -4)))
        continue;
      if (isParameterMenu(layout.id)) {
        _pairMenuOpen = false;
        _pairMenuHover = -1;
        _openParameterMenu = layout.id;
        _parameterMenuHover = -1;
        setNeedsDisplay(true);
        return;
      }
      double defaultValue = 0.0;
      if (drawing::sliderDoubleClickDefault(event, &_plugin->plugin, layout.id,
                                            &defaultValue)) {
        setParam_value(layout.id, defaultValue);
        _dragParam = CLAP_INVALID_ID;
        return;
      }
      _dragParam = layout.id;
      updateSlider_point(_dragParam, point);
      return;
    }
  }
  void mouseDragged(drawing::Event *event) {
    drawing::Point point =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    point.y -= kContentTranslation;
    if (_dragField != 0)
      updateMapPosition_field(point, _dragField);
    else if (_dragParam != CLAP_INVALID_ID)
      updateSlider_point(_dragParam, point);
  }
  void mouseUp(drawing::Event *event) {
    (void)event;
    _dragParam = CLAP_INVALID_ID;
    _dragField = 0;
  }
  void mouseMoved(drawing::Event *event) {
    drawing::Point point =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    point.y -= kContentTranslation;
    if (_pairMenuOpen) {
      _pairMenuHover =
          drawing::dropdownHitIndex(point, pairMenuRect(), 20,
                                    static_cast<uint32_t>(kPairPresets.size()));
      setNeedsDisplay(true);
    } else if (_openParameterMenu != CLAP_INVALID_ID) {
      _parameterMenuHover = drawing::dropdownHitIndex(
          point, parameterDropdownRect(_openParameterMenu), 18.0,
          parameterMenuItemCount(_openParameterMenu));
      setNeedsDisplay(true);
    }
  }
  void mouseExited(drawing::Event *event) {
    (void)event;
    _pairMenuHover = -1;
    _parameterMenuHover = -1;
    setNeedsDisplay(true);
  }
#include "s3g_input_encoder_ray_bilocation_files.inc"
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
