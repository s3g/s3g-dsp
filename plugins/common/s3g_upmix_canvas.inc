// Literal adaptation of S3GFormatUpscaleMapView's active matrix/Tier Rings
// drawing and interactions. Geometry and factory algorithms are kept together
// for comparison with the retained Cocoa reference, not reinterpreted.
namespace upmix_canvas {
using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;
using foundation::Palette;
using Point = CPoint;
struct Size {
  double width = 0., height = 0.;
};
struct Box {
  Point origin{};
  Size size{};
};
constexpr Box makeBox(double x, double y, double w, double h) {
  return {{x, y}, {w, h}};
}
constexpr double maxX(Box b) { return b.origin.x + b.size.width; }
constexpr double maxY(Box b) { return b.origin.y + b.size.height; }
constexpr double midX(Box b) { return b.origin.x + b.size.width * .5; }
constexpr double midY(Box b) { return b.origin.y + b.size.height * .5; }
constexpr Box insetBox(Box b, double x, double y) {
  return makeBox(b.origin.x + x, b.origin.y + y, b.size.width - 2 * x,
                 b.size.height - 2 * y);
}
constexpr bool pointInBox(Point p, Box b) {
  return p.x >= b.origin.x && p.y >= b.origin.y && p.x < maxX(b) &&
         p.y < maxY(b);
}
constexpr bool containsBox(Box a, Box b) {
  return b.origin.x >= a.origin.x && b.origin.y >= a.origin.y &&
         maxX(b) <= maxX(a) && maxY(b) <= maxY(a);
}
constexpr bool intersectsBox(Box a, Box b) {
  return a.origin.x < maxX(b) && b.origin.x < maxX(a) && a.origin.y < maxY(b) &&
         b.origin.y < maxY(a);
}
constexpr Box layoutBox(s3g::gui_layout::Rect b) {
  return makeBox(b.x, b.y, b.width, b.height);
}
CRect canvasBox(Box b) {
  return rect(b.origin.x, b.origin.y, b.size.width, b.size.height);
}
int dropdownHitIndex(Point p, Box b, double height, uint32_t count) {
  if (!count || !pointInBox(p, b))
    return -1;
  return std::clamp(int((p.y - b.origin.y) / height), 0, int(count) - 1);
}
const char *printArgument(const std::string &s) { return s.c_str(); }
template <class T> T printArgument(T t) { return t; }
template <class... Args>
std::string stringFormat(const char *pattern, const Args &... args) {
  return format(pattern, printArgument(args)...);
}
struct Path {
  std::vector<Point> points;
  Box ovalBounds{};
  bool isOval = false, closed = false;
  double width = 1.;
  static Path oval(Box b) {
    Path p;
    p.ovalBounds = b;
    p.isOval = true;
    return p;
  }
  void moveToPoint(Point p) {
    points.clear();
    points.push_back(p);
  }
  void lineToPoint(Point p) { points.push_back(p); }
  void closePath() { closed = true; }
  void setLineWidth(double w) { width = w; }
};
CColor calibratedColor(uint32_t rgb, double alpha = 1.) {
  uint32_t converted = rgb;
  switch (rgb) {
  case 0xff2a1d:
    converted = 0xff4325;
    break;
  case 0xff7f18:
    converted = 0xff931d;
    break;
  case 0xe8d14a:
    converted = 0xedd75b;
    break;
  case 0x65d778:
    converted = 0x74db8b;
    break;
  case 0x2d7f4a:
    converted = 0x378f5c;
    break;
  case 0x111313:
    converted = 0x151818;
    break;
  case 0x171919:
    converted = 0x1e2121;
    break;
  case 0x171a1a:
    converted = 0x1e2223;
    break;
  case 0x394141:
    converted = 0x495252;
    break;
  case 0x3d4444:
    converted = 0x4e5656;
    break;
  case 0x3f4747:
    converted = 0x505959;
    break;
  case 0x54716e:
    converted = 0x668481;
    break;
  case 0x667070:
    converted = 0x798383;
    break;
  case 0x687070:
    converted = 0x7b8383;
    break;
  case 0x6b8f8a:
    converted = 0x7da09c;
    break;
  case 0x6eaaa2:
    converted = 0x7fb7b1;
    break;
  case 0x6fa49d:
    converted = 0x81b2ad;
    break;
  case 0x75aaa3:
    converted = 0x86b7b2;
    break;
  case 0x777f7f:
    converted = 0x8a9191;
    break;
  case 0x79a7a1:
    converted = 0x8ab5b1;
    break;
  case 0x9a7584:
    converted = 0xab8996;
    break;
  case 0xa17386:
    converted = 0xb18898;
    break;
  case 0xaeb7b7:
    converted = 0xbcc4c4;
    break;
  case 0xb8b8b8:
    converted = 0xc5c5c5;
    break;
  case 0xc49255:
    converted = 0xd0a368;
    break;
  case 0xd8d8d8:
    converted = 0xe0e0e0;
    break;
  case 0xe0e0e0:
    converted = 0xe6e6e6;
    break;
  default:
    if (((rgb >> 16) & 255u) == (rgb & 255u) &&
        ((rgb >> 8) & 255u) == (rgb & 255u))
      converted = s3g::portable_gui::topology_colors::gray[rgb & 255u];
    break;
  }
  auto c = color(converted);
  c.alpha = uint8_t(std::clamp(alpha, 0., 1.) * 255.);
  return c;
}
constexpr Box kFormatPanelRect = {{824.0, 86.0}, {260.0, 126.0}};
constexpr Box kAutoPanelRect = {{824.0, 226.0}, {260.0, 165.0}};
constexpr Box kMatrixPanelRect = {{18.0, 48.0}, {1084.0, 654.0}};
constexpr Box kSpatialPanelRect = {{686.0, 126.0}, {416.0, 320.0}};
constexpr Box kRightPanelRect = {{824.0, 226.0}, {260.0, 458.0}};
constexpr Box kSelectedPanelRect = {{824.0, 405.0}, {260.0, 197.0}};
constexpr Box kSpatialMapRect = {{696.0, 156.0}, {396.0, 280.0}};
constexpr Box kMatrixAvailableRect = {{96.0, 160.0}, {700.0, 440.0}};
constexpr Box kLayoutSurfaceRect = {{38.0, 86.0}, {1044.0, 590.0}};

constexpr double kLayoutProjectionHorizontalInset = 42.0;
constexpr double kLayoutProjectionVerticalInset = 42.0;
constexpr double kTierRingDegreeLabelGutter = 34.0;
constexpr double kLayoutNodeHitRadius = 18.0;
constexpr float kTierRingZenithElevation = 89.5f;
constexpr clap_id kUiFactoryPreset = 0xffff0001u;
constexpr uint32_t kCustomMatrixPreset = 0xffffffffu;

enum class MatrixPreset : uint32_t {
  Init = 0u,
  StereoQuadRepeat,
  StereoQuadCross,
  StereoQuadDifference,
  StereoFiveZeroSpread,
  StereoSevenZeroSpread,
  QuadOctophonicRing,
  QuadRing12,
  QuadRing16,
  QuadDoubleRing16,
  OctophonicRing16,
  OctophonicRing32,
  OctophonicDoubleRing16,
  OctophonicDoubleRing32,
};

constexpr uint32_t kMatrixPresetCount = 14u;

const char *matrixPresetName(uint32_t preset) {
  switch (static_cast<MatrixPreset>(preset)) {
  case MatrixPreset::Init:
    return "Init — Stereo → Quad";
  case MatrixPreset::StereoQuadRepeat:
    return "Stereo → Quad Repeat";
  case MatrixPreset::StereoQuadCross:
    return "Stereo → Quad Cross";
  case MatrixPreset::StereoQuadDifference:
    return "Stereo → Quad Difference";
  case MatrixPreset::StereoFiveZeroSpread:
    return "Stereo → 5.0 Spread";
  case MatrixPreset::StereoSevenZeroSpread:
    return "Stereo → 7.0 Spread";
  case MatrixPreset::QuadOctophonicRing:
    return "Quad → Octophonic Ring";
  case MatrixPreset::QuadRing12:
    return "Quad → Ring 12";
  case MatrixPreset::QuadRing16:
    return "Quad → Ring 16";
  case MatrixPreset::QuadDoubleRing16:
    return "Quad → Double Ring 16";
  case MatrixPreset::OctophonicRing16:
    return "Octophonic → Ring 16";
  case MatrixPreset::OctophonicRing32:
    return "Octophonic → Ring 32";
  case MatrixPreset::OctophonicDoubleRing16:
    return "Octophonic → Double Ring 16";
  case MatrixPreset::OctophonicDoubleRing32:
    return "Octophonic → Double Ring 32";
  default:
    return "Custom / Current";
  }
}

constexpr uint32_t kRecipeCount = 4u;

const char *recipeName(uint32_t recipe) {
  constexpr const char *names[kRecipeCount]{"Repeat", "Spatial Match", "Cross",
                                            "All Tiers"};
  return names[std::min<uint32_t>(recipe, kRecipeCount - 1u)];
}

s3g::FormatUpscalePlacement recipePlacement(uint32_t recipe) {
  constexpr s3g::FormatUpscalePlacement placements[kRecipeCount]{
      s3g::FormatUpscalePlacement::Interleave,
      s3g::FormatUpscalePlacement::SameSide,
      s3g::FormatUpscalePlacement::Cross,
      s3g::FormatUpscalePlacement::TierFill,
  };
  return placements[std::min<uint32_t>(recipe, kRecipeCount - 1u)];
}

uint32_t recipeIndex(s3g::FormatUpscalePlacement placement) {
  for (uint32_t recipe = 0u; recipe < kRecipeCount; ++recipe)
    if (recipePlacement(recipe) == placement)
      return recipe;
  return placement == s3g::FormatUpscalePlacement::Cross ? 2u : 1u;
}

Box layoutProjectionRect(bool output) {
  constexpr double outerMargin = 24.0;
  constexpr double mapGap = 96.0;
  constexpr double mapWidth =
      (kLayoutSurfaceRect.size.width - outerMargin * 2.0 - mapGap) * 0.5;
  const double left = kLayoutSurfaceRect.origin.x + outerMargin +
                      (output ? mapWidth + mapGap : 0.0);
  return makeBox(left, kLayoutSurfaceRect.origin.y + 112.0, mapWidth,
                 kLayoutSurfaceRect.size.height - 166.0);
}

Point layoutProjectionPoint(Box map, float azimuthDeg, float elevationDeg) {
  const double xSpan = std::max<double>(
      1.0, map.size.width - kLayoutProjectionHorizontalInset * 2.0);
  const double ySpan = std::max<double>(
      1.0, map.size.height - kLayoutProjectionVerticalInset * 2.0);
  const double azimuth = std::clamp<double>(azimuthDeg, -180.0, 180.0);
  const double elevation = std::clamp<double>(elevationDeg, -90.0, 90.0);
  return Point(map.origin.x + kLayoutProjectionHorizontalInset +
                   (180.0 - azimuth) / 360.0 * xSpan,
               map.origin.y + kLayoutProjectionVerticalInset +
                   (90.0 - elevation) / 180.0 * ySpan);
}

void layoutNodePoints(const s3g::FormatUpscaleLayoutData &layout, Box map,
                      bool origami, std::array<Point, kChannels> &points) {
  if (!origami) {
    for (uint32_t index = 0u; index < layout.count; ++index)
      points[index] =
          layoutProjectionPoint(map, layout.speakers[index].azimuthDeg,
                                layout.speakers[index].elevationDeg);
    return;
  }

  std::array<float, kChannels> tierHeights{};
  uint32_t tierCount = 0u;
  for (uint32_t index = 0u; index < layout.count; ++index) {
    const float height =
        s3g::formatUpscaleSpeakerHeight(layout.speakers[index]);
    uint32_t tier = 0u;
    while (tier < tierCount && std::abs(tierHeights[tier] - height) >=
                                   s3g::kFormatUpscaleTierHeightTolerance)
      ++tier;
    if (tier == tierCount)
      tierHeights[tierCount++] = height;
  }
  std::sort(tierHeights.begin(), tierHeights.begin() + tierCount,
            [](float a, float b) { return a < b; });
  std::array<float, kChannels> tierHorizontalExtents{};
  for (uint32_t index = 0u; index < layout.count; ++index) {
    const float height =
        s3g::formatUpscaleSpeakerHeight(layout.speakers[index]);
    uint32_t tier = 0u;
    while (tier + 1u < tierCount && std::abs(tierHeights[tier] - height) >=
                                        s3g::kFormatUpscaleTierHeightTolerance)
      ++tier;
    const auto direction = s3g::directionFromAed(
        layout.speakers[index].azimuthDeg, layout.speakers[index].elevationDeg);
    const float horizontal =
        std::max(std::abs(direction.x), std::abs(direction.y)) *
        layout.speakers[index].distance;
    tierHorizontalExtents[tier] =
        std::max(tierHorizontalExtents[tier], horizontal);
  }
  const Point center = Point(midX(map), midY(map));
  const double maximumExtent = std::max<double>(
      24.0, std::min(map.size.width, map.size.height) * 0.5 - 38.0);
  const double innerExtent = maximumExtent * 0.28;
  const double outerExtent = std::max<double>(
      innerExtent + 16.0, maximumExtent - kTierRingDegreeLabelGutter);
  for (uint32_t index = 0u; index < layout.count; ++index) {
    const float height =
        s3g::formatUpscaleSpeakerHeight(layout.speakers[index]);
    uint32_t tier = 0u;
    while (tier + 1u < tierCount && std::abs(tierHeights[tier] - height) >=
                                        s3g::kFormatUpscaleTierHeightTolerance)
      ++tier;
    const double planExtent =
        tierCount <= 1u ? maximumExtent * 0.70
                        : outerExtent - (outerExtent - innerExtent) *
                                            static_cast<double>(tier) /
                                            static_cast<double>(tierCount - 1u);
    const auto direction = s3g::directionFromAed(
        layout.speakers[index].azimuthDeg, layout.speakers[index].elevationDeg);
    const double horizontalExtent =
        std::max<double>(0.000001, tierHorizontalExtents[tier]);
    const double scale = planExtent / horizontalExtent;
    const double roomX = direction.x * layout.speakers[index].distance;
    const double roomY = direction.y * layout.speakers[index].distance;

    const bool zenith =
        layout.speakers[index].elevationDeg >= kTierRingZenithElevation;
    points[index] = Point(center.x - (zenith ? 0.0 : roomY * scale),
                          center.y - (zenith ? 0.0 : roomX * scale));
  }
}

constexpr Box kInputMapPanelRect = kSpatialPanelRect;
constexpr Box kOutputMapPanelRect = kSpatialPanelRect;
constexpr Box kEquationPanelRect = kRightPanelRect;
constexpr Box kMethodPanelRect = kMatrixPanelRect;
constexpr s3g::gui_layout::Panel kDistributionPanel{
    s3g::gui_layout::PluginClass::OutputUtility,
    s3g::gui_layout::PanelRole::Routing,
    {18.0, 48.0, 1084.0, 654.0},
    36.0,
    30.0,
    4u};
constexpr s3g::gui_layout::Panel kExtensionPanel{
    s3g::gui_layout::PluginClass::OutputUtility,
    s3g::gui_layout::PanelRole::Output,
    {824.0, 616.0, 260.0, 68.0},
    30.0,
    25.0,
    1u};

struct FormatUpscaleMatrixGeometry {
  Box grid = Box{};
  double cell = 1.0;
};

FormatUpscaleMatrixGeometry matrixGeometry(uint32_t inputCount,
                                           uint32_t outputCount) {
  inputCount = std::max<uint32_t>(1u, inputCount);
  outputCount = std::max<uint32_t>(1u, outputCount);
  const double cell = std::max<double>(
      1.0,
      std::min<double>({
          52.0,
          kMatrixAvailableRect.size.width / static_cast<double>(outputCount),
          kMatrixAvailableRect.size.height / static_cast<double>(inputCount),
      }));
  const double width = cell * static_cast<double>(outputCount);
  const double height = cell * static_cast<double>(inputCount);
  return {
      makeBox(midX(kMatrixAvailableRect) - width * 0.5,
              midY(kMatrixAvailableRect) - height * 0.5, width, height),
      cell,
  };
}

bool matrixCellAtPoint(Point point, uint32_t inputCount, uint32_t outputCount,
                       uint32_t &input, uint32_t &output) {
  const auto geometry = matrixGeometry(inputCount, outputCount);
  if (!pointInBox(point, geometry.grid))
    return false;
  output = std::min<uint32_t>(
      outputCount - 1u,
      static_cast<uint32_t>((point.x - geometry.grid.origin.x) /
                            geometry.cell));
  input = std::min<uint32_t>(
      inputCount - 1u, static_cast<uint32_t>(
                           (point.y - geometry.grid.origin.y) / geometry.cell));
  return true;
}

Box cocoaPanelRect(const s3g::gui_layout::Panel &panel) {
  return layoutBox(panel.frame);
}

Box panelMapRect(Box panel) {
  return makeBox(panel.origin.x + 10.0, panel.origin.y + 64.0,
                 panel.size.width - 20.0, panel.size.height - 72.0);
}

Box formatRowRect(Box panel) {
  return makeBox(panel.origin.x, panel.origin.y + 25.0, panel.size.width, 31.0);
}

Box compactFormatRowRect(bool output) {
  return makeBox(kFormatPanelRect.origin.x + 12.0,
                 kFormatPanelRect.origin.y + 27.0 + (output ? 49.0 : 0.0),
                 kFormatPanelRect.size.width - 24.0, 42.0);
}

Box sideFormatValueRect(bool output) {
  const Box row = compactFormatRowRect(output);
  return makeBox(row.origin.x, row.origin.y + 17.0, row.size.width, 19.0);
}

Box sideAutoModeRowRect() {
  return makeBox(kAutoPanelRect.origin.x + 12.0, kAutoPanelRect.origin.y + 27.0,
                 kAutoPanelRect.size.width - 24.0, 42.0);
}

Box sideAutoModeValueRect() {
  const Box row = sideAutoModeRowRect();
  return makeBox(row.origin.x, row.origin.y + 17.0, row.size.width, 19.0);
}

Box autoCopiesRowRect() {
  return makeBox(kAutoPanelRect.origin.x, kAutoPanelRect.origin.y + 68.0,
                 kAutoPanelRect.size.width, 31.0);
}

Box autoShapeRowRect() {
  return makeBox(kAutoPanelRect.origin.x + 12.0,
                 kAutoPanelRect.origin.y + 116.0,
                 kAutoPanelRect.size.width - 24.0, 42.0);
}

Box autoShapeValueRect() {
  const Box row = autoShapeRowRect();
  return makeBox(row.origin.x, row.origin.y + 17.0, row.size.width, 19.0);
}

Box spatialButtonRect(uint32_t button) {
  constexpr double gap = 4.0;
  constexpr double widths[5]{22.0, 22.0, 62.0, 61.0, 67.0};
  double right = maxX(kSpatialPanelRect) - 7.0;
  for (int index = 4; index >= 0; --index) {
    const double left = right - widths[index];
    if (button == static_cast<uint32_t>(index))
      return makeBox(left, kSpatialPanelRect.origin.y + 3.0, widths[index],
                     15.0);
    right = left - gap;
  }
  return Box{};
}

Box viewSelectorRect(uint32_t view) {
  constexpr double width = 96.0;
  constexpr double gap = 5.0;
  const double right = maxX(kMatrixPanelRect) - 7.0;
  return makeBox(right - width -
                     static_cast<double>(1u - std::min<uint32_t>(view, 1u)) *
                         (width + gap),
                 kMatrixPanelRect.origin.y + 3.0, width, 15.0);
}

Box formatEditButtonRect(bool output) {
  constexpr double width = 70.0;
  constexpr double gap = 4.0;
  const double right = maxX(kFormatPanelRect) - 7.0;
  return makeBox(right - width - (output ? 0.0 : width + gap),
                 kFormatPanelRect.origin.y + 3.0, width, 15.0);
}

Box sideAutoActionRect(uint32_t action) {
  constexpr double widths[2]{70.0, 54.0};
  constexpr double gap = 4.0;
  const double right = maxX(kAutoPanelRect) - 7.0;
  const double clearLeft = right - widths[1];
  const double left = action == 0u ? clearLeft - gap - widths[0] : clearLeft;
  return makeBox(left, kAutoPanelRect.origin.y + 3.0, widths[action], 17.0);
}

Box sideNormalizationRect(uint32_t normalization) {
  constexpr double widths[3]{58.0, 58.0, 67.0};
  constexpr double gap = 3.0;
  double x = kSelectedPanelRect.origin.x + 59.0;
  for (uint32_t index = 0u; index < normalization; ++index)
    x += widths[index] + gap;
  return makeBox(x, kSelectedPanelRect.origin.y + 158.0, widths[normalization],
                 17.0);
}

Box layoutPopupActionRect() {
  constexpr double width = 86.0;
  return makeBox(maxX(kLayoutSurfaceRect) - width - 10.0,
                 kLayoutSurfaceRect.origin.y + 9.0, width, 17.0);
}

Box mapVisualRect(Box panel) {
  const Box map = panelMapRect(panel);
  return makeBox(map.origin.x, map.origin.y, map.size.width,
                 map.size.height - 29.0);
}

Box mapViewButtonRect(Box panel, uint32_t button) {
  constexpr double gap = 4.0;
  constexpr double smallWidth = 23.0;
  constexpr double centerWidth = 68.0;
  constexpr double designWidth = 52.0;
  const double right = maxX(panel) - 8.0;
  if (button == 2u)
    return makeBox(right - centerWidth, panel.origin.y + 3.0, centerWidth,
                   15.0);
  const double centerLeft = right - centerWidth;
  if (button == 3u)
    return makeBox(centerLeft - gap - smallWidth * 2.0 - gap * 2.0 -
                       designWidth,
                   panel.origin.y + 3.0, designWidth, 15.0);
  const double x = centerLeft - gap - smallWidth -
                   static_cast<double>(1u - button) * (smallWidth + gap);
  return makeBox(x, panel.origin.y + 3.0, smallWidth, 15.0);
}

Box customEditorColumnRect(uint32_t column) {
  return makeBox(kRightPanelRect.origin.x,
                 kRightPanelRect.origin.y + 22.0 +
                     static_cast<double>(column) * 35.0,
                 kRightPanelRect.size.width, 31.0);
}

Box customEditorSliderHitRect(uint32_t column) {
  const Box row = customEditorColumnRect(column);
  return makeBox(
      static_cast<double>(s3g::gui_layout::processorControlX(row.origin.x)),
      row.origin.y + 3.0,
      static_cast<double>(s3g::gui_layout::processorTrackWidth(row.size.width)),
      20.0);
}

Box customEditorFieldRect(uint32_t column) {
  const Box row = customEditorColumnRect(column);
  return makeBox(maxX(row) - 57.0, row.origin.y + 4.0, 54.0, 17.0);
}

Box customEditorActionRect(uint32_t action) {
  constexpr double widths[3]{70.0, 108.0, 56.0};
  constexpr double gap = 5.0;
  double x = kRightPanelRect.origin.x + 8.0;
  for (uint32_t index = 0u; index < action; ++index)
    x += widths[index] + gap;
  return makeBox(x, maxY(kRightPanelRect) - 24.0, widths[action], 17.0);
}

Box connectionActionRect(uint32_t action) {
  constexpr double width = 132.0;
  constexpr double gap = 8.0;
  const double right = maxX(kMethodPanelRect) - 10.0;
  return makeBox(right - width -
                     static_cast<double>(1u - action) * (width + gap),
                 kMethodPanelRect.origin.y + 27.0, width, 23.0);
}

Box methodColumnRect(uint32_t column) {
  constexpr double gap = 12.0;
  constexpr double inset = 8.0;
  const double width =
      (kMethodPanelRect.size.width - inset * 2.0 - gap * 2.0) / 3.0;
  return makeBox(kMethodPanelRect.origin.x + inset +
                     static_cast<double>(column) * (width + gap),
                 kMethodPanelRect.origin.y + 25.0, width, 31.0);
}

Box panelSliderRow(const s3g::gui_layout::Panel &panel, uint32_t row) {
  return makeBox(panel.frame.x, s3g::gui_layout::rowY(panel, row) - 5.0,
                 panel.frame.width, 26.0);
}

Box valueRect(Box row) {
  return makeBox(
      static_cast<double>(s3g::gui_layout::processorControlX(row.origin.x)),
      row.origin.y + 4.0,
      static_cast<double>(s3g::gui_layout::processorMenuWidth(row.size.width)),
      17.0);
}

uint32_t menuCountForParam(clap_id id) {
  if (id == kParamInputLayout || id == kParamOutputLayout)
    return s3g::kFormatUpscaleLayoutCount;
  if (id == kUiFactoryPreset)
    return kMatrixPresetCount;
  if (id == kParamPlacement)
    return kRecipeCount;
  if (id == kParamAutoRowShape)
    return 4u;
  if (id == kParamBasis || id == kParamOrigin)
    return 3u;
  return 0u;
}

Box menuRowForParam(clap_id id) {
  switch (id) {
  case kParamInputLayout:
    return compactFormatRowRect(false);
  case kParamOutputLayout:
    return compactFormatRowRect(true);
  case kUiFactoryPreset: {
    constexpr s3g::gui_layout::Canvas canvas{static_cast<double>(kGuiWidth),
                                             static_cast<double>(kGuiHeight)};
    return layoutBox(
        s3g::gui_layout::outputUtilityTitleBand(canvas).presetMenu);
  }
  case kParamAutoRowShape:
    return autoShapeRowRect();
  case kParamOrigin:
    return methodColumnRect(0u);
  case kParamBasis:
    return methodColumnRect(1u);
  case kParamPlacement:
    return sideAutoModeRowRect();
  default:
    return Box{};
  }
}

Box menuValueRectForParam(clap_id id) {
  if (id == kParamInputLayout)
    return sideFormatValueRect(false);
  if (id == kParamOutputLayout)
    return sideFormatValueRect(true);
  if (id == kUiFactoryPreset)
    return menuRowForParam(id);
  if (id == kParamPlacement)
    return sideAutoModeValueRect();
  if (id == kParamAutoRowShape)
    return autoShapeValueRect();
  return valueRect(menuRowForParam(id));
}

struct FormatMenuGeometry {
  Box first = Box{};
  Box second = Box{};
  uint32_t firstCount = 0u;
  uint32_t secondCount = 0u;
  double itemHeight = 20.0;
};

FormatMenuGeometry formatMenuGeometry(clap_id id) {
  const uint32_t count = menuCountForParam(id);
  if (id == kUiFactoryPreset) {
    const Box anchor = menuRowForParam(id);
    constexpr double width = 310.0;
    constexpr double itemHeight = 20.0;
    return {
        makeBox(anchor.origin.x, maxY(anchor) + 4.0, width,
                itemHeight * static_cast<double>(count)),
        Box{},
        count,
        0u,
        itemHeight,
    };
  }
  const bool format = id == kParamInputLayout || id == kParamOutputLayout;
  const uint32_t firstCount = format ? (count + 1u) / 2u : count;
  const uint32_t secondCount = format ? count - firstCount : 0u;
  const Box row = menuRowForParam(id);
  const double columnWidth = format ? 230.0 : row.size.width;
  const double left = format
                          ? kFormatPanelRect.origin.x - 8.0 - columnWidth * 2.0
                          : row.origin.x - columnWidth - 8.0;
  const double top = format ? kFormatPanelRect.origin.y : row.origin.y;
  return {
      makeBox(left, top, columnWidth, 20.0 * static_cast<double>(firstCount)),
      makeBox(left + columnWidth, top, columnWidth,
              20.0 * static_cast<double>(secondCount)),
      firstCount,
      secondCount,
      20.0,
  };
}

Box selectedWeightRowRect() {
  return makeBox(kSelectedPanelRect.origin.x,
                 kSelectedPanelRect.origin.y + 79.0,
                 kSelectedPanelRect.size.width, 31.0);
}

Box selectedWeightHitRect() {
  const Box row = selectedWeightRowRect();
  return makeBox(
      static_cast<double>(s3g::gui_layout::processorControlX(row.origin.x)),
      row.origin.y + 3.0,
      static_cast<double>(s3g::gui_layout::processorTrackWidth(row.size.width)),
      20.0);
}

Box crosspointActionRect(uint32_t action) {
  (void)action;
  return makeBox(kSelectedPanelRect.origin.x + 12.0,
                 kSelectedPanelRect.origin.y + 122.0,
                 kSelectedPanelRect.size.width - 24.0, 17.0);
}

Box levelActionRect(uint32_t action) {
  constexpr double width = 112.0;
  constexpr double gap = 6.0;
  return makeBox(kSelectedPanelRect.origin.x + 12.0 +
                     static_cast<double>(action) * (width + gap),
                 kSelectedPanelRect.origin.y + 157.0, width, 17.0);
}

Box rowShapeButtonRect(uint32_t action) {
  constexpr double width = 55.0;
  constexpr double gap = 4.0;
  return makeBox(kSelectedPanelRect.origin.x + 12.0 +
                     static_cast<double>(action) * (width + gap),
                 kSelectedPanelRect.origin.y + 205.0, width, 17.0);
}

Box shapeAxisButtonRect(bool column) {
  return column ? makeBox(kSelectedPanelRect.origin.x + 145.0,
                          kSelectedPanelRect.origin.y + 181.0, 103.0, 17.0)
                : makeBox(kSelectedPanelRect.origin.x + 61.0,
                          kSelectedPanelRect.origin.y + 181.0, 80.0, 17.0);
}

Box sliderRowForParam(clap_id id) {
  switch (id) {
  case kParamAmount:
    return panelSliderRow(kDistributionPanel, 0u);
  case kParamCopies:
    return autoCopiesRowRect();
  case kParamRotation:
    return panelSliderRow(kDistributionPanel, 2u);
  case kParamSpread:
    return panelSliderRow(kDistributionPanel, 3u);
  case kParamOutputGain:
    return panelSliderRow(kExtensionPanel, 0u);
  default:
    return Box{};
  }
}

Box sliderHitRectForParam(clap_id id) {
  switch (id) {
  case kParamAmount:
    return layoutBox(s3g::gui_layout::sliderHitRect(kDistributionPanel, 0u));
  case kParamCopies: {
    const Box row = autoCopiesRowRect();
    return makeBox(
        static_cast<double>(s3g::gui_layout::processorControlX(row.origin.x)),
        row.origin.y + 3.0,
        static_cast<double>(
            s3g::gui_layout::processorTrackWidth(row.size.width)),
        20.0);
  }
  case kParamRotation:
    return layoutBox(s3g::gui_layout::sliderHitRect(kDistributionPanel, 2u));
  case kParamSpread:
    return layoutBox(s3g::gui_layout::sliderHitRect(kDistributionPanel, 3u));
  case kParamOutputGain:
    return layoutBox(s3g::gui_layout::sliderHitRect(kExtensionPanel, 0u));
  default:
    return Box{};
  }
}

class Editor : public View {
public:
  Plugin &instance;
  Plugin *_plugin;
  clap_id _openMenu = CLAP_INVALID_ID, _dragParam = CLAP_INVALID_ID;
  int _hoverMenuItem = -1;
  uint32_t _selectedInput = 0, _selectedOutput = 0, _page = 0,
           _matrixPreset = 0;
  uint32_t _lastLayoutInputLabelCount = 0, _lastLayoutOutputLabelCount = 0;
  bool _selectionIsOutput = false, _layoutOrigami = true,
       _layoutPopupChild = false;
  bool _matrixPainting = false, _matrixPaintConnected = false,
       _matrixWeightAdjusting = false;
  bool _dragWeight = false, _shapeColumn = false;
  int _matrixPaintInput = -1, _matrixPaintOutput = -1, _designMap = -1,
      _dragCustom = -1;
  Point _matrixDragOrigin{};
  float _matrixDragStartWeight = 0.;
  Editor *_layoutPopupOwner = nullptr;
  Editor *_layoutPopupView = nullptr;
  std::unique_ptr<foundation::AuxiliaryWindow> _layoutPanel;
  bool dockRequested = false;
  char _titlePresetName[256] = "INIT";
  std::string fileError;
  using Kind = s3g::clap_gui::ParamEventKind;
  clap_id active = CLAP_INVALID_ID;
  CColor fillInk = color(0), strokeInk = color(0);
  explicit Editor(Plugin &p, Editor *owner = nullptr)
      : View(kGuiWidth, kGuiHeight), instance(p), _plugin(p.monitorModel.get()),
        _layoutPopupChild(owner != nullptr), _layoutPopupOwner(owner) {
    if (owner)
      _page = 1;
    service();
  }
  ~Editor() override {
    stopRefresh();
    _layoutPanel.reset();
  }
  double refreshPeriodMilliseconds() const override { return 1000. / 30.; }
  void stopRefresh() override {
    View::stopRefresh();
    endGesture();
    _matrixPainting = _matrixWeightAdjusting = _dragWeight = false;
    _dragParam = CLAP_INVALID_ID;
    _dragCustom = -1;
    _openMenu = CLAP_INVALID_ID;
    _hoverMenuItem = -1;
    if (_layoutPanel)
      _layoutPanel->hide();
  }
  void service() override {
    syncUpmixModel(instance);
    if (_layoutPopupOwner) {
      _selectedInput = _layoutPopupOwner->_selectedInput;
      _selectedOutput = _layoutPopupOwner->_selectedOutput;
      _selectionIsOutput = _layoutPopupOwner->_selectionIsOutput;
      _layoutOrigami = _layoutPopupOwner->_layoutOrigami;
      _shapeColumn = _selectionIsOutput;
    }
    if (dockRequested) {
      dockRequested = false;
      if (_layoutPanel)
        _layoutPanel->hide();
      _page = 1;
      _designMap = -1;
      _openMenu = CLAP_INVALID_ID;
      updateCustomValueFields();
    }
  }
  void requestService() {
    s3g::clap_gui::requestParamEventService(instance.host,
                                            instance.monitorHostParams);
  }
  void endGesture() {
    if (active == CLAP_INVALID_ID)
      return;
    instance.monitorGuiEvents.push({Kind::GestureEnd, active, 0.});
    active = CLAP_INVALID_ID;
    requestService();
  }
  bool commitEdit(const SavedState &before,
                  const std::array<double, kParamCount> &previous) {
    std::array<s3g::clap_gui::ParamEvent, kParamCount * 3 + 1> events{};
    uint32_t count = 0;
    clap_id nextActive = active;
    for (clap_id id = 1; id <= kParamCount; ++id) {
      const double next = getParamValue(*_plugin, id);
      if (next == previous[id - 1])
        continue;
      if (id == _dragParam) {
        if (nextActive == CLAP_INVALID_ID) {
          events[count++] = {Kind::GestureBegin, id, 0.};
          nextActive = id;
        }
        events[count++] = {Kind::Value, id, next};
      } else {
        events[count++] = {Kind::GestureBegin, id, 0.};
        events[count++] = {Kind::Value, id, next};
        events[count++] = {Kind::GestureEnd, id, 0.};
      }
    }
    if (instance.monitorGuiEvents.available() <
        count + (nextActive != CLAP_INVALID_ID)) {
      restoreUpmixModel(*_plugin, before);
      return false;
    }
    // Only parameters actually edited are published. Unrelated concurrent
    // host automation is left untouched.
    for (uint32_t i = 0; i < count; ++i)
      if (events[i].kind == Kind::Value) {
        const clap_id id = events[i].paramId;
        double expected = previous[id - 1];
        if (!instance.monitorValues[id - 1].compare_exchange_strong(
                expected, events[i].value, std::memory_order_acq_rel)) {
          setParamValue(*_plugin, id, expected, false);
          events[i].value = expected;
        }
      }
    const auto after = captureUpmix(*_plugin);
    const bool routingChanged =
        before.manualRoutesActive != after.manualRoutesActive ||
        before.manualWeights != after.manualWeights ||
        std::memcmp(&before.customInput, &after.customInput,
                    sizeof(before.customInput)) ||
        std::memcmp(&before.customOutput, &after.customOutput,
                    sizeof(before.customOutput));
    if (routingChanged) {
      publishUpmixRouting(instance);
      if (instance.host && instance.monitorHostState &&
          instance.monitorHostState->mark_dirty)
        instance.monitorHostState->mark_dirty(instance.host);
    }
    active = nextActive;
    instance.monitorGuiEvents.pushBatch(events.data(), count);
    if (count || routingChanged)
      requestService();
    return true;
  }
  template <class F> bool edit(F action) {
    syncUpmixModel(instance);
    const auto before = captureUpmix(*_plugin);
    std::array<double, kParamCount> previous{};
    for (clap_id id = 1; id <= kParamCount; ++id)
      previous[id - 1] = getParamValue(*_plugin, id);
    action();
    return commitEdit(before, previous);
  }
  bool pointerDown(MouseDownEvent &event) override {
    endGesture();
    if (event.buttonState.isRight())
      edit([&] { rightMouseDown(event); });
    else if (event.buttonState.isLeft())
      edit([&] { mouseDown(event); });
    else
      return false;
    return true;
  }
  void pointerMove(MouseMoveEvent &event) override {
    if (event.buttonState.isLeft())
      edit([&] { mouseDragged(event); });
    else
      mouseMoved(event);
    invalid();
  }
  void pointerUp(MouseUpEvent &event) override {
    mouseUp(event);
    endGesture();
  }
  void paint() override {
    service();
    drawRect(makeBox(0, 0, kGuiWidth, kGuiHeight));
    if (_page == 0 && _designMap >= 0 && _openMenu == CLAP_INVALID_ID)
      paintNumericFields();
  }
  void setFill(CColor c) { fillInk = c; }
  void setStroke(CColor c) { strokeInk = c; }
  void fillBox(Box b) { View::fill(canvasBox(b), fillInk); }
  void frameBox(Box b) {
    // NSFrameRect paints INSIDE using fill ink, unlike NSBezierPath.
    fillBox(makeBox(b.origin.x, b.origin.y, b.size.width, 1.));
    fillBox(makeBox(b.origin.x, maxY(b) - 1., b.size.width, 1.));
    fillBox(makeBox(b.origin.x, b.origin.y, 1., b.size.height));
    fillBox(makeBox(maxX(b) - 1., b.origin.y, 1., b.size.height));
  }
  void strokeLine(Point a, Point b) { line(a, b, strokeInk); }
  void drawPath(const Path &path, bool filled) {
    if (path.isOval)
      ellipse(canvasBox(path.ovalBounds), filled ? fillInk : strokeInk, filled,
              path.width);
    else {
      auto points = path.points;
      if (path.closed && !points.empty())
        points.push_back(points.front());
      polygon(points, filled ? fillInk : strokeInk, filled, path.width);
    }
  }
  Size measure(const std::string &label, bool) {
    context->setFont(font);
    return {context->getStringWidth(label.c_str()), font->getSize() * 1.2};
  }
  void drawAt(const std::string &label, Point pt, bool values) {
    const auto size = measure(label, values);
    text(label, rect(pt.x, pt.y, size.width + 2., size.height),
         values ? style.value : style.label);
  }
  void drawPanelFrame(double x, double y, double w, double h,
                      const Palette &s) {
    fill(rect(x, y, w, h), s.cell);
    fill(rect(x, y, w, 2.), s.accent);
  }
  void drawPanelHeader(const std::string &title, bool, double x, double y,
                       double w, double h, bool, const Palette &s) {
    fill(rect(x, y, w, h), s.strip);
    fill(rect(x, y, w, 2.), s.accent);
    drawAt(title,
           {x + s3g::gui_layout::kStandardMetrics.headerLabelInset, y + 5.},
           false);
  }
  void drawToolboxHeaderButton(Box b, Box, const std::string &title,
                               bool selected, bool, const Palette &) {
    foundation::drawButton(*context, canvasBox(b), title, font, selected);
  }
  void drawMenu(const std::string &label, const std::string &value, double y,
                bool, bool, const Palette &s, double labelX, double x,
                double w) {
    if (!label.empty())
      drawAt(upper(label), {labelX, y - 2.}, false);
    foundation::drawMenuBox(*context, rect(x, y - 1., w, 15.), upper(value),
                            font, s);
  }
  void drawProcessorSlider(const std::string &label, const std::string &value,
                           double norm, double y, double x, double w, bool,
                           bool, const Palette &s) {
    namespace L = s3g::gui_layout;
    const double control = L::processorControlX(x),
                 track = L::processorTrackWidth(w);
    text(upper(label),
         rect(L::processorLabelX(x), y - 2.,
              control - L::processorLabelX(x) - 7., 15.),
         s.label);
    foundation::drawHorizontalSlider(*context, rect(control, y + 1., track, 9.),
                                     norm, y - 1., 13., s);
    const auto b = rect(L::processorValueX(x, w), y - 2.,
                        L::kStandardMetrics.processorValueWidth, 15.);
    text(foundation::sliderValueTextToFit(*context, value, b.getWidth(), font),
         b, s.value, kRightText);
  }
  void drawDropdownMenu(Box b, double height, const std::string *items,
                        uint32_t count, int selected, int hover, bool,
                        const Palette &s) {
    if (!count)
      return;
    fill(canvasBox(insetBox(b, -2., -2.)), color(0x080808));
    fill(canvasBox(b), color(0x151515));
    stroke(canvasBox(b), color(0x6c6c6c));
    for (uint32_t i = 0; i < count; ++i) {
      const auto row =
          rect(b.origin.x, b.origin.y + i * height, b.size.width, height);
      if (int(i) == hover || int(i) == selected)
        fill(CRect(row).inset(1, 1),
             color(int(i) == hover ? 0x343434 : 0x292929));
      else if (i % 2)
        fill(CRect(row).inset(1, 1), s.strip);
      if (int(i) == hover || int(i) == selected)
        fill(rect(row.left + 2., row.top + 2., 3., row.getHeight() - 4.),
             s.fill);
      if (i)
        line({row.left, row.top}, {row.right, row.top}, color(0x3a3a3a));
      text(upper(items[i]),
           rect(row.left + 9., row.top, row.getWidth() - 14., row.getHeight()),
           s.label);
    }
  }
  void drawOutputUtilityTitleBand(const std::string &title,
                                  const std::string &preset,
                                  const std::string &summary,
                                  const s3g::gui_layout::EncoderTitleBand &band,
                                  const Palette &s) {
    fill(rect(0, 0, kGuiWidth, 40), s.background);
    foundation::drawPluginTitle(
        *context, title,
        rect(band.titleX, 13., band.presetLabelX - band.titleX - 10., 15.),
        titleFont);
    text("PRESET", rect(band.presetLabelX, 13., 60., 15.), s.label);
    foundation::drawMenuBox(*context, canvasBox(layoutBox(band.presetMenu)),
                            upper(preset), font, s);
    foundation::drawButton(*context, canvasBox(layoutBox(band.loadButton)),
                           "LOAD", font);
    foundation::drawButton(*context, canvasBox(layoutBox(band.saveButton)),
                           "SAVE", font);
    const double left = band.saveButton.x + band.saveButton.width + 12.;
    text(fileError.empty() ? summary : fileError,
         rect(left, 13., kGuiWidth - left - band.statusRightInset, 15.),
         s.value, kRightText);
  }
  bool sliderDefault(unsigned clicks, clap_id id, double &result) {
    if (clicks < 2)
      return false;
    const auto *def = findParam(id);
    if (!def)
      return false;
    result = def->defaultValue;
    return true;
  }
  // Readouts are painted from the editing model; an active CTextEdit is
  // deliberately left alone until its normal commit/focus-loss callback.
  void updateCustomValueFields() {}
  void paintNumericFields() {
    auto layout = editableLayout();
    const unsigned selected =
        std::min(editableSelection(), std::max(1u, layout.count) - 1u);
    const auto speaker = layout.speakers[selected];
    const double values[] = {speaker.azimuthDeg, speaker.elevationDeg,
                             speaker.distance};
    for (unsigned i = 0; i < 3; ++i) {
      const auto b = canvasBox(customEditorFieldRect(i + 2));
      fill(b, style.strip);
      text(format(i == 2 ? "%.2f" : "%+.1f", values[i]), b, style.value,
           kCenterText);
      number(b, values[i],
             [this, i, selected, output = _designMap == 1](double value) {
               if (!std::isfinite(value))
                 return;
               edit([&] {
                 auto layout = output ? _plugin->dsp.customOutputLayout()
                                      : _plugin->dsp.customInputLayout();
                 auto &speaker = layout.speakers[std::min(
                     selected, std::max(1u, layout.count) - 1u)];
                 if (i == 0)
                   speaker.azimuthDeg = float(value);
                 if (i == 1)
                   speaker.elevationDeg = float(value);
                 if (i == 2)
                   speaker.distance = float(value);
                 if (output)
                   _plugin->dsp.setCustomOutputLayout(layout);
                 else
                   _plugin->dsp.setCustomInputLayout(layout);
                 applyParams(*_plugin);
                 markMatrixCustom();
               });
             });
    }
  }
  void setDocumentationLayoutOrigami(bool enabled) {
    _layoutOrigami = enabled;
    invalid();
  }
  void openLayoutPopup() {
    if (_layoutPopupChild)
      return;
    if (!_layoutPanel) {
      _layoutPanel = std::make_unique<foundation::AuxiliaryWindow>(
          "s3g MATRIX UPMIX 64 — TIER RINGS", kGuiWidth, kGuiHeight);
      auto *child = new Editor(instance, this);
      if (!_layoutPanel->attach(child)) {
        _layoutPanel.reset();
        return;
      }
      _layoutPopupView = child;
    }
    auto *frame = getFrame();
    void *native = frame && frame->getPlatformFrame()
                       ? frame->getPlatformFrame()->getPlatformRepresentation()
                       : nullptr;
    if (_layoutPanel->show(native)) {
      _page = 0;
      _designMap = -1;
      _openMenu = CLAP_INVALID_ID;
    }
  }
  void dockLayoutPopup() { dockRequested = true; }
  bool presetFile(const std::string &path, bool save) {
    const clap_plugin_state_t codec{encodeUpmixState, decodeUpmixState};
    bool success = false;
    if (save)
      success = s3g::clap_gui::portable::saveStateFile(&_plugin->plugin, codec,
                                                       path.c_str());
    else {
      auto candidate = std::make_unique<Plugin>();
      candidate->plugin.plugin_data = candidate.get();
      restoreUpmixModel(*candidate, captureUpmix(*_plugin));
      success = s3g::clap_gui::portable::loadStateFile(&candidate->plugin,
                                                       codec, path.c_str());
      if (success && finiteUpmixState(captureUpmix(*candidate))) {
        candidate->params.outputGainDb = _plugin->params.outputGainDb;
        restoreUpmixModel(*_plugin, captureUpmix(*candidate));
        _matrixPreset = kCustomMatrixPreset;
      } else
        success = false;
    }
    if (success)
      std::snprintf(
          _titlePresetName, sizeof(_titlePresetName), "%s",
          foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem())
              .c_str());
    return success;
  }
  bool handleTitleClick(Point point) {
    const auto band =
        s3g::gui_layout::outputUtilityTitleBand({kGuiWidth, kGuiHeight});
    const bool save = pointInBox(point, layoutBox(band.saveButton));
    if (!save && !pointInBox(point, layoutBox(band.loadButton)))
      return false;
    foundation::FileDialogOptions options;
    options.save = save;
    options.title =
        save ? "Save Matrix Upmix Preset" : "Load Matrix Upmix Preset";
    options.extension = "s3gpreset";
    options.extensionDescription = "s3g Preset";
    options.defaultSaveName = "Preset.s3gpreset";
    options.initialDirectory = foundation::presetDirectory("Matrix Upmix");
    const auto path = foundation::runFileDialog(getFrame(), options);
    if (!path.empty())
      fileError = presetFile(path, save) ? "" : "PRESET FILE ERROR";
    return true;
  }

  s3g::FormatUpscaleLayoutData editableLayout() {
    if (!_plugin)
      return {};
    return _designMap == 1 ? _plugin->dsp.customOutputLayout()
                           : _plugin->dsp.customInputLayout();
  }

  uint32_t editableSelection() {
    return _designMap == 1 ? _selectedOutput : _selectedInput;
  }

  void markMatrixCustom() {
    _matrixPreset = kCustomMatrixPreset;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CUSTOM");
  }

  void setEditableLayout(const s3g::FormatUpscaleLayoutData &layout) {
    if (!_plugin || _designMap < 0)
      return;
    markMatrixCustom();
    if (_designMap == 1)
      _plugin->dsp.setCustomOutputLayout(layout);
    else
      _plugin->dsp.setCustomInputLayout(layout);
    applyParams(*_plugin);
  }

  void enterDesignMap(int map) {
    if (!_plugin || map < 0 || map > 1)
      return;
    const bool output = map == 1;
    const auto currentLayout =
        output ? _plugin->dsp.outputLayout() : _plugin->dsp.inputLayout();
    const auto currentFormat =
        output ? _plugin->params.outputLayout : _plugin->params.inputLayout;
    if (currentFormat != s3g::FormatUpscaleLayout::Custom) {
      if (output)
        _plugin->dsp.setCustomOutputLayout(currentLayout);
      else
        _plugin->dsp.setCustomInputLayout(currentLayout);
    }
    _designMap = map;
    setParamValue(*_plugin, output ? kParamOutputLayout : kParamInputLayout,
                  static_cast<double>(
                      static_cast<uint32_t>(s3g::FormatUpscaleLayout::Custom)));
    updateCustomValueFields();
  }

  void loadDocumentationCube41Layout() {
    if (!_plugin)
      return;
    _plugin->params.inputLayout = s3g::FormatUpscaleLayout::Stereo;
    _plugin->params.outputLayout = s3g::FormatUpscaleLayout::Cube41;
    _plugin->params.basis = s3g::FormatUpscaleBasis::Direct;
    _plugin->params.placement = s3g::FormatUpscalePlacement::Match;
    _plugin->params.origin = s3g::FormatUpscaleOrigin::Share;
    _plugin->params.copies = 1u;
    applyParams(*_plugin);
    _plugin->dsp.useAutomaticRoutes();
    _page = 1u;
    _designMap = -1;
    _selectedInput = 0u;
    _selectedOutput = 0u;
    _selectionIsOutput = false;
    _matrixPreset = kCustomMatrixPreset;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                  "CUBE 41 CHECK");
    updateCustomValueFields();
    invalid();
  }

  void drawPanel(Box rect, std::string title, bool attrs,
                 const Palette &style) {
    drawPanelFrame(rect.origin.x, rect.origin.y, rect.size.width,
                   rect.size.height, style);
    drawPanelHeader(title, true, rect.origin.x, rect.origin.y, rect.size.width,
                    21.0, attrs, style);
  }

  void drawSideFormatControl(clap_id param, bool output, bool attrs,
                             bool valueAttrs, const Palette &style) {
    std::string label = output ? "OUT FORMAT" : "IN FORMAT";
    const Box row = compactFormatRowRect(output);
    const Box box = sideFormatValueRect(output);
    drawAt(label, row.origin, attrs);
    std::string value = std::string(
        menuValueName(param, roundedUint(getParamValue(*_plugin, param))));
    drawMenu("", value, box.origin.y + 1.0, attrs, valueAttrs, style,
             box.origin.x, box.origin.x, box.size.width);
  }

  void drawSideAutoModeControl(bool attrs, bool valueAttrs,
                               const Palette &style) {
    const Box row = sideAutoModeRowRect();
    const Box box = sideAutoModeValueRect();
    drawAt("RECIPE", row.origin, attrs);
    std::string value =
        std::string(recipeName(recipeIndex(_plugin->params.placement)));
    drawMenu("", value, box.origin.y + 1.0, attrs, valueAttrs, style,
             box.origin.x, box.origin.x, box.size.width);
    drawToolboxHeaderButton(sideAutoActionRect(0u), kAutoPanelRect, "APPLY",
                            false, attrs, style);
    drawToolboxHeaderButton(sideAutoActionRect(1u), kAutoPanelRect, "CLEAR",
                            false, attrs, style);
    const Box copiesRow = autoCopiesRowRect();
    if (_plugin->params.placement == s3g::FormatUpscalePlacement::TierFill) {
      std::string coverage =
          stringFormat("ALL %u", _plugin->dsp.activeOutputs());
      drawAt("COVERAGE (FIXED)",
             Point(copiesRow.origin.x, copiesRow.origin.y + 5.0), attrs);
      const Size coverageSize = measure(coverage, valueAttrs);
      drawAt(coverage,
             Point(maxX(copiesRow) - 12.0 - coverageSize.width,
                   copiesRow.origin.y + 5.0),
             valueAttrs);
      drawAt("EVERY OUTPUT  •  EVERY HEIGHT TIER",
             Point(copiesRow.origin.x + 12.0, copiesRow.origin.y + 25.0),
             attrs);
    } else {
      const uint32_t copies = effectiveAutoFillCopies(*_plugin);
      const uint32_t maximum = autoFillCopyMaximum(*_plugin);
      const double normalized = maximum > 1u
                                    ? static_cast<double>(copies - 1u) /
                                          static_cast<double>(maximum - 1u)
                                    : 0.0;
      std::string label =
          _plugin->params.placement == s3g::FormatUpscalePlacement::Interleave
              ? "REPEAT ROUNDS"
              : "ROUTES / INPUT";
      std::string copyValue;
      if (_plugin->params.placement ==
          s3g::FormatUpscalePlacement::Interleave) {
        const uint32_t covered = std::min<uint32_t>(
            _plugin->dsp.activeOutputs(), copies * _plugin->dsp.activeInputs());
        copyValue = stringFormat("%u×", copies);
        std::string coverage =
            stringFormat("COMPLETE ROUNDS  •  %u OF %u OUTPUTS", covered,
                         _plugin->dsp.activeOutputs());
        drawAt(coverage,
               Point(copiesRow.origin.x + 12.0, copiesRow.origin.y + 25.0),
               attrs);
      } else {
        copyValue = stringFormat("%u EACH", copies);
        std::string target =
            stringFormat("TARGET  •  %u ROUTES PER INPUT", copies);
        drawAt(target,
               Point(copiesRow.origin.x + 12.0, copiesRow.origin.y + 25.0),
               attrs);
      }
      drawProcessorSlider(label, copyValue, normalized,
                          copiesRow.origin.y + 5.0, copiesRow.origin.x,
                          copiesRow.size.width, attrs, valueAttrs, style);
    }
    const Box shapeRow = autoShapeRowRect();
    const Box shapeBox = autoShapeValueRect();
    drawAt("WEIGHT SHAPE", shapeRow.origin, attrs);
    std::string shape =
        std::string(s3g::formatUpscaleRowShapeName(_plugin->autoRowShape));
    drawMenu("", shape, shapeBox.origin.y + 1.0, attrs, valueAttrs, style,
             shapeBox.origin.x, shapeBox.origin.x, shapeBox.size.width);
  }

  void drawSetupActions(bool attrs, const Palette &style) {
    drawToolboxHeaderButton(formatEditButtonRect(false), kFormatPanelRect,
                            "EDIT IN", _designMap == 0, attrs, style);
    drawToolboxHeaderButton(formatEditButtonRect(true), kFormatPanelRect,
                            "EDIT OUT", _designMap == 1, attrs, style);
  }

  void drawSliderControl(clap_id param, Box row, bool attrs, bool valueAttrs,
                         const Palette &style) {
    const ParamDef *def = findParam(param);
    if (!def)
      return;
    const double value = getParamValue(*_plugin, param);
    const double normalized = static_cast<double>(
        (value - def->minimum) / (def->maximum - def->minimum));
    char text[64]{};
    paramsValueToText(nullptr, param, value, text, sizeof(text));
    drawProcessorSlider(std::string(def->label), std::string(text), normalized,
                        row.origin.y + 5.0, row.origin.x, row.size.width, attrs,
                        valueAttrs, style);
  }

  void drawCustomEditor(bool attrs, bool valueAttrs, const Palette &style) {
    if (!_plugin || _designMap < 0)
      return;
    const auto layout = editableLayout();
    const uint32_t selected = std::min<uint32_t>(
        editableSelection(), std::max<uint32_t>(1u, layout.count) - 1u);
    const auto &speaker = layout.speakers[selected];
    const uint32_t countMinimum =
        _designMap == 1 ? std::max<uint32_t>(1u, _plugin->dsp.activeInputs())
                        : 1u;
    const uint32_t countMaximum =
        _designMap == 0
            ? std::max<uint32_t>(countMinimum, _plugin->dsp.activeOutputs())
            : kChannels;
    const double norms[5]{
        countMaximum > countMinimum
            ? static_cast<double>(layout.count - countMinimum) /
                  static_cast<double>(countMaximum - countMinimum)
            : 0.0,
        layout.count > 1u ? static_cast<double>(selected) /
                                static_cast<double>(layout.count - 1u)
                          : 0.0,
        static_cast<double>(s3g::aedAzimuthSliderNorm(speaker.azimuthDeg)),
        static_cast<double>((speaker.elevationDeg + 90.0f) / 180.0f),
        static_cast<double>((speaker.distance - 0.1f) / 2.9f),
    };
    std::string labels[5]{"COUNT", "SELECT", "AZ", "EL", "DST"};
    std::string values[5]{stringFormat("%u", layout.count),
                          stringFormat("%u", selected + 1u), "", "", ""};
    for (uint32_t column = 0u; column < 5u; ++column) {
      const Box row = customEditorColumnRect(column);
      drawProcessorSlider(labels[column], values[column],
                          std::clamp<double>(norms[column], 0.0, 1.0),
                          row.origin.y + 5.0, row.origin.x, row.size.width,
                          attrs, valueAttrs, style);
    }
    const auto direction =
        s3g::directionFromAed(speaker.azimuthDeg, speaker.elevationDeg);
    drawAt(stringFormat("AED  A%+.1f  E%+.1f  D%.2f",
                        static_cast<double>(speaker.azimuthDeg),
                        static_cast<double>(speaker.elevationDeg),
                        static_cast<double>(speaker.distance)),
           Point(kRightPanelRect.origin.x + 12.0,
                 kRightPanelRect.origin.y + 215.0),
           valueAttrs);
    drawAt(stringFormat("XYZ  %+.2f  %+.2f  %+.2f",
                        static_cast<double>(direction.x * speaker.distance),
                        static_cast<double>(direction.y * speaker.distance),
                        static_cast<double>(direction.z * speaker.distance)),
           Point(kRightPanelRect.origin.x + 12.0,
                 kRightPanelRect.origin.y + 237.0),
           valueAttrs);
    drawAt("CHANNEL ORDER = LIST ORDER",
           Point(kRightPanelRect.origin.x + 12.0,
                 kRightPanelRect.origin.y + 269.0),
           attrs);
    drawToolboxHeaderButton(customEditorActionRect(0u), kEquationPanelRect,
                            _designMap == 0 ? "FRONT 3" : "3 × 3 TIERS", false,
                            valueAttrs, style);
    drawToolboxHeaderButton(customEditorActionRect(1u), kEquationPanelRect,
                            "COPY OTHER MAP", false, valueAttrs, style);
    drawToolboxHeaderButton(customEditorActionRect(2u), kEquationPanelRect,
                            "DONE", true, valueAttrs, style);
    updateCustomValueFields();
  }

  void drawMatrixEditor(bool attrs, bool valueAttrs, const Palette &style) {
    const auto &inputLayout = _plugin->dsp.inputLayout();
    const auto &outputLayout = _plugin->dsp.outputLayout();
    const uint32_t inputCount = std::max<uint32_t>(1u, inputLayout.count);
    const uint32_t outputCount = std::max<uint32_t>(1u, outputLayout.count);
    _selectedInput = std::min<uint32_t>(_selectedInput, inputCount - 1u);
    _selectedOutput = std::min<uint32_t>(_selectedOutput, outputCount - 1u);
    const auto geometry = matrixGeometry(inputCount, outputCount);

    setFill(calibratedColor(0x111313));
    fillBox(insetBox(geometry.grid, -1.0, -1.0));

    setStroke(calibratedColor(0x3d4444, geometry.cell < 8.0 ? 0.35 : 0.70));
    for (uint32_t output = 0u; output <= outputCount; ++output) {
      const double x =
          geometry.grid.origin.x + static_cast<double>(output) * geometry.cell;
      strokeLine(Point(x, geometry.grid.origin.y),
                 Point(x, maxY(geometry.grid)));
    }
    for (uint32_t input = 0u; input <= inputCount; ++input) {
      const double y =
          geometry.grid.origin.y + static_cast<double>(input) * geometry.cell;
      strokeLine(Point(geometry.grid.origin.x, y),
                 Point(maxX(geometry.grid), y));
    }

    const uint32_t outputLabelStride = std::max<uint32_t>(
        1u, static_cast<uint32_t>(std::ceil(28.0 / geometry.cell)));
    const uint32_t inputLabelStride = std::max<uint32_t>(
        1u, static_cast<uint32_t>(std::ceil(15.0 / geometry.cell)));
    drawAt("OUTPUTS →",
           Point(geometry.grid.origin.x, geometry.grid.origin.y - 83.0), attrs);
    drawAt("INPUTS ↓",
           Point(geometry.grid.origin.x - 78.0, geometry.grid.origin.y - 83.0),
           attrs);

    uint32_t tierStart = 0u;
    while (tierStart < outputCount) {
      uint32_t tierEnd = tierStart + 1u;
      const float elevation = outputLayout.speakers[tierStart].elevationDeg;
      while (tierEnd < outputCount &&
             std::abs(outputLayout.speakers[tierEnd].elevationDeg - elevation) <
                 0.5f)
        ++tierEnd;
      const double left = geometry.grid.origin.x +
                          static_cast<double>(tierStart) * geometry.cell + 2.0;
      const double right = geometry.grid.origin.x +
                           static_cast<double>(tierEnd) * geometry.cell - 2.0;
      const double y = geometry.grid.origin.y - 55.0;
      setStroke(calibratedColor(0x6b8f8a, 0.80));
      strokeLine(Point(left, y), Point(right, y));
      std::string tier =
          stringFormat("E%+.0f° TIER", static_cast<double>(elevation));
      const Size tierSize = measure(tier, valueAttrs);
      if (right - left >= tierSize.width + 8.0)
        drawAt(tier, Point((left + right - tierSize.width) * 0.5, y - 19.0),
               valueAttrs);
      tierStart = tierEnd;
    }
    std::array<float, kChannels> outputPowers{};
    for (uint32_t output = 0u; output < outputCount; ++output) {
      for (uint32_t input = 0u; input < inputCount; ++input) {
        const float gain = _plugin->dsp.targetAnchorGain(input, output) +
                           _plugin->dsp.targetExtensionGain(input, output);
        outputPowers[output] += gain * gain;
      }
      if (outputPowers[output] <= 1.0001f)
        continue;
      const double centerX =
          geometry.grid.origin.x +
          (static_cast<double>(output) + 0.5) * geometry.cell;
      setFill(calibratedColor(0xc49255, 0.95));
      fillBox(makeBox(centerX - std::min<double>(5.0, geometry.cell * 0.3),
                      geometry.grid.origin.y - 9.0,
                      std::min<double>(10.0, geometry.cell * 0.6), 2.0));
    }
    for (uint32_t output = 0u; output < outputCount; ++output) {
      const bool selected = output == _selectedOutput;
      const bool edge = output == 0u || output + 1u == outputCount;
      if (!selected && !edge && output % outputLabelStride != 0u)
        continue;
      if (!edge &&
          static_cast<double>(outputCount - 1u - output) * geometry.cell < 28.0)
        continue;
      if (!selected && std::abs(static_cast<int>(output) -
                                static_cast<int>(_selectedOutput)) *
                               geometry.cell <
                           24.0)
        continue;
      const auto &speaker = outputLayout.speakers[output];
      std::string label = stringFormat("O%u", output + 1u);
      const Size size = measure(label, valueAttrs);
      drawAt(label,
             Point(geometry.grid.origin.x +
                       (static_cast<double>(output) + 0.5) * geometry.cell -
                       size.width * 0.5,
                   geometry.grid.origin.y - 42.0),
             valueAttrs);
      if (geometry.cell >= 38.0) {
        std::string azimuth =
            stringFormat("A%+.0f", static_cast<double>(speaker.azimuthDeg));
        const Size azimuthSize = measure(azimuth, valueAttrs);
        drawAt(azimuth,
               Point(geometry.grid.origin.x +
                         (static_cast<double>(output) + 0.5) * geometry.cell -
                         azimuthSize.width * 0.5,
                     geometry.grid.origin.y - 23.0),
               valueAttrs);
      }
    }
    for (uint32_t input = 0u; input < inputCount; ++input) {
      const bool selected = input == _selectedInput;
      const bool edge = input == 0u || input + 1u == inputCount;
      if (!selected && !edge && input % inputLabelStride != 0u)
        continue;
      if (!selected &&
          std::abs(static_cast<int>(input) - static_cast<int>(_selectedInput)) *
                  geometry.cell <
              14.0)
        continue;
      const auto &speaker = inputLayout.speakers[input];
      const char *role = s3g::formatUpscaleRoleName(speaker.role);
      std::string label = geometry.cell >= 24.0 && role[0]
                              ? stringFormat("I%u %s", input + 1u, role)
                              : stringFormat("I%u", input + 1u);
      std::string aed =
          stringFormat("A%+.0f E%+.0f", static_cast<double>(speaker.azimuthDeg),
                       static_cast<double>(speaker.elevationDeg));
      const Size labelSize = measure(label, valueAttrs);
      const Size aedSize = measure(aed, valueAttrs);
      const double centerY = geometry.grid.origin.y +
                             (static_cast<double>(input) + 0.5) * geometry.cell;
      drawAt(label,
             Point(geometry.grid.origin.x - labelSize.width - 11.0,
                   centerY - (geometry.cell >= 38.0 ? 15.0 : 7.0)),
             valueAttrs);
      if (geometry.cell >= 38.0)
        drawAt(
            aed,
            Point(geometry.grid.origin.x - aedSize.width - 11.0, centerY + 1.0),
            valueAttrs);
    }

    setStroke(calibratedColor(0x79a7a1, 0.95));
    const double selectedOutputX =
        geometry.grid.origin.x +
        (static_cast<double>(_selectedOutput) + 0.5) * geometry.cell;
    strokeLine(Point(selectedOutputX, geometry.grid.origin.y - 5.0),
               Point(selectedOutputX, geometry.grid.origin.y));
    const double selectedInputY =
        geometry.grid.origin.y +
        (static_cast<double>(_selectedInput) + 0.5) * geometry.cell;
    strokeLine(Point(geometry.grid.origin.x - 5.0, selectedInputY),
               Point(geometry.grid.origin.x, selectedInputY));

    uint32_t connectionCount = 0u;
    uint32_t selectedRouteCount = 0u;
    uint32_t selectedContributorCount = 0u;
    float selectedPower = 0.0f;
    float selectedColumnPower = 0.0f;
    bool hasDerivedPolarity = false;
    for (uint32_t input = 0u; input < inputCount; ++input) {
      for (uint32_t output = 0u; output < outputCount; ++output) {
        const float gain = _plugin->dsp.targetAnchorGain(input, output) +
                           _plugin->dsp.targetExtensionGain(input, output);
        const bool connected = std::abs(gain) > 0.0001f;
        if (gain < -0.0001f)
          hasDerivedPolarity = true;
        if (connected)
          ++connectionCount;
        if (input == _selectedInput) {
          if (connected) {
            ++selectedRouteCount;
            selectedPower += gain * gain;
          }
        }
        if (output == _selectedOutput && connected) {
          ++selectedContributorCount;
          selectedColumnPower += gain * gain;
        }
        const Point center =
            Point(geometry.grid.origin.x +
                      (static_cast<double>(output) + 0.5) * geometry.cell,
                  geometry.grid.origin.y +
                      (static_cast<double>(input) + 0.5) * geometry.cell);
        const double inactiveSize =
            std::clamp<double>(geometry.cell * 0.13, 0.8, 4.0);
        if (connected) {
          const float displayWeight =
              _plugin->dsp.manualRoutesActive()
                  ? std::abs(_plugin->dsp.manualWeight(input, output))
                  : std::min(1.0f, std::abs(gain));
          const double minimumActive =
              std::max<double>(1.2, geometry.cell * 0.24);
          const double activeSize = std::clamp<double>(
              geometry.cell * (0.20 + 0.58 * std::sqrt(displayWeight)),
              minimumActive,
              std::max<double>(minimumActive, geometry.cell - 1.5));
          setFill(calibratedColor(gain < 0.0f ? 0x9a7584 : 0x6fa49d, 0.96));
          fillBox(makeBox(center.x - activeSize * 0.5,
                          center.y - activeSize * 0.5, activeSize, activeSize));
        } else if (geometry.cell >= 4.0) {
          setFill(calibratedColor(0x687070, 0.58));
          fillBox(makeBox(center.x - inactiveSize * 0.5,
                          center.y - inactiveSize * 0.5, inactiveSize,
                          inactiveSize));
        }
      }
    }

    const char *inputRole =
        s3g::formatUpscaleRoleName(inputLayout.speakers[_selectedInput].role);
    const char *outputRole =
        s3g::formatUpscaleRoleName(outputLayout.speakers[_selectedOutput].role);
    std::string selection = stringFormat(
        "I%u%s%s → O%u%s%s   •   ROW %u DESTINATION%s   •   COLUMN %u "
        "CONTRIBUTOR%s",
        _selectedInput + 1u, inputRole[0] ? " " : "", inputRole,
        _selectedOutput + 1u, outputRole[0] ? " " : "", outputRole,
        selectedRouteCount, selectedRouteCount == 1u ? "" : "S",
        selectedContributorCount, selectedContributorCount == 1u ? "" : "S");
    const float selectedWeight =
        _plugin->dsp.manualRoutesActive()
            ? _plugin->dsp.manualWeight(_selectedInput, _selectedOutput)
            : _plugin->dsp.targetAnchorGain(_selectedInput, _selectedOutput) +
                  _plugin->dsp.targetExtensionGain(_selectedInput,
                                                   _selectedOutput);
    const float selectedGain =
        _plugin->dsp.targetAnchorGain(_selectedInput, _selectedOutput) +
        _plugin->dsp.targetExtensionGain(_selectedInput, _selectedOutput);
    std::string equation = stringFormat(
        "GAIN %+.0f%%   •   g %+.3f   •   INPUT Σg² %.3f   •   OUTPUT Σg² "
        "%.3f%s",
        static_cast<double>(selectedWeight * 100.0f),
        static_cast<double>(selectedGain), static_cast<double>(selectedPower),
        static_cast<double>(selectedColumnPower),
        selectedColumnPower > 1.0001f ? "   •   OUTPUT OVER" : "");
    if (hasDerivedPolarity) {
      equation = "STEREO DERIVE   S=(L−R)/√2   •   LR=+0.707S   •   RR=−0.707S "
                 "  •   FR / FL = −3 dB DIRECT";
    }
    std::string displayedPreset = std::string(matrixPresetName(_matrixPreset));
    std::string routeMode =
        _plugin->dsp.manualRoutesActive()
            ? (hasDerivedPolarity
                   ? stringFormat(
                         "STEREO DIFFERENCE / %s / PURPLE = DERIVED POLARITY",
                         displayedPreset)
                   : stringFormat("EXACT POSITIVE MATRIX / %s",
                                  displayedPreset))
            : stringFormat(
                  "AUTO: %s / %s / BASE %u TARGET%s",
                  s3g::formatUpscalePlacementName(_plugin->params.placement),
                  s3g::formatUpscaleRowShapeName(_plugin->autoRowShape),
                  _plugin->params.copies,
                  _plugin->params.copies == 1u ? "" : "S");
    std::string weightEdit =
        geometry.cell >= 10.0 ? "DRAG ↑↓ = 0…1" : "GAIN SLIDER";
    std::string mode =
        stringFormat("%s   •   %u CELL%s   •   CLICK ADD / SELECT   •   %s   • "
                     "  RIGHT DELETE",
                     routeMode, connectionCount,
                     connectionCount == 1u ? "" : "S", weightEdit);
    drawAt(
        selection,
        Point(kMatrixPanelRect.origin.x + 14.0, maxY(kMatrixPanelRect) - 92.0),
        valueAttrs);
    drawAt(
        equation,
        Point(kMatrixPanelRect.origin.x + 14.0, maxY(kMatrixPanelRect) - 65.0),
        valueAttrs);
    drawAt(
        mode,
        Point(kMatrixPanelRect.origin.x + 14.0, maxY(kMatrixPanelRect) - 38.0),
        valueAttrs);

    std::string viewNames[2]{"MATRIX", "LAYOUT"};
    for (uint32_t view = 0u; view < 2u; ++view)
      drawToolboxHeaderButton(viewSelectorRect(view), kMatrixPanelRect,
                              viewNames[view], view == 0u, attrs, style);
  }

  void drawConnectionInspector(bool attrs, bool valueAttrs,
                               const Palette &style) {
    const auto &inputLayout = _plugin->dsp.inputLayout();
    const auto &outputLayout = _plugin->dsp.outputLayout();
    _selectedInput = std::min<uint32_t>(
        _selectedInput, std::max<uint32_t>(1u, inputLayout.count) - 1u);
    _selectedOutput = std::min<uint32_t>(
        _selectedOutput, std::max<uint32_t>(1u, outputLayout.count) - 1u);
    const auto &input = inputLayout.speakers[_selectedInput];
    const auto &output = outputLayout.speakers[_selectedOutput];
    const char *inputRole = s3g::formatUpscaleRoleName(input.role);
    const char *outputRole = s3g::formatUpscaleRoleName(output.role);
    std::string inputAed =
        stringFormat("I%u%s%s   A%+.1f° E%+.1f°", _selectedInput + 1u,
                     inputRole[0] ? " " : "", inputRole,
                     static_cast<double>(input.azimuthDeg),
                     static_cast<double>(input.elevationDeg));
    std::string outputAed =
        stringFormat("O%u%s%s   A%+.1f° E%+.1f°", _selectedOutput + 1u,
                     outputRole[0] ? " " : "", outputRole,
                     static_cast<double>(output.azimuthDeg),
                     static_cast<double>(output.elevationDeg));
    drawAt(inputAed,
           Point(kSelectedPanelRect.origin.x + 12.0,
                 kSelectedPanelRect.origin.y + 34.0),
           valueAttrs);
    drawAt(outputAed,
           Point(kSelectedPanelRect.origin.x + 12.0,
                 kSelectedPanelRect.origin.y + 56.0),
           valueAttrs);

    const float weight =
        _plugin->dsp.manualRoutesActive()
            ? _plugin->dsp.manualWeight(_selectedInput, _selectedOutput)
            : _plugin->dsp.targetAnchorGain(_selectedInput, _selectedOutput) +
                  _plugin->dsp.targetExtensionGain(_selectedInput,
                                                   _selectedOutput);
    const bool derivedPolarity = weight < 0.0f;
    std::string weightText =
        derivedPolarity
            ? stringFormat("%.0f%%  DERIVED −",
                           static_cast<double>(std::abs(weight) * 100.0f))
            : stringFormat("%.0f%%", static_cast<double>(weight * 100.0f));
    const Box weightRow = selectedWeightRowRect();
    drawProcessorSlider(derivedPolarity ? "DERIVED GAIN" : "GAIN", weightText,
                        std::abs(weight), weightRow.origin.y + 5.0,
                        weightRow.origin.x, weightRow.size.width, attrs,
                        valueAttrs, style);
    drawToolboxHeaderButton(crosspointActionRect(0u), kSelectedPanelRect,
                            "DELETE", false, attrs, style);
    drawToolboxHeaderButton(levelActionRect(0u), kSelectedPanelRect,
                            "NORMALIZE INPUTS", false, attrs, style);
    drawToolboxHeaderButton(levelActionRect(1u), kSelectedPanelRect,
                            "LIMIT OUTPUTS", false, attrs, style);

    drawSliderControl(kParamOutputGain,
                      upmix_canvas::sliderRowForParam(kParamOutputGain), attrs,
                      valueAttrs, style);
  }

  void drawUnfoldedLayoutPage(bool attrs, bool valueAttrs,
                              const Palette &style) {
    const auto &inputLayout = _plugin->dsp.inputLayout();
    const auto &outputLayout = _plugin->dsp.outputLayout();
    _selectedInput = std::min<uint32_t>(
        _selectedInput, std::max<uint32_t>(1u, inputLayout.count) - 1u);
    _selectedOutput = std::min<uint32_t>(
        _selectedOutput, std::max<uint32_t>(1u, outputLayout.count) - 1u);

    struct UnfoldedTier {
      float height = 0.0f;
      float heightSum = 0.0f;
      float minimumElevation = 90.0f;
      float maximumElevation = -90.0f;
      std::array<uint32_t, kChannels> indices{};
      uint32_t count = 0u;
      double y = 0.0;
    };
    const Box inputMap = layoutProjectionRect(false);
    const Box outputMap = layoutProjectionRect(true);
    std::array<Point, kChannels> inputPoints{};
    std::array<Point, kChannels> outputPoints{};
    std::array<UnfoldedTier, kChannels> inputTiers{};
    std::array<UnfoldedTier, kChannels> outputTiers{};

    auto unfold = [&](const s3g::FormatUpscaleLayoutData &layout, Box map,
                      std::array<Point, kChannels> &points,
                      std::array<UnfoldedTier, kChannels> &tiers) {
      uint32_t tierCount = 0u;
      for (uint32_t index = 0u; index < layout.count; ++index) {
        const float height =
            s3g::formatUpscaleSpeakerHeight(layout.speakers[index]);
        const float elevation = layout.speakers[index].elevationDeg;
        uint32_t tier = 0u;
        while (tier < tierCount && std::abs(tiers[tier].height - height) >=
                                       s3g::kFormatUpscaleTierHeightTolerance)
          ++tier;
        if (tier == tierCount) {
          tiers[tier].height = height;
          ++tierCount;
        }
        tiers[tier].heightSum += height;
        tiers[tier].minimumElevation =
            std::min(tiers[tier].minimumElevation, elevation);
        tiers[tier].maximumElevation =
            std::max(tiers[tier].maximumElevation, elevation);
        tiers[tier].indices[tiers[tier].count++] = index;
      }
      std::sort(tiers.begin(), tiers.begin() + tierCount,
                [](const UnfoldedTier &a, const UnfoldedTier &b) {
                  return a.height < b.height;
                });
      for (uint32_t tier = 0u; tier < tierCount; ++tier) {
        tiers[tier].height =
            tiers[tier].heightSum /
            static_cast<float>(std::max<uint32_t>(1u, tiers[tier].count));
        tiers[tier].y = layoutProjectionPoint(map, 0.0f,
                                              (tiers[tier].minimumElevation +
                                               tiers[tier].maximumElevation) *
                                                  0.5f)
                            .y;
        std::sort(tiers[tier].indices.begin(),
                  tiers[tier].indices.begin() + tiers[tier].count,
                  [&](uint32_t a, uint32_t b) {
                    return layout.speakers[a].azimuthDeg <
                           layout.speakers[b].azimuthDeg;
                  });
      }

      layoutNodePoints(layout, map, _layoutOrigami, points);
      return tierCount;
    };
    const uint32_t inputTierCount =
        unfold(inputLayout, inputMap, inputPoints, inputTiers);
    const uint32_t outputTierCount =
        unfold(outputLayout, outputMap, outputPoints, outputTiers);

    setFill(calibratedColor(0x111313));
    fillBox(kLayoutSurfaceRect);
    setStroke(calibratedColor(0x3f4747, 0.85));
    frameBox(kLayoutSurfaceRect);
    setFill(calibratedColor(0x171a1a, 0.92));
    fillBox(inputMap);
    fillBox(outputMap);
    setStroke(calibratedColor(0x394141, 0.78));
    frameBox(inputMap);
    frameBox(outputMap);

    auto drawProjectionGrid = [&](Box map) {
      if (_layoutOrigami) {
        const Point center = Point(midX(map), midY(map));
        const double radius = std::max<double>(
            24.0, std::min(map.size.width, map.size.height) * 0.5 - 38.0);
        setStroke(calibratedColor(0x667070, 0.34));
        strokeLine(Point(center.x - radius, center.y),
                   Point(center.x + radius, center.y));
        strokeLine(Point(center.x, center.y - radius),
                   Point(center.x, center.y + radius));
        Path listener =
            Path::oval(makeBox(center.x - 2.5, center.y - 2.5, 5.0, 5.0));
        setFill(calibratedColor(0xaeb7b7, 0.74));
        drawPath(listener, true);

        std::string front = "FRONT  A0°";
        std::string rear = "REAR  A±180°";
        std::string left = "LEFT  A+90°";
        std::string right = "RIGHT  A−90°";
        const Size frontSize = measure(front, attrs);
        const Size rearSize = measure(rear, attrs);
        const Size leftSize = measure(left, attrs);
        drawAt(
            front,
            Point(center.x - frontSize.width * 0.5, center.y - radius - 19.0),
            attrs);
        drawAt(rear,
               Point(center.x - rearSize.width * 0.5, center.y + radius + 5.0),
               attrs);
        drawAt(left,
               Point(center.x - radius - leftSize.width - 6.0, center.y - 8.0),
               attrs);
        drawAt(right, Point(center.x + radius + 6.0, center.y - 8.0), attrs);
        return;
      }
      constexpr float azimuthGuides[3]{-90.0f, 0.0f, 90.0f};
      constexpr float elevationGuides[3]{90.0f, 0.0f, -90.0f};
      for (const float azimuth : azimuthGuides) {
        const double x = layoutProjectionPoint(map, azimuth, 0.0f).x;
        setStroke(
            calibratedColor(0x667070, std::abs(azimuth) < 0.5f ? 0.42 : 0.27));
        strokeLine(Point(x, map.origin.y + kLayoutProjectionVerticalInset),
                   Point(x, maxY(map) - kLayoutProjectionVerticalInset));
        std::string label =
            std::abs(azimuth) < 0.5f
                ? "A0° FRONT"
                : stringFormat("A%+.0f°", static_cast<double>(azimuth));
        const Size size = measure(label, attrs);
        drawAt(label, Point(x - size.width * 0.5, map.origin.y + 5.0), attrs);
      }
      for (const float elevation : elevationGuides) {
        const double y = layoutProjectionPoint(map, 0.0f, elevation).y;
        setStroke(calibratedColor(0x667070,
                                  std::abs(elevation) < 0.5f ? 0.42 : 0.27));
        strokeLine(Point(map.origin.x + kLayoutProjectionHorizontalInset, y),
                   Point(maxX(map) - kLayoutProjectionHorizontalInset, y));
        std::string label =
            stringFormat("E%+.0f°", static_cast<double>(elevation));
        drawAt(label, Point(map.origin.x + 5.0, y - 8.0), attrs);
      }
    };
    drawProjectionGrid(inputMap);
    drawProjectionGrid(outputMap);

    auto drawTierGuides = [&](const std::array<UnfoldedTier, kChannels> &tiers,
                              uint32_t tierCount, Box map,
                              const std::array<Point, kChannels> &points) {
      const uint32_t labelStride =
          tierCount <= 8u ? 1u
                          : static_cast<uint32_t>(std::ceil(
                                static_cast<double>(tierCount) / 8.0));
      for (uint32_t tier = 0u; tier < tierCount; ++tier) {
        if (_layoutOrigami) {
          const Point center = Point(midX(map), midY(map));
          setStroke(calibratedColor(0x54716e,
                                    tier % labelStride == 0u ? 0.58 : 0.24));
          Path contour = Path{};
          uint32_t contourCount = 0u;
          for (uint32_t offset = 0u; offset < tiers[tier].count; ++offset) {
            const Point point = points[tiers[tier].indices[offset]];
            if (std::hypot(point.x - center.x, point.y - center.y) < 2.0)
              continue;
            if (contourCount++ == 0u)
              contour.moveToPoint(point);
            else
              contour.lineToPoint(point);
          }
          if (contourCount >= 3u)
            contour.closePath();
          if (contourCount >= 2u) {
            contour.setLineWidth(tier == 0u ? 1.35 : 0.85);
            drawPath(contour, false);
          }
          continue;
        }
        const double top =
            layoutProjectionPoint(map, 0.0f, tiers[tier].maximumElevation).y;
        const double bottom =
            layoutProjectionPoint(map, 0.0f, tiers[tier].minimumElevation).y;
        const bool elevationBand =
            tiers[tier].maximumElevation - tiers[tier].minimumElevation > 0.75f;
        setStroke(
            calibratedColor(0x54716e, tier % labelStride == 0u ? 0.48 : 0.20));
        if (elevationBand) {

          const double bracketX = maxX(map) - kLayoutProjectionHorizontalInset;
          Path bracket = Path{};
          bracket.moveToPoint(Point(bracketX - 8.0, top));
          bracket.lineToPoint(Point(bracketX, top));
          bracket.lineToPoint(Point(bracketX, bottom));
          bracket.lineToPoint(Point(bracketX - 8.0, bottom));
          bracket.setLineWidth(1.0);
          drawPath(bracket, false);
        } else {
          strokeLine(Point(map.origin.x + kLayoutProjectionHorizontalInset,
                           tiers[tier].y),
                     Point(maxX(map) - kLayoutProjectionHorizontalInset,
                           tiers[tier].y));
        }
        if (tier % labelStride != 0u)
          continue;
        std::string tierTag = stringFormat("T%u", tier + 1u);
        const double tagY =
            (elevationBand ? (top + bottom) * 0.5 : tiers[tier].y) - 7.0;
        const Box tagRect =
            makeBox(maxX(map) - kLayoutProjectionHorizontalInset + 3.0, tagY,
                    22.0, 14.0);
        drawAt(tierTag, Point(tagRect.origin.x + 3.0, tagRect.origin.y), attrs);
      }
    };
    drawTierGuides(inputTiers, inputTierCount, inputMap, inputPoints);
    drawTierGuides(outputTiers, outputTierCount, outputMap, outputPoints);

    auto drawTierLegend = [&](const std::array<UnfoldedTier, kChannels> &tiers,
                              uint32_t tierCount, Box map) {
      if (tierCount == 0u)
        return;
      const uint32_t stride = tierCount <= 8u
                                  ? 1u
                                  : static_cast<uint32_t>(std::ceil(
                                        static_cast<double>(tierCount) / 8.0));
      const uint32_t visibleCount = static_cast<uint32_t>(std::ceil(
          static_cast<double>(tierCount) / static_cast<double>(stride)));
      const uint32_t columns = visibleCount > 4u ? 2u : 1u;
      const uint32_t rows = (visibleCount + columns - 1u) / columns;
      const double columnWidth = map.size.width / static_cast<double>(columns);
      uint32_t visible = 0u;
      for (uint32_t tier = 0u; tier < tierCount; tier += stride) {
        const uint32_t column = visible / rows;
        const uint32_t row = visible % rows;
        const bool elevationBand =
            tiers[tier].maximumElevation - tiers[tier].minimumElevation > 0.75f;
        std::string detail =
            elevationBand
                ? stringFormat(
                      "T%u  Z%+.2f  E%+.1f…%+.1f°  ×%u", tier + 1u,
                      static_cast<double>(tiers[tier].height),
                      static_cast<double>(tiers[tier].minimumElevation),
                      static_cast<double>(tiers[tier].maximumElevation),
                      tiers[tier].count)
                : stringFormat(
                      "T%u  E%+.1f°  ×%u", tier + 1u,
                      static_cast<double>((tiers[tier].minimumElevation +
                                           tiers[tier].maximumElevation) *
                                          0.5f),
                      tiers[tier].count);
        drawAt(detail,
               Point(map.origin.x + static_cast<double>(column) * columnWidth,
                     kLayoutSurfaceRect.origin.y + 56.0 +
                         static_cast<double>(row) * 14.0),
               valueAttrs);
        ++visible;
      }
    };
    drawTierLegend(inputTiers, inputTierCount, inputMap);
    drawTierLegend(outputTiers, outputTierCount, outputMap);

    uint32_t connectionCount = 0u;
    for (uint32_t input = 0u; input < inputLayout.count; ++input) {
      for (uint32_t output = 0u; output < outputLayout.count; ++output) {
        const float gain = _plugin->dsp.targetAnchorGain(input, output) +
                           _plugin->dsp.targetExtensionGain(input, output);
        if (std::abs(gain) > 0.0001f)
          ++connectionCount;
      }
    }
    struct FocusRoute {
      float anchor = 0.0f;
      float extension = 0.0f;
      float gain = 0.0f;
    };
    std::array<FocusRoute, kChannels> inputFocus{};
    std::array<FocusRoute, kChannels> outputFocus{};
    if (_selectionIsOutput) {
      for (uint32_t input = 0u; input < inputLayout.count; ++input) {
        inputFocus[input].anchor =
            _plugin->dsp.targetAnchorGain(input, _selectedOutput);
        inputFocus[input].extension =
            _plugin->dsp.targetExtensionGain(input, _selectedOutput);
        inputFocus[input].gain =
            inputFocus[input].anchor + inputFocus[input].extension;
      }
    } else {
      for (uint32_t output = 0u; output < outputLayout.count; ++output) {
        outputFocus[output].anchor =
            _plugin->dsp.targetAnchorGain(_selectedInput, output);
        outputFocus[output].extension =
            _plugin->dsp.targetExtensionGain(_selectedInput, output);
        outputFocus[output].gain =
            outputFocus[output].anchor + outputFocus[output].extension;
      }
    }
    auto maximumTierSize = [](const std::array<UnfoldedTier, kChannels> &tiers,
                              uint32_t tierCount) {
      uint32_t maximum = 1u;
      for (uint32_t tier = 0u; tier < tierCount; ++tier)
        maximum = std::max(maximum, tiers[tier].count);
      return maximum;
    };
    const double inputNodeSize = std::clamp<double>(
        (inputMap.size.width - 56.0) /
            static_cast<double>(maximumTierSize(inputTiers, inputTierCount)) *
            0.62,
        3.0, 12.0);
    const double outputNodeSize = std::clamp<double>(
        (outputMap.size.width - 56.0) /
            static_cast<double>(maximumTierSize(outputTiers, outputTierCount)) *
            0.62,
        3.0, 12.0);
    auto drawNodes = [&](const s3g::FormatUpscaleLayoutData &layout,
                         const std::array<Point, kChannels> &points,
                         const std::array<FocusRoute, kChannels> &routes,
                         bool output, double baseNodeSize) {
      std::array<double, kChannels> nodeSizes{};
      std::array<Box, kChannels> nodeRects{};
      std::array<uint8_t, kChannels> selectedFlags{};
      std::array<uint8_t, kChannels> mappedFlags{};
      for (uint32_t index = 0u; index < layout.count; ++index) {
        const bool selected =
            output ? index == _selectedOutput : index == _selectedInput;
        const bool focusNode = output ? (_selectionIsOutput && selected)
                                      : (!_selectionIsOutput && selected);
        const bool mapped = std::abs(routes[index].gain) > 0.0001f;
        const double magnitude =
            std::min<double>(1.0, std::abs(routes[index].gain));
        double size = mapped
                          ? baseNodeSize * (0.82 + 0.70 * std::sqrt(magnitude))
                          : baseNodeSize;
        if (selected)
          size = std::max<double>(8.0, size + 3.0);
        if (focusNode)
          size = std::max<double>(10.0, size + 2.0);
        const Box node = makeBox(points[index].x - size * 0.5,
                                 points[index].y - size * 0.5, size, size);
        nodeSizes[index] = size;
        nodeRects[index] = node;
        selectedFlags[index] = selected ? 1u : 0u;
        mappedFlags[index] = mapped ? 1u : 0u;
        CColor fill = calibratedColor(0x3d4444, 0.94);
        if (focusNode)
          fill = calibratedColor(output ? 0x75aaa3 : 0xd8d8d8, 0.99);
        else if (mapped)
          fill = routes[index].gain < 0.0f
                     ? calibratedColor(0xa17386, 0.99)
                     : (std::abs(routes[index].extension) >=
                                std::abs(routes[index].anchor)
                            ? calibratedColor(0x6eaaa2, 0.99)
                            : calibratedColor(0xb8b8b8, 0.99));
        if (output) {
          setFill(fill);
          fillBox(node);
          setStroke(
              calibratedColor(selected || mapped ? 0xe0e0e0 : 0x777f7f, 0.95));
          frameBox(node);
        } else {
          Path circle = Path::oval(node);
          setFill(fill);
          drawPath(circle, true);
          setStroke(
              calibratedColor(selected || mapped ? 0xe0e0e0 : 0x171919, 0.98));
          circle.setLineWidth(selected || mapped ? 1.8 : 1.0);
          drawPath(circle, false);
        }
      }

      const Box map = output ? outputMap : inputMap;
      const Point mapCenter = Point(midX(map), midY(map));
      std::array<Box, kChannels> occupiedLabels{};
      uint32_t occupiedCount = 0u;
      auto candidateIsClear = [&](Box candidate, uint32_t ownNode) {
        if (!containsBox(insetBox(map, 3.0, 3.0), candidate))
          return false;
        const Box padded = insetBox(candidate, -1.5, -1.5);
        for (uint32_t node = 0u; node < layout.count; ++node) {
          if (node == ownNode)
            continue;
          if (intersectsBox(padded, insetBox(nodeRects[node], -2.0, -2.0)))
            return false;
        }
        for (uint32_t label = 0u; label < occupiedCount; ++label)
          if (intersectsBox(padded, occupiedLabels[label]))
            return false;
        return true;
      };
      auto drawLabelPass = [&](bool priority) {
        for (uint32_t index = 0u; index < layout.count; ++index) {
          const bool important =
              selectedFlags[index] != 0u || mappedFlags[index] != 0u;
          if (important != priority)
            continue;
          const char *role =
              s3g::formatUpscaleRoleName(layout.speakers[index].role);
          std::string label =
              layout.count <= 12u && role[0]
                  ? stringFormat("%s%u %s", output ? "O" : "I", index + 1u,
                                 role)
                  : stringFormat("%s%u", output ? "O" : "I", index + 1u);
          const Size labelSize = measure(label, valueAttrs);
          const double gap = nodeSizes[index] * 0.5 + 4.0;
          std::array<Point, 4u> candidates{};
          uint32_t candidateCount = 1u;
          if (!_layoutOrigami) {
            candidates[0u] = Point(points[index].x - labelSize.width * 0.5,
                                   points[index].y + gap);
          } else {
            const double dx = points[index].x - mapCenter.x;
            const double dy = points[index].y - mapCenter.y;
            if (std::abs(dx) >= std::abs(dy)) {
              candidates[0u] =
                  dx >= 0.0 ? Point(points[index].x + gap,
                                    points[index].y - labelSize.height * 0.5)
                            : Point(points[index].x - gap - labelSize.width,
                                    points[index].y - labelSize.height * 0.5);
            } else {
              candidates[0u] =
                  dy >= 0.0 ? Point(points[index].x - labelSize.width * 0.5,
                                    points[index].y + gap)
                            : Point(points[index].x - labelSize.width * 0.5,
                                    points[index].y - gap - labelSize.height);
            }
            candidates[1u] = Point(points[index].x + gap,
                                   points[index].y - labelSize.height * 0.5);
            candidates[2u] = Point(points[index].x - labelSize.width * 0.5,
                                   points[index].y + gap);
            candidates[3u] = Point(points[index].x - gap - labelSize.width,
                                   points[index].y - labelSize.height * 0.5);
            candidateCount = 4u;
          }
          bool placed = false;
          Box chosen = Box{};
          for (uint32_t candidate = 0u; candidate < candidateCount;
               ++candidate) {
            const Box rect =
                makeBox(candidates[candidate].x, candidates[candidate].y,
                        labelSize.width, labelSize.height);
            if (!candidateIsClear(rect, index))
              continue;
            chosen = rect;
            placed = true;
            break;
          }
          if (!placed && important) {
            chosen = makeBox(candidates[0u].x, candidates[0u].y,
                             labelSize.width, labelSize.height);
            placed = true;
          }
          if (!placed)
            continue;
          drawAt(label, chosen.origin, valueAttrs);
          occupiedLabels[occupiedCount++] = chosen;
        }
      };
      drawLabelPass(true);
      drawLabelPass(false);
      if (output)
        _lastLayoutOutputLabelCount = occupiedCount;
      else
        _lastLayoutInputLabelCount = occupiedCount;
    };
    drawNodes(inputLayout, inputPoints, inputFocus, false, inputNodeSize);
    drawNodes(outputLayout, outputPoints, outputFocus, true, outputNodeSize);

    std::string inputTitle =
        stringFormat("INPUT %s   %s   •   %u CHANNEL%s   •   %u TIER%s",
                     _layoutOrigami ? "PLAN" : "AED",
                     s3g::formatUpscaleLayoutName(_plugin->params.inputLayout),
                     inputLayout.count, inputLayout.count == 1u ? "" : "S",
                     inputTierCount, inputTierCount == 1u ? "" : "S");
    std::string outputTitle =
        stringFormat("OUTPUT %s   %s   •   %u CHANNEL%s   •   %u TIER%s",
                     _layoutOrigami ? "PLAN" : "AED",
                     s3g::formatUpscaleLayoutName(_plugin->params.outputLayout),
                     outputLayout.count, outputLayout.count == 1u ? "" : "S",
                     outputTierCount, outputTierCount == 1u ? "" : "S");
    drawAt(inputTitle,
           Point(inputMap.origin.x, kLayoutSurfaceRect.origin.y + 36.0), attrs);
    drawAt(outputTitle,
           Point(outputMap.origin.x, kLayoutSurfaceRect.origin.y + 36.0),
           attrs);
    auto drawAzimuthAxis = [&](Box map) {
      const double y = maxY(map) + 13.0;
      if (_layoutOrigami) {
        std::string label =
            "LOW / T1 = OUTER   •   HIGH = INNER   •   E+90° = CENTER";
        const Size size = measure(label, valueAttrs);
        drawAt(label, Point(midX(map) - size.width * 0.5, y), valueAttrs);
        return;
      }
      constexpr float ticks[5]{-180.0f, -90.0f, 0.0f, 90.0f, 180.0f};
      for (const float tick : ticks) {
        std::string label = stringFormat("%+.0f°", static_cast<double>(tick));
        const Size size = measure(label, valueAttrs);
        const double x = layoutProjectionPoint(map, tick, 0.0f).x;
        drawAt(label, Point(x - size.width * 0.5, y), valueAttrs);
      }
    };
    drawAzimuthAxis(inputMap);
    drawAzimuthAxis(outputMap);
    uint32_t focusedCount = 0u;
    if (_selectionIsOutput) {
      for (uint32_t input = 0u; input < inputLayout.count; ++input)
        if (std::abs(inputFocus[input].gain) > 0.0001f)
          ++focusedCount;
    } else {
      for (uint32_t output = 0u; output < outputLayout.count; ++output)
        if (std::abs(outputFocus[output].gain) > 0.0001f)
          ++focusedCount;
    }
    std::string footer = stringFormat(
        "FOCUS %s%u   •   %u OF %u ROUTES   •   CLICK POINT = FOCUS   •   GRAY "
        "/ TEAL = POSITIVE   •   PURPLE = STEREO DIFFERENCE   •   SIZE = "
        "|GAIN|",
        _selectionIsOutput ? "O" : "I",
        (_selectionIsOutput ? _selectedOutput : _selectedInput) + 1u,
        focusedCount, connectionCount);
    drawAt(footer,
           Point(kLayoutSurfaceRect.origin.x + 10.0,
                 maxY(kLayoutSurfaceRect) - 22.0),
           valueAttrs);

    if (!_layoutPopupChild)
      drawToolboxHeaderButton(viewSelectorRect(0u), kMatrixPanelRect, "MATRIX",
                              false, attrs, style);
    drawToolboxHeaderButton(viewSelectorRect(1u), kMatrixPanelRect, "LAYOUT",
                            true, attrs, style);
    drawToolboxHeaderButton(
        layoutPopupActionRect(), kLayoutSurfaceRect,
        _layoutPopupChild ? "DOCK" : "POP OUT",
        _layoutPopupChild ||
            (_layoutPanel && (_layoutPanel && _layoutPanel->visible())),
        attrs, style);
  }

  void drawOpenMenu(bool attrs, const Palette &style) {
    if (_openMenu == CLAP_INVALID_ID)
      return;
    const uint32_t count = menuCountForParam(_openMenu);
    if (count == 0u)
      return;
    std::array<std::string, s3g::kFormatUpscaleLayoutCount> items{};
    for (uint32_t index = 0u; index < count; ++index) {
      const char *name = _openMenu == kUiFactoryPreset
                             ? matrixPresetName(index)
                             : (_openMenu == kParamPlacement
                                    ? recipeName(index)
                                    : menuValueName(_openMenu, index));
      items[index] = std::string(name);
    }
    const auto menu = formatMenuGeometry(_openMenu);
    const int selected =
        _openMenu == kUiFactoryPreset
            ? (_matrixPreset < kMatrixPresetCount
                   ? static_cast<int>(_matrixPreset)
                   : -1)
            : (_openMenu == kParamPlacement
                   ? static_cast<int>(recipeIndex(_plugin->params.placement))
                   : static_cast<int>(
                         roundedUint(getParamValue(*_plugin, _openMenu))));
    drawDropdownMenu(menu.first, menu.itemHeight, items.data(), menu.firstCount,
                     selected < static_cast<int>(menu.firstCount) ? selected
                                                                  : -1,
                     _hoverMenuItem >= 0 &&
                             _hoverMenuItem < static_cast<int>(menu.firstCount)
                         ? _hoverMenuItem
                         : -1,
                     attrs, style);
    drawDropdownMenu(menu.second, menu.itemHeight,
                     items.data() + menu.firstCount, menu.secondCount,
                     selected >= static_cast<int>(menu.firstCount)
                         ? selected - static_cast<int>(menu.firstCount)
                         : -1,
                     _hoverMenuItem >= static_cast<int>(menu.firstCount)
                         ? _hoverMenuItem - static_cast<int>(menu.firstCount)
                         : -1,
                     attrs, style);
  }

  void drawRect(Box dirtyRect) {
    (void)dirtyRect;
    if (!_plugin)
      return;
    const Palette style{};
    bool attrs = false;
    bool valueAttrs = true;
    setFill(style.background);
    fillBox(makeBox(0, 0, kGuiWidth, kGuiHeight));
    const s3g::gui_layout::Canvas canvas{static_cast<double>(kGuiWidth),
                                         static_cast<double>(kGuiHeight)};
    const auto titleBand = s3g::gui_layout::outputUtilityTitleBand(canvas);
    std::string summary =
        stringFormat("%s → %s / %s%s",
                     s3g::formatUpscaleLayoutName(_plugin->params.inputLayout),
                     s3g::formatUpscaleLayoutName(_plugin->params.outputLayout),
                     _page == 1u ? "TIER RINGS" : "MATRIX",
                     _layoutPopupChild ? " / DETACHED" : "");
    const char *presetName = _layoutPopupChild && _layoutPopupOwner
                                 ? _layoutPopupOwner->_titlePresetName
                                 : _titlePresetName;
    drawOutputUtilityTitleBand("s3g MATRIX UPMIX 64", std::string(presetName),
                               summary, titleBand, style);

    if (_page == 1u) {
      drawPanel(kMatrixPanelRect, "LAYOUT — TIER RINGS / OVERHEAD PLAN", attrs,
                style);
      drawUnfoldedLayoutPage(attrs, valueAttrs, style);

      drawOutputUtilityTitleBand("s3g MATRIX UPMIX 64", std::string(presetName),
                                 summary, titleBand, style);
      return;
    }

    drawPanel(kMatrixPanelRect,
              "CONNECTION MATRIX — INPUT ROWS / OUTPUT COLUMNS", attrs, style);
    drawMatrixEditor(attrs, valueAttrs, style);

    drawPanel(kFormatPanelRect, "SETUP", attrs, style);
    drawSideFormatControl(kParamInputLayout, false, attrs, valueAttrs, style);
    drawSideFormatControl(kParamOutputLayout, true, attrs, valueAttrs, style);
    drawSetupActions(attrs, style);

    drawPanel(kAutoPanelRect, "AUTO FILL", attrs, style);
    drawSideAutoModeControl(attrs, valueAttrs, style);

    if (_designMap >= 0) {
      drawPanel(kRightPanelRect,
                _designMap == 0 ? "CUSTOM INPUT — AED / XYZ"
                                : "CUSTOM OUTPUT — AED / XYZ",
                attrs, style);
      drawCustomEditor(attrs, valueAttrs, style);
    } else {
      drawPanel(kSelectedPanelRect, "SELECTED CROSSPOINT", attrs, style);
      drawPanel(cocoaPanelRect(kExtensionPanel), "OUTPUT", attrs, style);
      drawConnectionInspector(attrs, valueAttrs, style);
    }
    drawOpenMenu(attrs, style);
  }

  void updateSlider(Point point) {
    const ParamDef *def = findParam(_dragParam);
    if (!def)
      return;
    Box row = sliderRowForParam(_dragParam);
    const double start =
        static_cast<double>(s3g::gui_layout::processorControlX(row.origin.x));
    const double width = std::max<double>(
        20.0, static_cast<double>(
                  s3g::gui_layout::processorTrackWidth(row.size.width)));
    const double normalized =
        std::clamp(static_cast<double>((point.x - start) / width), 0.0, 1.0);
    const double minimum = _dragParam == kParamCopies ? 1.0 : def->minimum;
    const double maximum =
        _dragParam == kParamCopies
            ? static_cast<double>(autoFillCopyMaximum(*_plugin))
            : def->maximum;
    double value = minimum + normalized * (maximum - minimum);
    if (def->stepped)
      value = std::floor(value + 0.5);
    setParamValue(*_plugin, _dragParam, value);
    invalid();
  }

  void updateSelectedWeight(Point point) {
    if (!_plugin)
      return;
    const Box hit = selectedWeightHitRect();
    const float normalized = static_cast<float>(std::clamp<double>(
        (point.x - hit.origin.x) / std::max<double>(20.0, hit.size.width), 0.0,
        1.0));
    if (!_plugin->dsp.manualRoutesActive())
      makeCurrentMatrixExact(*_plugin);
    const float current =
        _plugin->dsp.manualWeight(_selectedInput, _selectedOutput);
    _plugin->dsp.setManualWeight(_selectedInput, _selectedOutput,
                                 current < 0.0f ? -normalized : normalized);
    markMatrixCustom();
    invalid();
  }

  void updateCustomSlider(Point point) {
    if (!_plugin || _designMap < 0 || _dragCustom < 0 || _dragCustom > 4)
      return;
    const uint32_t column = static_cast<uint32_t>(_dragCustom);
    const Box hit = customEditorSliderHitRect(column);
    const double normalized = std::clamp<double>(
        (point.x - hit.origin.x) / std::max<double>(20.0, hit.size.width), 0.0,
        1.0);
    auto layout = editableLayout();
    uint32_t selected = std::min<uint32_t>(
        editableSelection(), std::max<uint32_t>(1u, layout.count) - 1u);
    if (column == 0u) {
      const uint32_t minimum =
          _designMap == 1 ? std::max<uint32_t>(1u, _plugin->dsp.activeInputs())
                          : 1u;
      const uint32_t maximum =
          _designMap == 0
              ? std::max<uint32_t>(minimum, _plugin->dsp.activeOutputs())
              : kChannels;
      const uint32_t oldCount = layout.count;
      layout.count = std::clamp<uint32_t>(
          static_cast<uint32_t>(std::floor(
              normalized * static_cast<double>(maximum - minimum) + 0.5)) +
              minimum,
          minimum, maximum);
      if (layout.count > oldCount) {
        const float step = 360.0f / static_cast<float>(layout.count);
        for (uint32_t index = oldCount; index < layout.count; ++index) {
          const float azimuth = s3g::formatUpscaleWrapDegrees(
              -30.0f - step * static_cast<float>(index));
          s3g::formatUpscaleSetSpeaker(
              layout, index, azimuth, 0.0f,
              s3g::formatUpscaleRoleForAed(azimuth, 0.0f));
        }
      }
      selected = std::min<uint32_t>(selected, layout.count - 1u);
      if (_designMap == 1)
        _selectedOutput = selected;
      else
        _selectedInput = selected;
    } else if (column == 1u) {
      selected =
          layout.count > 1u
              ? static_cast<uint32_t>(std::floor(
                    normalized * static_cast<double>(layout.count - 1u) + 0.5))
              : 0u;
      if (_designMap == 1)
        _selectedOutput = selected;
      else
        _selectedInput = selected;
    } else {
      auto &speaker = layout.speakers[selected];
      if (column == 2u)
        speaker.azimuthDeg = s3g::aedAzimuthFromSliderNorm(normalized);
      else if (column == 3u)
        speaker.elevationDeg = static_cast<float>(normalized * 180.0 - 90.0);
      else
        speaker.distance = static_cast<float>(0.1 + normalized * 2.9);
    }
    setEditableLayout(layout);
    updateCustomValueFields();
    invalid();
  }

  void mouseDown(MouseDownEvent &event) {
    const Point point = event.mousePosition;
    const s3g::gui_layout::Canvas canvas{static_cast<double>(kGuiWidth),
                                         static_cast<double>(kGuiHeight)};
    const auto titleBand = s3g::gui_layout::outputUtilityTitleBand(canvas);
    if (pointInBox(point, layoutBox(titleBand.presetMenu))) {
      Editor *owner =
          _layoutPopupChild && _layoutPopupOwner ? _layoutPopupOwner : this;
      owner->_page = 0u;
      owner->_designMap = -1;
      owner->_openMenu = kUiFactoryPreset;
      owner->_hoverMenuItem = -1;
      (owner ? owner->invalid() : void());
      return;
    }
    const bool loadingTitlePreset =
        pointInBox(point, layoutBox(titleBand.loadButton));
    if (handleTitleClick(point)) {
      if (loadingTitlePreset)
        _matrixPreset = kCustomMatrixPreset;
      _openMenu = CLAP_INVALID_ID;
      _hoverMenuItem = -1;
      updateCustomValueFields();
      invalid();
      return;
    }
    for (uint32_t view = 0u; view < 2u; ++view) {
      if (!pointInBox(point, viewSelectorRect(view)))
        continue;
      if (view == 0u) {
        if (_layoutPopupChild)
          return;
        _page = 0u;
        _designMap = -1;
        updateCustomValueFields();
        invalid();
        return;
      }
      if (!_layoutPopupChild) {
        _page = 1u;
        _designMap = -1;
        updateCustomValueFields();
      }
      setDocumentationLayoutOrigami(true);
      return;
    }
    if (_page == 1u) {
      if (pointInBox(point, layoutPopupActionRect())) {
        if (_layoutPopupChild && _layoutPopupOwner)
          _layoutPopupOwner->dockLayoutPopup();
        else
          openLayoutPopup();
        return;
      }
      const auto selectLayoutNode = [&](bool output) {
        const auto &layout =
            output ? _plugin->dsp.outputLayout() : _plugin->dsp.inputLayout();
        const Box map = layoutProjectionRect(output);
        if (!pointInBox(point, map))
          return false;
        std::array<Point, kChannels> points{};
        layoutNodePoints(layout, map, _layoutOrigami, points);
        uint32_t nearest = 0u;
        double nearestDistance = 1000000.0;
        for (uint32_t index = 0u; index < layout.count; ++index) {
          const double dx = point.x - points[index].x;
          const double dy = point.y - points[index].y;
          const double distance = dx * dx + dy * dy;
          if (distance < nearestDistance) {
            nearestDistance = distance;
            nearest = index;
          }
        }
        if (nearestDistance > kLayoutNodeHitRadius * kLayoutNodeHitRadius)
          return false;
        Editor *root =
            _layoutPopupChild && _layoutPopupOwner ? _layoutPopupOwner : this;
        if (output)
          root->_selectedOutput = nearest;
        else
          root->_selectedInput = nearest;
        root->_selectionIsOutput = output;
        root->_shapeColumn = output;
        if (root->_layoutPopupView) {
          root->_layoutPopupView->_selectedInput = root->_selectedInput;
          root->_layoutPopupView->_selectedOutput = root->_selectedOutput;
          root->_layoutPopupView->_selectionIsOutput = output;
        }
        _selectedInput = root->_selectedInput;
        _selectedOutput = root->_selectedOutput;
        _selectionIsOutput = output;
        _shapeColumn = output;
        (root ? root->invalid() : void());
        (root->_layoutPopupView ? root->_layoutPopupView->invalid() : void());
        invalid();
        return true;
      };
      if (selectLayoutNode(false) || selectLayoutNode(true))
        return;
      return;
    }
    if (_openMenu != CLAP_INVALID_ID) {
      const auto menu = formatMenuGeometry(_openMenu);
      int hit =
          dropdownHitIndex(point, menu.first, menu.itemHeight, menu.firstCount);
      if (hit < 0) {
        const int secondHit = dropdownHitIndex(
            point, menu.second, menu.itemHeight, menu.secondCount);
        if (secondHit >= 0)
          hit = static_cast<int>(menu.firstCount) + secondHit;
      }
      if (hit >= 0) {
        if (_openMenu == kUiFactoryPreset) {
          applyMatrixPreset(static_cast<uint32_t>(hit));
        } else if (_openMenu == kParamPlacement) {
          setParamValue(
              *_plugin, kParamPlacement,
              static_cast<double>(recipePlacement(static_cast<uint32_t>(hit))));
          markMatrixCustom();
        } else {
          setParamValue(*_plugin, _openMenu, static_cast<double>(hit));
        }
        if (_openMenu == kParamInputLayout || _openMenu == kParamOutputLayout) {
          if (static_cast<uint32_t>(hit) !=
              static_cast<uint32_t>(s3g::FormatUpscaleLayout::Custom))
            _designMap = -1;
          markMatrixCustom();
          updateCustomValueFields();
        }
      }
      _openMenu = CLAP_INVALID_ID;
      _hoverMenuItem = -1;
      invalid();
      return;
    }

    for (uint32_t output = 0u; output < 2u; ++output) {
      if (!pointInBox(point, formatEditButtonRect(output != 0u)))
        continue;
      const int map = static_cast<int>(output);
      if (_designMap == map) {
        _designMap = -1;
        updateCustomValueFields();
      } else {
        enterDesignMap(map);
        _selectionIsOutput = output != 0u;
      }
      invalid();
      return;
    }

    for (uint32_t action = 0u; action < 2u; ++action) {
      if (!pointInBox(point, sideAutoActionRect(action)))
        continue;
      if (action == 0u)
        applyAutoFill(*_plugin);
      else {
        _plugin->normalization = s3g::FormatUpscaleNormalization::Exact;
        _plugin->dsp.setNormalization(_plugin->normalization);
        _plugin->dsp.clearManualRoutes();
      }
      markMatrixCustom();
      _designMap = -1;
      updateCustomValueFields();
      invalid();
      return;
    }

    if (_designMap >= 0) {
      for (uint32_t action = 0u; action < 3u; ++action) {
        if (!pointInBox(point, customEditorActionRect(action)))
          continue;
        if (action == 0u) {
          const auto layout =
              _designMap == 0
                  ? s3g::formatUpscaleDefaultCustomInputLayout()
                  : s3g::formatUpscaleDefaultThreeTierOutputLayout();
          setEditableLayout(layout);
          if (_designMap == 0)
            _selectedInput = 0u;
          else
            _selectedOutput = 0u;
        } else if (action == 1u) {
          const auto layout = _designMap == 0 ? _plugin->dsp.outputLayout()
                                              : _plugin->dsp.inputLayout();
          setEditableLayout(layout);
          if (_designMap == 0)
            _selectedInput = 0u;
          else
            _selectedOutput = 0u;
        } else {
          _designMap = -1;
        }
        updateCustomValueFields();
        invalid();
        return;
      }
      for (uint32_t column = 0u; column < 5u; ++column) {
        if (!pointInBox(point, customEditorSliderHitRect(column)))
          continue;
        _dragCustom = static_cast<int>(column);
        updateCustomSlider(point);
        return;
      }
    }

    const clap_id menus[4]{kParamInputLayout, kParamOutputLayout,
                           kParamPlacement, kParamAutoRowShape};
    for (clap_id param : menus) {
      if (pointInBox(point, menuValueRectForParam(param))) {
        _openMenu = param;
        _hoverMenuItem = -1;
        invalid();
        return;
      }
    }

    if (_designMap < 0) {
      if (pointInBox(point, crosspointActionRect(0u))) {
        if (!_plugin->dsp.manualRoutesActive())
          makeCurrentMatrixExact(*_plugin);
        _plugin->dsp.setManualWeight(_selectedInput, _selectedOutput, 0.0f);
        markMatrixCustom();
        invalid();
        return;
      }
      for (uint32_t action = 0u; action < 2u; ++action) {
        if (pointInBox(point, levelActionRect(action))) {
          if (action == 0u)
            normalizeExactInputRows(*_plugin);
          else
            limitExactOutputColumns(*_plugin);
          markMatrixCustom();
          invalid();
          return;
        }
      }
      if (pointInBox(point, selectedWeightHitRect())) {
        _dragWeight = true;
        updateSelectedWeight(point);
        return;
      }
      const clap_id sliders[2]{kParamCopies, kParamOutputGain};
      for (clap_id param : sliders) {
        if (param == kParamCopies &&
            _plugin->params.placement == s3g::FormatUpscalePlacement::TierFill)
          continue;
        if (!pointInBox(point, sliderHitRectForParam(param)))
          continue;
        double defaultValue = 0.0;
        if (sliderDefault(event.clickCount, param, defaultValue)) {
          if (param == kParamCopies)
            defaultValue =
                std::min<double>(defaultValue, autoFillCopyMaximum(*_plugin));
          setParamValue(*_plugin, param, defaultValue);
        } else {
          _dragParam = param;
          updateSlider(point);
        }
        invalid();
        return;
      }
    }

    const auto &inputLayout = _plugin->dsp.inputLayout();
    const auto &outputLayout = _plugin->dsp.outputLayout();
    const auto geometry = matrixGeometry(inputLayout.count, outputLayout.count);
    for (uint32_t input = 0u; input < inputLayout.count; ++input) {
      const Box labelRect = makeBox(
          geometry.grid.origin.x - 92.0,
          geometry.grid.origin.y + static_cast<double>(input) * geometry.cell,
          88.0, geometry.cell);
      if (!pointInBox(point, labelRect))
        continue;
      _selectedInput = input;
      _selectionIsOutput = false;
      _shapeColumn = false;
      invalid();
      return;
    }
    for (uint32_t output = 0u; output < outputLayout.count; ++output) {
      const Box labelRect = makeBox(
          geometry.grid.origin.x + static_cast<double>(output) * geometry.cell,
          geometry.grid.origin.y - 45.0, geometry.cell, 43.0);
      if (!pointInBox(point, labelRect))
        continue;
      _selectedOutput = output;
      _selectionIsOutput = true;
      _shapeColumn = true;
      invalid();
      return;
    }
    uint32_t matrixInput = 0u;
    uint32_t matrixOutput = 0u;
    if (matrixCellAtPoint(point, inputLayout.count, outputLayout.count,
                          matrixInput, matrixOutput)) {
      const bool connected =
          std::abs(_plugin->dsp.targetAnchorGain(matrixInput, matrixOutput)) >
              0.0001f ||
          std::abs(_plugin->dsp.targetExtensionGain(matrixInput,
                                                    matrixOutput)) > 0.0001f;
      if (!_plugin->dsp.manualRoutesActive())
        makeCurrentMatrixExact(*_plugin);
      _selectedInput = matrixInput;
      _selectedOutput = matrixOutput;
      _selectionIsOutput = false;
      markMatrixCustom();
      if (event.clickCount >= 2) {
        _plugin->dsp.setManualWeight(matrixInput, matrixOutput,
                                     connected ? 0.0f : 1.0f);
      } else {
        if (!connected) {
          _plugin->dsp.setManualWeight(matrixInput, matrixOutput, 1.0f);
        }
        _matrixPainting = true;
        _matrixPaintConnected = true;
        _matrixWeightAdjusting = false;
        _matrixDragOrigin = point;
        _matrixDragStartWeight =
            _plugin->dsp.manualWeight(matrixInput, matrixOutput);
        _matrixPaintInput = static_cast<int>(matrixInput);
        _matrixPaintOutput = static_cast<int>(matrixOutput);
      }
      invalid();
      return;
    }
  }

  void rightMouseDown(MouseDownEvent &event) {
    if (!_plugin || _page == 1u || _designMap >= 0) {
      (void)0;
      return;
    }
    const Point point = event.mousePosition;
    if (_openMenu != CLAP_INVALID_ID) {
      _openMenu = CLAP_INVALID_ID;
      _hoverMenuItem = -1;
      invalid();
      return;
    }
    const auto &inputLayout = _plugin->dsp.inputLayout();
    const auto &outputLayout = _plugin->dsp.outputLayout();
    uint32_t input = 0u;
    uint32_t output = 0u;
    if (!matrixCellAtPoint(point, inputLayout.count, outputLayout.count, input,
                           output)) {
      (void)0;
      return;
    }
    if (!_plugin->dsp.manualRoutesActive())
      makeCurrentMatrixExact(*_plugin);
    _plugin->dsp.setManualWeight(input, output, 0.0f);
    markMatrixCustom();
    _selectedInput = input;
    _selectedOutput = output;
    _selectionIsOutput = false;
    invalid();
  }

  void mouseMoved(MouseMoveEvent &event) {
    if (_openMenu == CLAP_INVALID_ID)
      return;
    const Point point = event.mousePosition;
    const auto menu = formatMenuGeometry(_openMenu);
    _hoverMenuItem =
        dropdownHitIndex(point, menu.first, menu.itemHeight, menu.firstCount);
    if (_hoverMenuItem < 0) {
      const int second = dropdownHitIndex(point, menu.second, menu.itemHeight,
                                          menu.secondCount);
      if (second >= 0)
        _hoverMenuItem = static_cast<int>(menu.firstCount) + second;
    }
    invalid();
  }

  void mouseDragged(MouseMoveEvent &event) {
    if (_page == 1u)
      return;
    const Point point = event.mousePosition;
    if (_matrixPainting) {
      uint32_t input = 0u;
      uint32_t output = 0u;
      const auto &inputLayout = _plugin->dsp.inputLayout();
      const auto &outputLayout = _plugin->dsp.outputLayout();
      const auto geometry =
          matrixGeometry(inputLayout.count, outputLayout.count);
      const bool inside = matrixCellAtPoint(point, inputLayout.count,
                                            outputLayout.count, input, output);
      const bool sameCell = inside &&
                            static_cast<int>(input) == _matrixPaintInput &&
                            static_cast<int>(output) == _matrixPaintOutput;
      if (!_matrixWeightAdjusting && sameCell && geometry.cell >= 10.0 &&
          std::abs(point.y - _matrixDragOrigin.y) >= 2.0) {
        _matrixWeightAdjusting = true;
      }
      if (_matrixWeightAdjusting) {
        const float magnitude = static_cast<float>(
            std::clamp<double>(std::abs(_matrixDragStartWeight) +
                                   (_matrixDragOrigin.y - point.y) /
                                       std::max<double>(12.0, geometry.cell),
                               0.0, 1.0));
        _plugin->dsp.setManualWeight(static_cast<uint32_t>(_matrixPaintInput),
                                     static_cast<uint32_t>(_matrixPaintOutput),
                                     _matrixDragStartWeight < 0.0f ? -magnitude
                                                                   : magnitude);
        markMatrixCustom();
        _selectedInput = static_cast<uint32_t>(_matrixPaintInput);
        _selectedOutput = static_cast<uint32_t>(_matrixPaintOutput);
        invalid();
      } else if (inside && !sameCell) {
        _plugin->dsp.setManualRoute(input, output, _matrixPaintConnected);
        _matrixPaintInput = static_cast<int>(input);
        _matrixPaintOutput = static_cast<int>(output);
        _matrixDragOrigin = point;
        _matrixDragStartWeight = _plugin->dsp.manualWeight(input, output);
        _selectedInput = input;
        _selectedOutput = output;
        _selectionIsOutput = false;
        invalid();
      }
      return;
    }
    if (_dragWeight) {
      updateSelectedWeight(point);
      return;
    }
    if (_dragCustom >= 0) {
      updateCustomSlider(point);
      return;
    }
    if (_dragParam != CLAP_INVALID_ID)
      updateSlider(point);
  }

  void mouseUp(MouseUpEvent &event) {
    if (_matrixPainting) {
      _matrixPainting = false;
      _matrixWeightAdjusting = false;
      _matrixPaintInput = -1;
      _matrixPaintOutput = -1;
      invalid();
      return;
    }
    (void)event;
    _dragWeight = false;
    _dragParam = CLAP_INVALID_ID;
    _dragCustom = -1;
  }

  void applyMatrixPreset(uint32_t preset) {
    if (!_plugin || preset >= kMatrixPresetCount)
      return;
    const auto selectedPreset = static_cast<MatrixPreset>(preset);
    auto finishPreset = [&]() {
      _matrixPreset = preset;
      std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                    matrixPresetName(preset));
      _selectedInput = 0u;
      _selectedOutput = 0u;
      _selectionIsOutput = false;
      _designMap = -1;
      updateCustomValueFields();
      invalid();
    };

    if (selectedPreset == MatrixPreset::Init) {
      const float outputGain = _plugin->params.outputGainDb;
      _plugin->params = {};
      _plugin->params.outputGainDb = outputGain;
      _plugin->autoRowShape = s3g::FormatUpscaleRowShape::Flat;
      _plugin->normalization = s3g::FormatUpscaleNormalization::Exact;
      applyParams(*_plugin);
      _plugin->dsp.useAutomaticRoutes();
      finishPreset();
      return;
    }

    s3g::FormatUpscaleLayout expansionInput = s3g::FormatUpscaleLayout::Stereo;
    s3g::FormatUpscaleLayout expansionOutput = s3g::FormatUpscaleLayout::Quad;
    bool expansionPreset = true;
    switch (selectedPreset) {
    case MatrixPreset::QuadOctophonicRing:
      expansionInput = s3g::FormatUpscaleLayout::Quad;
      expansionOutput = s3g::FormatUpscaleLayout::OctophonicRing;
      break;
    case MatrixPreset::QuadRing12:
      expansionInput = s3g::FormatUpscaleLayout::Quad;
      expansionOutput = s3g::FormatUpscaleLayout::Ring12;
      break;
    case MatrixPreset::QuadRing16:
      expansionInput = s3g::FormatUpscaleLayout::Quad;
      expansionOutput = s3g::FormatUpscaleLayout::Ring16;
      break;
    case MatrixPreset::QuadDoubleRing16:
      expansionInput = s3g::FormatUpscaleLayout::Quad;
      expansionOutput = s3g::FormatUpscaleLayout::DoubleRing16;
      break;
    case MatrixPreset::OctophonicRing16:
      expansionInput = s3g::FormatUpscaleLayout::OctophonicRing;
      expansionOutput = s3g::FormatUpscaleLayout::Ring16;
      break;
    case MatrixPreset::OctophonicRing32:
      expansionInput = s3g::FormatUpscaleLayout::OctophonicRing;
      expansionOutput = s3g::FormatUpscaleLayout::Ring32;
      break;
    case MatrixPreset::OctophonicDoubleRing16:
      expansionInput = s3g::FormatUpscaleLayout::OctophonicRing;
      expansionOutput = s3g::FormatUpscaleLayout::DoubleRing16;
      break;
    case MatrixPreset::OctophonicDoubleRing32:
      expansionInput = s3g::FormatUpscaleLayout::OctophonicRing;
      expansionOutput = s3g::FormatUpscaleLayout::DoubleRing32;
      break;
    default:
      expansionPreset = false;
      break;
    }
    if (expansionPreset) {
      _plugin->params.inputLayout = expansionInput;
      _plugin->params.outputLayout = expansionOutput;
      _plugin->params.basis = s3g::FormatUpscaleBasis::Direct;
      _plugin->params.placement = s3g::FormatUpscalePlacement::Nearest;
      _plugin->params.origin = s3g::FormatUpscaleOrigin::Remap;
      _plugin->params.amountPercent = 100.0f;
      _plugin->params.copies = std::max<uint32_t>(
          1u, s3g::formatUpscaleLayoutChannels(expansionOutput) /
                  std::max<uint32_t>(
                      1u, s3g::formatUpscaleLayoutChannels(expansionInput)));
      _plugin->autoRowShape = s3g::FormatUpscaleRowShape::Flat;
      _plugin->normalization = s3g::FormatUpscaleNormalization::Exact;
      applyParams(*_plugin);

      const auto &inputs = _plugin->dsp.inputLayout();
      const auto &outputs = _plugin->dsp.outputLayout();

      struct Candidate {
        float score = 0.0f;
        uint32_t input = 0u;
        uint32_t output = 0u;
      };
      std::array<Candidate, kChannels * kChannels> candidates{};
      uint32_t candidateCount = 0u;
      for (uint32_t input = 0u; input < inputs.count; ++input) {
        for (uint32_t output = 0u; output < outputs.count; ++output) {
          candidates[candidateCount++] = {
              s3g::formatUpscaleAngularDistance(
                  inputs.speakers[input].azimuthDeg,
                  outputs.speakers[output].azimuthDeg) +
                  std::abs(inputs.speakers[input].elevationDeg -
                           outputs.speakers[output].elevationDeg) *
                      0.75f,
              input,
              output,
          };
        }
      }
      std::sort(candidates.begin(), candidates.begin() + candidateCount,
                [](const Candidate &left, const Candidate &right) {
                  if (left.score != right.score)
                    return left.score < right.score;
                  if (left.output != right.output)
                    return left.output < right.output;
                  return left.input < right.input;
                });

      std::array<float, kChannels> tierHeights{};
      std::array<uint32_t, kChannels> tierCounts{};
      std::array<uint32_t, kChannels> outputTiers{};
      uint32_t tierCount = 0u;
      for (uint32_t output = 0u; output < outputs.count; ++output) {
        const float height =
            s3g::formatUpscaleSpeakerHeight(outputs.speakers[output]);
        uint32_t tier = 0u;
        while (tier < tierCount && std::abs(tierHeights[tier] - height) >=
                                       s3g::kFormatUpscaleTierHeightTolerance)
          ++tier;
        if (tier == tierCount) {
          tierHeights[tier] = height;
          ++tierCount;
        }
        outputTiers[output] = tier;
        ++tierCounts[tier];
      }

      std::array<uint32_t, kChannels> owner{};
      owner.fill(kChannels);
      std::array<uint32_t, kChannels> ownedCount{};
      bool orderedGroups = inputs.count > 0u;
      for (uint32_t tier = 0u; tier < tierCount; ++tier) {
        orderedGroups = orderedGroups && tierCounts[tier] >= inputs.count &&
                        tierCounts[tier] % inputs.count == 0u;
      }
      if (orderedGroups) {

        for (uint32_t tier = 0u; tier < tierCount; ++tier) {
          std::array<uint32_t, kChannels> tierOutputs{};
          uint32_t tierOutputCount = 0u;
          for (uint32_t output = 0u; output < outputs.count; ++output) {
            if (outputTiers[output] == tier)
              tierOutputs[tierOutputCount++] = output;
          }
          const uint32_t groupSize = tierOutputCount / inputs.count;
          std::array<bool, kChannels> inputUsed{};
          for (uint32_t group = 0u; group < inputs.count; ++group) {
            float sumX = 0.0f;
            float sumY = 0.0f;
            for (uint32_t offset = 0u; offset < groupSize; ++offset) {
              const uint32_t output = tierOutputs[group * groupSize + offset];
              const float radians = outputs.speakers[output].azimuthDeg *
                                    3.14159265358979323846f / 180.0f;
              sumX += std::cos(radians);
              sumY += std::sin(radians);
            }
            const float centerAzimuth =
                std::atan2(sumY, sumX) * 180.0f / 3.14159265358979323846f;
            uint32_t nearestInput = kChannels;
            float nearestScore = 1000000.0f;
            for (uint32_t input = 0u; input < inputs.count; ++input) {
              if (inputUsed[input])
                continue;
              const float score = s3g::formatUpscaleAngularDistance(
                  inputs.speakers[input].azimuthDeg, centerAzimuth);
              if (score < nearestScore) {
                nearestScore = score;
                nearestInput = input;
              }
            }
            if (nearestInput >= inputs.count)
              continue;
            inputUsed[nearestInput] = true;
            for (uint32_t offset = 0u; offset < groupSize; ++offset) {
              const uint32_t output = tierOutputs[group * groupSize + offset];
              owner[output] = nearestInput;
              ++ownedCount[nearestInput];
            }
          }
        }
      } else {
        std::array<uint32_t, kChannels * kChannels> tierQuota{};
        std::array<uint32_t, kChannels * kChannels> tierOwnedCount{};
        for (uint32_t tier = 0u; tier < tierCount; ++tier) {
          const uint32_t evenShare =
              tierCounts[tier] / std::max<uint32_t>(1u, inputs.count);
          const uint32_t remainder =
              tierCounts[tier] % std::max<uint32_t>(1u, inputs.count);
          for (uint32_t input = 0u; input < inputs.count; ++input) {
            tierQuota[tier * kChannels + input] =
                evenShare + (input < remainder ? 1u : 0u);
          }
        }
        for (uint32_t index = 0u; index < candidateCount; ++index) {
          const auto &candidate = candidates[index];
          const uint32_t tier = outputTiers[candidate.output];
          const uint32_t tierInput = tier * kChannels + candidate.input;
          if (owner[candidate.output] != kChannels ||
              tierOwnedCount[tierInput] >= tierQuota[tierInput])
            continue;
          owner[candidate.output] = candidate.input;
          ++ownedCount[candidate.input];
          ++tierOwnedCount[tierInput];
        }
        for (uint32_t output = 0u; output < outputs.count; ++output) {
          if (owner[output] != kChannels)
            continue;
          const uint32_t tier = outputTiers[output];
          for (uint32_t input = 0u; input < inputs.count; ++input) {
            const uint32_t tierInput = tier * kChannels + input;
            if (tierOwnedCount[tierInput] >= tierQuota[tierInput])
              continue;
            owner[output] = input;
            ++ownedCount[input];
            ++tierOwnedCount[tierInput];
            break;
          }
        }
      }
      std::array<float, kChannels * kChannels> weights{};
      for (uint32_t output = 0u; output < outputs.count; ++output) {
        const uint32_t input = owner[output];
        weights[input * kChannels + output] =
            1.0f / std::sqrt(static_cast<float>(
                       std::max<uint32_t>(1u, ownedCount[input])));
      }
      setExactManualMatrix(*_plugin, weights);
      finishPreset();
      return;
    }

    _plugin->params.inputLayout =
        selectedPreset == MatrixPreset::StereoQuadRepeat ||
                selectedPreset == MatrixPreset::StereoQuadCross ||
                selectedPreset == MatrixPreset::StereoQuadDifference ||
                selectedPreset == MatrixPreset::StereoFiveZeroSpread ||
                selectedPreset == MatrixPreset::StereoSevenZeroSpread
            ? s3g::FormatUpscaleLayout::Stereo
            : _plugin->params.inputLayout;
    if (selectedPreset == MatrixPreset::StereoQuadRepeat ||
        selectedPreset == MatrixPreset::StereoQuadCross ||
        selectedPreset == MatrixPreset::StereoQuadDifference)
      _plugin->params.outputLayout = s3g::FormatUpscaleLayout::Quad;
    else if (selectedPreset == MatrixPreset::StereoFiveZeroSpread)
      _plugin->params.outputLayout = s3g::FormatUpscaleLayout::FiveZero;
    else if (selectedPreset == MatrixPreset::StereoSevenZeroSpread)
      _plugin->params.outputLayout = s3g::FormatUpscaleLayout::SevenZero;
    _plugin->params.basis = s3g::FormatUpscaleBasis::Direct;
    _plugin->params.placement = s3g::FormatUpscalePlacement::SameSide;
    _plugin->params.origin = s3g::FormatUpscaleOrigin::Remap;
    _plugin->params.amountPercent = 100.0f;
    _plugin->normalization = s3g::FormatUpscaleNormalization::Exact;
    applyParams(*_plugin);

    const auto &inputLayout = _plugin->dsp.inputLayout();
    const auto &outputLayout = _plugin->dsp.outputLayout();
    auto nearest = [](const s3g::FormatUpscaleLayoutData &layout,
                      float azimuth) {
      uint32_t best = 0u;
      float bestScore = 1000000.0f;
      for (uint32_t index = 0u; index < layout.count; ++index) {
        const float score =
            s3g::formatUpscaleAngularDistance(layout.speakers[index].azimuthDeg,
                                              azimuth) +
            std::abs(layout.speakers[index].elevationDeg) * 1.5f;
        if (score < bestScore) {
          bestScore = score;
          best = index;
        }
      }
      return best;
    };
    const uint32_t right = nearest(inputLayout, -30.0f);
    const uint32_t left = nearest(inputLayout, 30.0f);
    std::array<float, kChannels * kChannels> weights{};
    auto set = [&](uint32_t input, uint32_t output, float gain) {
      weights[input * kChannels + output] = gain;
    };

    if (selectedPreset == MatrixPreset::StereoQuadRepeat ||
        selectedPreset == MatrixPreset::StereoQuadCross ||
        selectedPreset == MatrixPreset::StereoQuadDifference) {
      const uint32_t frontLeft = nearest(outputLayout, 45.0f);
      const uint32_t frontRight = nearest(outputLayout, -45.0f);
      const uint32_t rearRight = nearest(outputLayout, -135.0f);
      const uint32_t rearLeft = nearest(outputLayout, 135.0f);
      if (selectedPreset == MatrixPreset::StereoQuadRepeat) {
        constexpr float gain = 0.7071067811865475f;
        set(left, frontLeft, gain);
        set(left, rearLeft, gain);
        set(right, frontRight, gain);
        set(right, rearRight, gain);
      } else if (selectedPreset == MatrixPreset::StereoQuadCross) {
        constexpr float gain = 0.7071067811865475f;
        set(left, frontLeft, gain);
        set(left, rearRight, gain);
        set(right, frontRight, gain);
        set(right, rearLeft, gain);
      } else {

        constexpr float frontGain = 0.7071067811865475f;
        constexpr float differenceGain = 0.5f;
        set(left, frontLeft, frontGain);
        set(right, frontRight, frontGain);
        set(right, rearRight, differenceGain);
        set(left, rearRight, -differenceGain);
        set(left, rearLeft, differenceGain);
        set(right, rearLeft, -differenceGain);
      }
    } else {
      const bool seven = selectedPreset == MatrixPreset::StereoSevenZeroSpread;
      const float frontGain = seven ? 0.6324555320336759f : 0.7071067811865475f;
      const float otherGain = seven ? 0.4472135954999579f : 0.5f;
      set(right, nearest(outputLayout, -30.0f), frontGain);
      set(left, nearest(outputLayout, 30.0f), frontGain);
      set(right, nearest(outputLayout, -110.0f), otherGain);
      set(left, nearest(outputLayout, 110.0f), otherGain);
      if (seven) {
        set(right, nearest(outputLayout, -150.0f), otherGain);
        set(left, nearest(outputLayout, 150.0f), otherGain);
      }
      const uint32_t center = nearest(outputLayout, 0.0f);
      set(left, center, otherGain);
      set(right, center, otherGain);
    }
    setExactManualMatrix(*_plugin, weights);
    finishPreset();
  }

  void loadDocumentationThreeTierLayout() {
    if (!_plugin)
      return;
    _plugin->dsp.setCustomInputLayout(
        s3g::formatUpscaleDefaultCustomInputLayout());
    _plugin->dsp.setCustomOutputLayout(
        s3g::formatUpscaleDefaultThreeTierOutputLayout());
    _plugin->params.inputLayout = s3g::FormatUpscaleLayout::Custom;
    _plugin->params.outputLayout = s3g::FormatUpscaleLayout::Custom;
    _plugin->params.placement = s3g::FormatUpscalePlacement::Interleave;
    _plugin->params.copies = 3u;
    applyParams(*_plugin);
    std::array<uint8_t, kChannels * kChannels> routes{};
    for (uint32_t input = 0u; input < 3u; ++input) {
      for (uint32_t tier = 0u; tier < 3u; ++tier)
        routes[input * kChannels + tier * 3u + input] = 1u;
    }
    _plugin->dsp.setManualRoutes(routes, true);
    _page = 0u;
    _designMap = -1;
    _selectedInput = 0u;
    _selectedOutput = 0u;
    _selectionIsOutput = false;
    _matrixPreset = kCustomMatrixPreset;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                  "CUSTOM 3 x 9");
    updateCustomValueFields();
    invalid();
  }
};
Editor *documentationEditor(const clap_plugin_t *plugin) {
  auto *host = self(plugin)->portableGuiEditor;
  return host ? dynamic_cast<Editor *>(host->contentView()) : nullptr;
}
const s3g::gui_documentation::Extension documentation{
    [](const clap_plugin_t *plugin) {
      auto *v = documentationEditor(plugin);
      if (!v)
        return false;
      return v->edit([&] { v->loadDocumentationThreeTierLayout(); });
    },
    [](const clap_plugin_t *plugin, uint32_t page) {
      auto *v = documentationEditor(plugin);
      if (!v || page > 2)
        return false;
      if (page == 2) {
        if (!v->edit([&] { v->loadDocumentationCube41Layout(); }))
          return false;
      } else if (page == 0) {
        if (!v->edit([&] { v->loadDocumentationThreeTierLayout(); }))
          return false;
      }
      v->_page = page ? 1 : 0;
      v->_layoutOrigami = true;
      v->_designMap = -1;
      v->invalid();
      return true;
    },
    nullptr};
} // namespace upmix_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new upmix_canvas::Editor(p);
}
