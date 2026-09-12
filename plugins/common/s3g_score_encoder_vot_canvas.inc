// Literal Cocoa adaptation: original geometry, paths, draw order and hit maps.
namespace score_encoder_canvas {
namespace drawing = s3g::portable_gui::resonator_drawing;
namespace foundation = s3g::portable_gui::foundation;
using Kind = s3g::clap_gui::ParamEventKind;
constexpr s3g::gui_layout::Panel kSynthPanel{
    s3g::gui_layout::PluginClass::ProceduralEncoder,
    s3g::gui_layout::PanelRole::Engine,
    {630.0, 134.0, 250.0, 158.0},
    36.0,
    26.0,
    5u,
};
static_assert(s3g::gui_layout::sourceCardinalityControlMatches(
    kSynthPanel, s3g::gui_layout::SharedControlRole::SourceCardinality));
constexpr double kVoiceCountRowY = static_cast<double>(s3g::gui_layout::rowY(
    kSynthPanel, s3g::gui_layout::sourceCardinalityRow(
                     s3g::gui_layout::SharedControlRole::SourceCardinality)));

static VSTGUI::CColor votColor(int rgb, double alpha = 1.0) {
  return drawing::calibratedColor(rgb, alpha);
}

static float votLinearToSrgb(float value) {
  const float x = std::clamp(value, 0.0f, 1.0f);
  return x <= 0.0031308f ? x * 12.92f
                         : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

static VSTGUI::CColor votPointColor(float azimuthDeg, float elevationDeg,
                                    float distance, bool selected) {
  const float hue = std::fmod((azimuthDeg / 360.0f) + 1.0f, 1.0f);
  const float light = std::clamp(
      (std::clamp(elevationDeg, -90.0f, 90.0f) + 90.0f) / 180.0f, 0.30f, 0.86f);
  const float chroma = std::clamp(distance / 2.0f, 0.10f, 1.0f) * 0.34f;
  const float a = std::cos(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
  const float b = std::sin(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
  const float l3 = light + 0.3963377774f * a + 0.2158037573f * b;
  const float m3 = light - 0.1055613458f * a - 0.0638541728f * b;
  const float s3 = light - 0.0894841775f * a - 1.2914855480f * b;
  const float l = l3 * l3 * l3;
  const float m = m3 * m3 * m3;
  const float s = s3 * s3 * s3;
  float r = votLinearToSrgb(4.0767416621f * l - 3.3077115913f * m +
                            0.2309699292f * s);
  float g = votLinearToSrgb(-1.2684380046f * l + 2.6097574011f * m -
                            0.3413193965f * s);
  float blue = votLinearToSrgb(-0.0041960863f * l - 0.7034186147f * m +
                               1.7076147010f * s);
  const float grayMix = selected ? 0.06f : 0.16f;
  r = r * (1.0f - grayMix) + 0.72f * grayMix;
  g = g * (1.0f - grayMix) + 0.72f * grayMix;
  blue = blue * (1.0f - grayMix) + 0.72f * grayMix;
  return drawing::calibratedRGB(r, g, blue, selected ? 1.0 : 0.88);
}

static std::vector<float> readWavMono(const std::string &path) {
  std::vector<float> result;
  std::unique_ptr<FILE, decltype(&std::fclose)> file(
      foundation::openFileUtf8(path.c_str(), "rb"), &std::fclose);
  if (!file || std::fseek(file.get(), 0, SEEK_END) != 0)
    return result;
  const long length = std::ftell(file.get());
  if (length < 44 || length > 256 * 1024 * 1024 ||
      std::fseek(file.get(), 0, SEEK_SET) != 0)
    return result;
  std::vector<uint8_t> data(static_cast<size_t>(length));
  if (std::fread(data.data(), 1, data.size(), file.get()) != data.size())
    return result;
  const uint8_t *bytes = data.data();
  const size_t size = data.size();
  const auto u16 = [&](size_t offset) -> uint16_t {
    return offset + 1 < size
               ? static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8))
               : 0u;
  };
  const auto u32 = [&](size_t offset) -> uint32_t {
    return offset + 3 < size
               ? uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8) |
                     (uint32_t(bytes[offset + 2]) << 16) |
                     (uint32_t(bytes[offset + 3]) << 24)
               : 0u;
  };
  if (std::memcmp(bytes, "RIFF", 4) != 0 ||
      std::memcmp(bytes + 8, "WAVE", 4) != 0)
    return result;
  uint16_t audioFormat = 0;
  uint16_t channels = 0;
  uint16_t bits = 0;
  size_t dataOffset = 0;
  uint32_t dataSize = 0;
  for (size_t offset = 12; offset + 8 <= size;) {
    const uint32_t chunkSize = u32(offset + 4);
    if (chunkSize > size - offset - 8)
      return {};
    if (std::memcmp(bytes + offset, "fmt ", 4) == 0 && chunkSize >= 16) {
      audioFormat = u16(offset + 8);
      channels = u16(offset + 10);
      bits = u16(offset + 22);
    } else if (std::memcmp(bytes + offset, "data", 4) == 0) {
      dataOffset = offset + 8;
      dataSize = chunkSize;
    }
    offset += 8 + chunkSize + (chunkSize & 1u);
  }
  if (dataOffset == 0 || channels == 0)
    return result;
  if (!((audioFormat == 3 && bits == 32) ||
        (audioFormat == 1 && (bits == 16 || bits == 24 || bits == 32))))
    return {};
  const uint32_t bytesPerSample = bits / 8u;
  if (bytesPerSample == 0)
    return result;
  const uint32_t frameBytes = bytesPerSample * channels;
  const uint32_t frames = dataSize / std::max<uint32_t>(1u, frameBytes);
  result.reserve(frames);
  for (uint32_t frame = 0; frame < frames; ++frame) {
    const size_t offset = dataOffset + static_cast<size_t>(frame) * frameBytes;
    float sum = 0.0f;
    for (uint32_t channel = 0; channel < channels; ++channel) {
      const size_t p = offset + channel * bytesPerSample;
      float value = 0.0f;
      if (audioFormat == 3 && bits == 32 && p + 3 < size) {
        std::memcpy(&value, bytes + p, sizeof(float));
      } else if (audioFormat == 1 && bits == 16 && p + 1 < size) {
        const int16_t sample =
            static_cast<int16_t>(bytes[p] | (bytes[p + 1] << 8));
        value = static_cast<float>(sample) / 32768.0f;
      } else if (audioFormat == 1 && bits == 24 && p + 2 < size) {
        int32_t sample = static_cast<int32_t>(bytes[p] | (bytes[p + 1] << 8) |
                                              (bytes[p + 2] << 16));
        if ((sample & 0x800000) != 0)
          sample |= ~0xffffff;
        value = static_cast<float>(sample) / 8388608.0f;
      } else if (audioFormat == 1 && bits == 32 && p + 3 < size) {
        int32_t sample = 0;
        std::memcpy(&sample, bytes + p, sizeof(int32_t));
        value = static_cast<float>(sample) / 2147483648.0f;
      }
      if (!std::isfinite(value))
        return {};
      sum += value;
    }
    if (!std::isfinite(sum))
      return {};
    result.push_back(sum / static_cast<float>(channels));
  }
  return result;
}

class Editor final : public s3g::portable_gui::routing::Canvas {
public:
  Plugin *_plugin;

  clap_id _dragParam;
  int _dragArea;
  int _openMenu;
  int _hoverMenuItem;
  uint32_t _menuItemCount;
  drawing::Point _menuOrigin;
  double _menuWidth;
  int _leftPage;
  uint32_t _selectedVoice;
  uint32_t _selectedScoreNode;
  int _scoreDragLane;
  int _viewMode;
  double _viewAzDeg;
  double _viewElDeg;
  double _viewZoom;
  bool _dragView;
  drawing::Point _lastDragPoint;
  std::array<std::array<s3g::AmbiVotMotionPoint, 48>, s3g::kAmbiVotMaxVoices>
      _trails;
  uint32_t _trailHead;
  uint32_t _trailCount;
  char _titlePresetName[64];
  s3g::AmbiVotParams _snapshot{};
#include "s3g_score_encoder_editor.inc"
  Editor(Plugin &instance) : Canvas(kGuiWidth, kGuiHeight) {
    auto *plugin = &instance;

    _plugin = plugin;
    _dragParam = CLAP_INVALID_ID;
    _dragArea = 0;
    _openMenu = 0;
    _hoverMenuItem = -1;
    _menuItemCount = 0;
    _menuOrigin = drawing::makePoint(0, 0);
    _menuWidth = 124.0;
    _leftPage = plugin ? plugin->guiPage : 0;
    _selectedVoice = 0;
    _selectedScoreNode = 0;
    _scoreDragLane = -1;
    _viewMode = plugin ? plugin->guiViewMode : 0;
    _viewAzDeg = plugin ? plugin->guiViewAzDeg : 38.0;
    _viewElDeg = plugin ? plugin->guiViewElDeg : 32.0;
    _viewZoom = plugin ? plugin->guiViewZoom : 1.0;
    if (_viewMode == 0) {
      _viewAzDeg = 90.0;
      _viewElDeg = 0.0;
    } else if (_viewMode == 1) {
      _viewAzDeg = 90.0;
      _viewElDeg = 90.0;
    } else if (_viewMode == 2) {
      _viewAzDeg = 38.0;
      _viewElDeg = 32.0;
    }
    if (plugin && _viewMode >= 0 && _viewMode <= 2) {
      plugin->guiViewAzDeg = static_cast<float>(_viewAzDeg);
      plugin->guiViewElDeg = static_cast<float>(_viewElDeg);
    }
    _dragView = false;
    _lastDragPoint = drawing::makePoint(0, 0);
    _trailHead = 0;
    _trailCount = 0;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CURRENT");

    setWantsFocus(true);
    service();
  }
  ~Editor() override { stopRefresh(); }
  void service() override {
    _snapshot = portableParamsSnapshot(*_plugin);
    // Host state can be recalled with the editor still open.
    _leftPage = _plugin->guiPage;
    _viewMode = _plugin->guiViewMode;
    _viewAzDeg = _plugin->guiViewAzDeg;
    _viewElDeg = _plugin->guiViewElDeg;
    _viewZoom = _plugin->guiViewZoom;
    if (_viewMode == 0) {
      _viewAzDeg = 90.;
      _viewElDeg = 0.;
    } else if (_viewMode == 1) {
      _viewAzDeg = 90.;
      _viewElDeg = 90.;
    } else if (_viewMode == 2) {
      _viewAzDeg = 38.;
      _viewElDeg = 32.;
    }
    captureTrails();
  }
  void storeViewState() {
    if (!_plugin)
      return;
    _plugin->guiPage = _leftPage;
    _plugin->guiViewMode = _viewMode;
    _plugin->guiViewAzDeg = static_cast<float>(_viewAzDeg);
    _plugin->guiViewElDeg = static_cast<float>(_viewElDeg);
    _plugin->guiViewZoom = static_cast<float>(_viewZoom);
  }

  s3g::AmbiVotMotionPoint snapshotPoint(uint32_t index) {
    s3g::AmbiVotMotionPoint point{};
    if (!_plugin || index >= s3g::kAmbiVotMaxVoices)
      return point;
    point.azimuthDeg =
        _plugin->guiAzimuth[index].load(std::memory_order_relaxed);
    point.elevationDeg =
        _plugin->guiElevation[index].load(std::memory_order_relaxed);
    point.distance =
        _plugin->guiDistance[index].load(std::memory_order_relaxed);
    point.u = _plugin->guiU[index].load(std::memory_order_relaxed);
    point.v = _plugin->guiV[index].load(std::memory_order_relaxed);
    return point;
  }

  void captureTrails() {
    if (!_plugin)
      return;
    for (uint32_t i = 0; i < s3g::kAmbiVotMaxVoices; ++i)
      _trails[i][_trailHead] = snapshotPoint(i);
    _trailHead = (_trailHead + 1u) % 48u;
    _trailCount = std::min<uint32_t>(48u, _trailCount + 1u);
  }

  drawing::Rect leftPanelRect() { return drawing::makeRect(18, 42, 596, 834); }

  drawing::Rect leftContentRect() {
    return drawing::makeRect(34, 72, 564, 788);
  }

  drawing::Rect pageButtonRect(int index) {
    return drawing::makeRect(130.0 + static_cast<double>(index) * 58.0, 46.0,
                             52.0, 13.0);
  }

  drawing::Rect viewButtonRect(int index) {
    return drawing::makeRect(430.0 + static_cast<double>(index) * 49.0, 46.0,
                             43.0, 13.0);
  }

  drawing::Rect zoomButtonRect(int index) {
    return drawing::makeRect(378.0 + static_cast<double>(index) * 23.0, 46.0,
                             18.0, 13.0);
  }

  drawing::Rect synthLoadButtonRect() {
    return drawing::makeRect(824, 138, 48, 13);
  }

  drawing::Rect scoreRemoveButtonRect() {
    return drawing::makeRect(1032, 476, 18, 13);
  }

  drawing::Rect scoreAddButtonRect() {
    return drawing::makeRect(1054, 476, 18, 13);
  }

  drawing::Rect scoreResetButtonRect() {
    return drawing::makeRect(1080, 476, 54, 13);
  }

  void setViewPreset(int mode) {
    _viewMode = mode;
    if (mode == 0) {
      _viewAzDeg = 90.0;
      _viewElDeg = 0.0;
    } else if (mode == 1) {
      _viewAzDeg = 90.0;
      _viewElDeg = 90.0;
    } else {
      _viewAzDeg = 38.0;
      _viewElDeg = 32.0;
    }
    storeViewState();
    setNeedsDisplay(true);
  }

  double viewScaleForRect(drawing::Rect rect) {
    return std::min(rect.size.width, rect.size.height) * 0.25 *
           std::clamp(_viewZoom, 0.55, 2.20);
  }

  drawing::Point projectWorldPoint_rect_depth(s3g::Vec3 point,
                                              drawing::Rect rect,
                                              double *depth) {
    const double centerX = drawing::midX(rect);
    const double centerY = drawing::midY(rect);
    const double scale = viewScaleForRect(rect);
    if (_viewMode == 0) {
      if (depth)
        *depth = static_cast<double>(point.z);
      return drawing::makePoint(centerX - static_cast<double>(point.y) * scale,
                                centerY - static_cast<double>(point.x) * scale);
    }
    if (_viewMode == 1) {
      if (depth)
        *depth = static_cast<double>(point.x);
      return drawing::makePoint(centerX - static_cast<double>(point.y) * scale,
                                centerY - static_cast<double>(point.z) * scale);
    }
    const float azimuth = static_cast<float>(_viewAzDeg * M_PI / 180.0);
    const float elevation = static_cast<float>(_viewElDeg * M_PI / 180.0);
    const float ca = std::cos(azimuth);
    const float sa = std::sin(azimuth);
    const float ce = std::cos(elevation);
    const float se = std::sin(elevation);
    const float x1 = ca * point.x - sa * point.y;
    const float y1 = sa * point.x + ca * point.y;
    const float y2 = ce * y1 + se * point.z;
    const float z2 = -se * y1 + ce * point.z;
    if (depth)
      *depth = static_cast<double>(z2);
    return drawing::makePoint(centerX + static_cast<double>(x1) * scale,
                              centerY - static_cast<double>(y2) * scale);
  }

  drawing::Point projectWorldPointX_y_z(double x, double y, double z) {
    return projectWorldPoint_rect_depth(
        {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
        leftContentRect(), nullptr);
  }

  drawing::Point
  projectMotionPoint_rect_depth(const s3g::AmbiVotMotionPoint &point,
                                drawing::Rect rect, double *depth) {
    const s3g::Vec3 direction =
        s3g::directionFromAed(point.azimuthDeg, point.elevationDeg);
    return projectWorldPoint_rect_depth({direction.x * point.distance,
                                         direction.y * point.distance,
                                         direction.z * point.distance},
                                        rect, depth);
  }

  void drawPageButtons_style(drawing::Attrs attrs,
                             const drawing::Style &style) {
    static drawing::String labels[] = {"FIELD", "VECTOR", "SCORE"};
    const drawing::Rect header = drawing::makeRect(18, 42, 596, 21);
    for (int i = 0; i < 3; ++i) {
      drawing::drawToolboxHeaderButton(pageButtonRect(i), header, labels[i],
                                       i == _leftPage, attrs, style);
    }
  }

  void drawViewButtons_style(drawing::Attrs attrs,
                             const drawing::Style &style) {
    if (_leftPage != 0)
      return;
    static drawing::String labels[] = {"TOP", "SIDE", "3/4"};
    const drawing::Rect header = drawing::makeRect(18, 42, 596, 21);
    for (int i = 0; i < 3; ++i) {
      drawing::drawToolboxHeaderButton(viewButtonRect(i), header, labels[i],
                                       i == _viewMode, attrs, style);
    }
    drawing::drawToolboxHeaderButton(zoomButtonRect(0), header, "-", false,
                                     attrs, style);
    drawing::drawToolboxHeaderButton(zoomButtonRect(1), header, "+", false,
                                     attrs, style);
  }

  void drawField_attrs(drawing::Rect rect, drawing::Attrs attrs) {
    drawing::setFill(votColor(0x0b0b0b));
    drawing::rectFill(rect);
    drawing::setStroke(votColor(0x575757));
    drawing::frameRect(rect);
    drawing::GraphicsScope graphics(*drawing::current->context);
    drawing::clipRect(drawing::inset(rect, 1, 1));

    const double radius = viewScaleForRect(rect);
    drawing::setStroke(votColor(0x343434));
    drawing::Path sphere =
        drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
            drawing::midX(rect) - radius, drawing::midY(rect) - radius,
            radius * 2.0, radius * 2.0));
    (sphere).setLineWidth(0.8);
    (sphere).stroke();
    drawing::drawAmbisonicOrientationGuides([&](double x, double y, double z) {
      return projectWorldPoint_rect_depth(
          {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
          rect, nullptr);
    });

    const uint32_t voices =
        std::clamp<uint32_t>(_snapshot.voices, 1u, s3g::kAmbiVotMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u);
    struct ProjectedPoint {
      uint32_t index;
      double depth;
      drawing::Point point;
      s3g::Vec3 world;
      s3g::AmbiVotMotionPoint motion;
    };
    std::array<ProjectedPoint, s3g::kAmbiVotMaxVoices> projected{};
    for (uint32_t voice = 0; voice < voices; ++voice) {
      projected[voice].index = voice;
      projected[voice].motion = snapshotPoint(voice);
      const s3g::Vec3 direction =
          s3g::directionFromAed(projected[voice].motion.azimuthDeg,
                                projected[voice].motion.elevationDeg);
      projected[voice].world = {
          direction.x * projected[voice].motion.distance,
          direction.y * projected[voice].motion.distance,
          direction.z * projected[voice].motion.distance,
      };
      projected[voice].point = projectWorldPoint_rect_depth(
          projected[voice].world, rect, &projected[voice].depth);
    }
    drawing::Path weakEdges = drawing::Path::bezierPath();
    drawing::Path mediumEdges = drawing::Path::bezierPath();
    drawing::Path strongEdges = drawing::Path::bezierPath();
    drawing::Path selectedEdges = drawing::Path::bezierPath();
    const float neighborRadius = std::max(0.05f, _snapshot.neighborRadius);
    for (uint32_t a = 0; a < voices; ++a) {
      for (uint32_t b = a + 1u; b < voices; ++b) {
        const float dx = projected[a].world.x - projected[b].world.x;
        const float dy = projected[a].world.y - projected[b].world.y;
        const float dz = projected[a].world.z - projected[b].world.z;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance > neighborRadius)
          continue;
        const float proximity = 1.0f - distance / neighborRadius;
        drawing::Path edge =
            proximity > 0.66f ? strongEdges
                              : (proximity > 0.33f ? mediumEdges : weakEdges);
        (edge).moveToPoint(projected[a].point);
        (edge).lineToPoint(projected[b].point);
        if (a == _selectedVoice || b == _selectedVoice) {
          (selectedEdges).moveToPoint(projected[a].point);
          (selectedEdges).lineToPoint(projected[b].point);
        }
      }
    }
    drawing::setStroke(votColor(0x707070, 0.22));
    (weakEdges).setLineWidth(0.65);
    (weakEdges).stroke();
    drawing::setStroke(votColor(0x8a8a8a, 0.34));
    (mediumEdges).setLineWidth(0.85);
    (mediumEdges).stroke();
    drawing::setStroke(votColor(0xb0b0b0, 0.52));
    (strongEdges).setLineWidth(1.05);
    (strongEdges).stroke();
    const auto selectedMotion = snapshotPoint(_selectedVoice);
    drawing::setStroke(drawing::withAlpha(
        votPointColor(selectedMotion.azimuthDeg, selectedMotion.elevationDeg,
                      selectedMotion.distance, true),
        0.70));
    (selectedEdges).setLineWidth(1.35);
    (selectedEdges).stroke();

    std::sort(projected.begin(), projected.begin() + voices,
              [](const ProjectedPoint &a, const ProjectedPoint &b) {
                return a.depth < b.depth;
              });
    drawing::Attrs idAttrs =
        drawing::textAttrs(votColor(0x0b0b0b), voices > 32u ? 5.5 : 7.0);
    for (uint32_t drawIndex = 0; drawIndex < voices; ++drawIndex) {
      const auto &item = projected[drawIndex];
      const bool selected = item.index == _selectedVoice;
      const bool gated = _plugin->guiNeighborGate[item.index].load(
                             std::memory_order_relaxed) != 0u;
      const double size = selected ? 15.0 : (voices > 32u ? 9.0 : 12.0);
      const drawing::Rect pointRect = drawing::makeRect(
          item.point.x - size * 0.5, item.point.y - size * 0.5, size, size);
      drawing::setFill(drawing::withAlpha(
          votPointColor(item.motion.azimuthDeg, item.motion.elevationDeg,
                        item.motion.distance, selected),
          (gated || _snapshot.mode == s3g::AmbiVotMode::Midi) ? 0.96 : 0.42));
      drawing::rectFill(pointRect);
      drawing::setStroke(votColor(selected ? 0xe0e0e0 : 0x111111));
      drawing::frameRect(pointRect);
      drawing::String label = drawing::stringFormat("%u", item.index + 1u);
      const drawing::Size labelSize =
          drawing::sizeWithAttributes(label, idAttrs);
      drawing::drawAtPoint_withAttributes(
          label,
          drawing::makePoint(drawing::midX(pointRect) - labelSize.width * 0.5,
                             drawing::midY(pointRect) - labelSize.height * 0.5 -
                                 0.5),
          idAttrs);
    }
    graphics.restore();

    const auto selected = snapshotPoint(_selectedVoice);
    const uint32_t neighbors = _plugin->guiNeighborCount[_selectedVoice].load(
        std::memory_order_relaxed);
    const bool gated = _plugin->guiNeighborGate[_selectedVoice].load(
                           std::memory_order_relaxed) != 0u;
    drawing::String readout = drawing::stringFormat(
        "V%u   AZ %+06.1f   EL %+05.1f   D %.2f   N %u/%u   %@",
        _selectedVoice + 1u, selected.azimuthDeg, selected.elevationDeg,
        selected.distance, neighbors, _snapshot.requiredNeighbors,
        gated ? "ON" : "OFF");
    drawing::drawAtPoint_withAttributes(
        readout, drawing::makePoint(rect.origin.x + 10, rect.origin.y + 9),
        attrs);
  }

  void drawMiniWave_rect_color(
      const std::array<float, s3g::kAmbiVotTableSize> &table,
      drawing::Rect rect, VSTGUI::CColor color) {
    drawing::Path path = drawing::Path::bezierPath();
    for (uint32_t i = 0; i < s3g::kAmbiVotTableSize; ++i) {
      const double x =
          rect.origin.x + static_cast<double>(i) /
                              static_cast<double>(s3g::kAmbiVotTableSize - 1u) *
                              rect.size.width;
      const double y = drawing::midY(rect) -
                       static_cast<double>(table[i]) * rect.size.height * 0.40;
      if (i == 0u)
        (path).moveToPoint(drawing::makePoint(x, y));
      else
        (path).lineToPoint(drawing::makePoint(x, y));
    }
    drawing::setStroke(color);
    (path).setLineWidth(0.75);
    (path).stroke();
  }

  void drawVectorPage_attrs_valueAttrs_style(drawing::Rect rect,
                                             drawing::Attrs attrs,
                                             drawing::Attrs valueAttrs,
                                             const drawing::Style &style) {
    auto bank = displayBank();
    if (!bank)
      return;
    const drawing::Rect bankRect =
        drawing::makeRect(rect.origin.x + 8, rect.origin.y + 8, 252, 252);
    const drawing::Rect pathRect =
        drawing::makeRect(rect.origin.x + 278, rect.origin.y + 8, 270, 252);
    drawing::setFill(votColor(0x0b0b0b));
    drawing::rectFill(bankRect);
    drawing::rectFill(pathRect);
    drawing::setStroke(votColor(0x575757));
    drawing::frameRect(bankRect);
    drawing::frameRect(pathRect);
    const uint32_t voices =
        std::clamp<uint32_t>(_snapshot.voices, 1u, s3g::kAmbiVotMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u);
    const auto selected = snapshotPoint(_selectedVoice);
    const auto vectorWeights =
        s3g::ambiVotVectorWeights(selected.u, selected.v, _snapshot.morph);
    const float maxWeight = *std::max_element(vectorWeights.values.begin(),
                                              vectorWeights.values.end());
    const double cellW = bankRect.size.width / 4.0;
    const double cellH = bankRect.size.height / 4.0;
    for (uint32_t displayRow = 0; displayRow < 4u; ++displayRow) {
      const uint32_t row = 3u - displayRow;
      for (uint32_t column = 0; column < 4u; ++column) {
        const uint32_t table = row * 4u + column;
        const drawing::Rect cell = drawing::makeRect(
            bankRect.origin.x + column * cellW,
            bankRect.origin.y + displayRow * cellH, cellW, cellH);
        const double influence = std::sqrt(vectorWeights.values[table] /
                                           std::max(0.000001f, maxWeight));
        drawing::setFill(
            drawing::calibratedWhite(0.055 + influence * 0.15, 1.0));
        drawing::rectFill(drawing::inset(cell, 1, 1));
        drawing::setStroke(votColor(0x303030));
        drawing::frameRect(cell);
        drawMiniWave_rect_color(
            bank->tables[table], drawing::inset(cell, 5, 8),
            drawing::calibratedWhite(0.38 + influence * 0.35, 1.0));
      }
    }
    drawing::drawAtPoint_withAttributes(
        "TABLE BANK",
        drawing::makePoint(bankRect.origin.x + 8, bankRect.origin.y + 7),
        attrs);
    drawing::drawAtPoint_withAttributes(
        "VECTOR PATH",
        drawing::makePoint(pathRect.origin.x + 8, pathRect.origin.y + 7),
        attrs);

    const drawing::Rect pathInner = drawing::inset(pathRect, 14, 28);
    for (int i = 1; i < 4; ++i) {
      drawing::setStroke(votColor(0x303030));
      drawing::Path::strokeLineFromPoint_toPoint(
          drawing::makePoint(pathInner.origin.x +
                                 pathInner.size.width * i / 4.0,
                             pathInner.origin.y),
          drawing::makePoint(pathInner.origin.x +
                                 pathInner.size.width * i / 4.0,
                             drawing::maxY(pathInner)));
      drawing::Path::strokeLineFromPoint_toPoint(
          drawing::makePoint(pathInner.origin.x,
                             pathInner.origin.y +
                                 pathInner.size.height * i / 4.0),
          drawing::makePoint(drawing::maxX(pathInner),
                             pathInner.origin.y +
                                 pathInner.size.height * i / 4.0));
    }
    const double hillRadiusX =
        vectorWeights.sigmaCells * pathInner.size.width / 4.0;
    const double hillRadiusY =
        vectorWeights.sigmaCells * pathInner.size.height / 4.0;
    for (uint32_t row = 0; row < 4u; ++row) {
      for (uint32_t column = 0; column < 4u; ++column) {
        const uint32_t table = row * 4u + column;
        const double centerU = (static_cast<double>(column) + 0.5) / 4.0;
        const double centerV = (static_cast<double>(row) + 0.5) / 4.0;
        const drawing::Point center = drawing::makePoint(
            pathInner.origin.x + centerU * pathInner.size.width,
            pathInner.origin.y + (1.0 - centerV) * pathInner.size.height);
        drawing::setStroke(votColor(0x3b3b3b));
        drawing::Path hill = drawing::Path::bezierPathWithOvalInRect(
            drawing::makeRect(center.x - hillRadiusX, center.y - hillRadiusY,
                              hillRadiusX * 2.0, hillRadiusY * 2.0));
        (hill).setLineWidth(0.65);
        (hill).stroke();
        const double influence = std::sqrt(vectorWeights.values[table] /
                                           std::max(0.000001f, maxWeight));
        drawing::setFill(
            drawing::calibratedWhite(0.26 + influence * 0.50, 1.0));
        drawing::rectFill(
            drawing::makeRect(center.x - 2.0, center.y - 2.0, 4.0, 4.0));
      }
    }
    const bool vectorMoving =
        _snapshot.motionScene != s3g::AmbiVotMotionScene::Manual;
    const float vectorCoverage = vectorMoving ? _snapshot.scan : 0.0f;
    if (vectorCoverage > 0.001f) {
      const float linkedU =
          0.5f + 0.5f * std::sin(selected.azimuthDeg * s3g::kPi / 180.0f);
      const float linkedV =
          std::clamp((selected.elevationDeg + 90.0f) / 180.0f, 0.0f, 1.0f);
      const float centerU =
          s3g::lerp(_snapshot.vectorX, linkedU, _snapshot.motionLink);
      const float centerV =
          s3g::lerp(_snapshot.vectorY, linkedV, _snapshot.motionLink);
      const float minimumU = centerU * (1.0f - vectorCoverage);
      const float maximumV = centerV * (1.0f - vectorCoverage) + vectorCoverage;
      const drawing::Rect coverageRect = drawing::makeRect(
          pathInner.origin.x + minimumU * pathInner.size.width,
          pathInner.origin.y + (1.0f - maximumV) * pathInner.size.height,
          vectorCoverage * pathInner.size.width,
          vectorCoverage * pathInner.size.height);
      drawing::setStroke(votColor(0x666666));
      drawing::frameRect(coverageRect);
    }
    if (_trailCount > 1u) {
      drawing::Path path = drawing::Path::bezierPath();
      for (uint32_t step = 0; step < _trailCount; ++step) {
        const uint32_t index = (_trailHead + 48u - _trailCount + step) % 48u;
        const auto &point = _trails[_selectedVoice][index];
        const drawing::Point p = drawing::makePoint(
            pathInner.origin.x + point.u * pathInner.size.width,
            pathInner.origin.y + (1.0f - point.v) * pathInner.size.height);
        if (step == 0u)
          (path).moveToPoint(p);
        else
          (path).lineToPoint(p);
      }
      const auto point = snapshotPoint(_selectedVoice);
      drawing::setStroke(
          drawing::withAlpha(votPointColor(point.azimuthDeg, point.elevationDeg,
                                           point.distance, true),
                             0.64));
      (path).setLineWidth(1.4);
      (path).stroke();
    }
    drawing::Attrs idAttrs =
        drawing::textAttrs(votColor(0x0b0b0b), voices > 32u ? 5.5 : 7.0);
    for (uint32_t voice = 0; voice < voices; ++voice) {
      const auto point = snapshotPoint(voice);
      const bool selected = voice == _selectedVoice;
      const double size = selected ? 14.0 : (voices > 32u ? 8.0 : 11.0);
      const drawing::Point p = drawing::makePoint(
          pathInner.origin.x + point.u * pathInner.size.width,
          pathInner.origin.y + (1.0f - point.v) * pathInner.size.height);
      const drawing::Rect marker =
          drawing::makeRect(p.x - size * 0.5, p.y - size * 0.5, size, size);
      drawing::setFill(votPointColor(point.azimuthDeg, point.elevationDeg,
                                     point.distance, selected));
      drawing::rectFill(marker);
      drawing::setStroke(votColor(selected ? 0xe0e0e0 : 0x111111));
      drawing::frameRect(marker);
      drawing::String label = drawing::stringFormat("%u", voice + 1u);
      const drawing::Size sizeText =
          drawing::sizeWithAttributes(label, idAttrs);
      drawing::drawAtPoint_withAttributes(
          label,
          drawing::makePoint(drawing::midX(marker) - sizeText.width * 0.5,
                             drawing::midY(marker) - sizeText.height * 0.5 -
                                 0.5),
          idAttrs);
    }

    const drawing::Rect waveRect =
        drawing::makeRect(rect.origin.x + 8, rect.origin.y + 278, 540, 300);
    drawing::setFill(votColor(0x0b0b0b));
    drawing::rectFill(waveRect);
    drawing::setStroke(votColor(0x575757));
    drawing::frameRect(waveRect);
    drawing::drawAtPoint_withAttributes(
        "INTERPOLATED WAVEFORM",
        drawing::makePoint(waveRect.origin.x + 8, waveRect.origin.y + 7),
        attrs);
    drawing::setStroke(votColor(0x2d2d2d));
    drawing::Path::strokeLineFromPoint_toPoint(
        drawing::makePoint(waveRect.origin.x + 8, drawing::midY(waveRect)),
        drawing::makePoint(drawing::maxX(waveRect) - 8,
                           drawing::midY(waveRect)));
    drawing::Path waveform = drawing::Path::bezierPath();
    constexpr uint32_t samples = 512;
    for (uint32_t i = 0; i < samples; ++i) {
      const float phase =
          static_cast<float>(i) / static_cast<float>(samples - 1u);
      const float value = s3g::ambiVotVectorSample(*bank, vectorWeights, phase);
      const double x =
          waveRect.origin.x + 10.0 + phase * (waveRect.size.width - 20.0);
      const double y =
          drawing::midY(waveRect) -
          static_cast<double>(value) * (waveRect.size.height * 0.39);
      if (i == 0u)
        (waveform).moveToPoint(drawing::makePoint(x, y));
      else
        (waveform).lineToPoint(drawing::makePoint(x, y));
    }
    drawing::setStroke(votPointColor(selected.azimuthDeg, selected.elevationDeg,
                                     selected.distance, true));
    (waveform).setLineWidth(1.25);
    (waveform).stroke();
    drawing::String waveformReadout = drawing::stringFormat(
        "V%u   U %.3f   V %.3f   LINK %.0f%%", _selectedVoice + 1u, selected.u,
        selected.v, _snapshot.motionLink * 100.0f);
    const drawing::Size waveformReadoutSize =
        drawing::sizeWithAttributes(waveformReadout, valueAttrs);
    drawing::drawAtPoint_withAttributes(
        waveformReadout,
        drawing::makePoint(drawing::maxX(waveRect) - waveformReadoutSize.width -
                               8,
                           waveRect.origin.y + 7),
        valueAttrs);

    drawing::drawSlider("X", drawing::stringFormat("%.2f", _snapshot.vectorX),
                        _snapshot.vectorX, rect.origin.y + 606, attrs,
                        valueAttrs, style, rect.origin.x + 8,
                        rect.origin.x + 86, rect.origin.x + 270, 170);
    drawing::drawSlider("Y", drawing::stringFormat("%.2f", _snapshot.vectorY),
                        _snapshot.vectorY, rect.origin.y + 632, attrs,
                        valueAttrs, style, rect.origin.x + 8,
                        rect.origin.x + 86, rect.origin.x + 270, 170);
    drawing::drawSlider(
        "SCAN", drawing::stringFormat("%.0f%%", _snapshot.scan * 100.0f),
        _snapshot.scan, rect.origin.y + 658, attrs, valueAttrs, style,
        rect.origin.x + 8, rect.origin.x + 86, rect.origin.x + 270, 170);
    drawing::drawSlider(
        "MORPH", drawing::stringFormat("%.0f%%", _snapshot.morph * 100.0f),
        _snapshot.morph, rect.origin.y + 684, attrs, valueAttrs, style,
        rect.origin.x + 8, rect.origin.x + 86, rect.origin.x + 270, 170);
  }

  void drawScorePage_attrs_valueAttrs_style(drawing::Rect rect,
                                            drawing::Attrs attrs,
                                            drawing::Attrs valueAttrs,
                                            const drawing::Style &style) {
    const auto score = loadScore(*_plugin);
    const uint32_t count = score.nodeCount;
    _selectedScoreNode = std::min<uint32_t>(_selectedScoreNode, count - 1u);
    const drawing::Rect timelineRect =
        drawing::makeRect(rect.origin.x + 8, rect.origin.y + 8, 540, 306);
    const drawing::Rect routeRect =
        drawing::makeRect(rect.origin.x + 8, rect.origin.y + 328, 540, 292);
    drawing::setFill(votColor(0x0b0b0b));
    drawing::rectFill(timelineRect);
    drawing::rectFill(routeRect);
    drawing::setStroke(votColor(0x575757));
    drawing::frameRect(timelineRect);
    drawing::frameRect(routeRect);
    drawing::drawAtPoint_withAttributes(
        "VECTOR SCORE",
        drawing::makePoint(timelineRect.origin.x + 8,
                           timelineRect.origin.y + 7),
        attrs);
    drawing::String scoreStatus = drawing::stringFormat(
        "%u NODES   %@   %.2f S", count,
        drawing::stringFromUtf8(s3g::ambiVotScoreModeName(_snapshot.scoreMode)),
        _snapshot.scoreDurationSec);
    const drawing::Size statusSize =
        drawing::sizeWithAttributes(scoreStatus, valueAttrs);
    drawing::drawAtPoint_withAttributes(
        scoreStatus,
        drawing::makePoint(drawing::maxX(timelineRect) - statusSize.width - 8,
                           timelineRect.origin.y + 7),
        valueAttrs);

    const drawing::Rect uLane = drawing::makeRect(
        timelineRect.origin.x + 14, timelineRect.origin.y + 34,
        timelineRect.size.width - 28, 112);
    const drawing::Rect vLane = drawing::makeRect(
        timelineRect.origin.x + 14, timelineRect.origin.y + 174,
        timelineRect.size.width - 28, 112);
    const float sustainStart = score.nodes[score.sustainStart].time;
    const float sustainEnd = score.nodes[score.sustainEnd].time;
    for (const auto lane : {uLane, vLane}) {
      drawing::setFill(drawing::calibratedWhite(0.12, 1.0));
      drawing::rectFill(drawing::makeRect(
          lane.origin.x + sustainStart * lane.size.width, lane.origin.y,
          (sustainEnd - sustainStart) * lane.size.width, lane.size.height));
      drawing::setStroke(votColor(0x343434));
      drawing::frameRect(lane);
      for (int division = 1; division < 4; ++division) {
        drawing::Path::strokeLineFromPoint_toPoint(
            drawing::makePoint(lane.origin.x + lane.size.width * division / 4.0,
                               lane.origin.y),
            drawing::makePoint(lane.origin.x + lane.size.width * division / 4.0,
                               drawing::maxY(lane)));
      }
      drawing::Path::strokeLineFromPoint_toPoint(
          drawing::makePoint(lane.origin.x, drawing::midY(lane)),
          drawing::makePoint(drawing::maxX(lane), drawing::midY(lane)));
    }
    drawing::drawAtPoint_withAttributes(
        "U", drawing::makePoint(uLane.origin.x + 6, uLane.origin.y + 5), attrs);
    drawing::drawAtPoint_withAttributes(
        "V", drawing::makePoint(vLane.origin.x + 6, vLane.origin.y + 5), attrs);

    auto drawLane = [&](drawing::Rect lane, bool drawU, VSTGUI::CColor color) {
      drawing::Path path = drawing::Path::bezierPath();
      bool first = true;
      for (uint32_t segment = 0; segment + 1u < count; ++segment) {
        const auto &from = score.nodes[segment];
        const auto &to = score.nodes[segment + 1u];
        for (uint32_t step = 0; step <= 20u; ++step) {
          const float local = static_cast<float>(step) / 20.0f;
          const float interpolation =
              s3g::ambiVotScoreCurveValue(from.curve, local);
          const float time = s3g::lerp(from.time, to.time, local);
          const float value = s3g::lerp(drawU ? from.u : from.v,
                                        drawU ? to.u : to.v, interpolation);
          const drawing::Point point = drawing::makePoint(
              lane.origin.x + time * lane.size.width,
              lane.origin.y + (1.0f - value) * lane.size.height);
          if (first) {
            (path).moveToPoint(point);
            first = false;
          } else
            (path).lineToPoint(point);
        }
      }
      drawing::setStroke(color);
      (path).setLineWidth(1.2);
      (path).stroke();
      for (uint32_t node = 0; node < count; ++node) {
        const float value = drawU ? score.nodes[node].u : score.nodes[node].v;
        const drawing::Point point = drawing::makePoint(
            lane.origin.x + score.nodes[node].time * lane.size.width,
            lane.origin.y + (1.0f - value) * lane.size.height);
        const double size = node == _selectedScoreNode ? 9.0 : 7.0;
        drawing::setFill(drawing::calibratedWhite(
            node == _selectedScoreNode ? 0.88 : 0.58, 1.0));
        drawing::rectFill(drawing::makeRect(point.x - size * 0.5,
                                            point.y - size * 0.5, size, size));
      }
    };
    drawLane(uLane, true, votColor(0xd0d0d0));
    drawLane(vLane, false, votColor(0x858585));

    drawing::drawAtPoint_withAttributes(
        "FIELD ROUTE",
        drawing::makePoint(routeRect.origin.x + 8, routeRect.origin.y + 7),
        attrs);
    const drawing::Rect routeInner = drawing::makeRect(
        routeRect.origin.x + 14, routeRect.origin.y + 30,
        routeRect.size.width - 28, routeRect.size.height - 44);
    drawing::setStroke(votColor(0x303030));
    for (int division = 1; division < 4; ++division) {
      drawing::Path::strokeLineFromPoint_toPoint(
          drawing::makePoint(routeInner.origin.x +
                                 routeInner.size.width * division / 4.0,
                             routeInner.origin.y),
          drawing::makePoint(routeInner.origin.x +
                                 routeInner.size.width * division / 4.0,
                             drawing::maxY(routeInner)));
      drawing::Path::strokeLineFromPoint_toPoint(
          drawing::makePoint(routeInner.origin.x,
                             routeInner.origin.y +
                                 routeInner.size.height * division / 4.0),
          drawing::makePoint(drawing::maxX(routeInner),
                             routeInner.origin.y +
                                 routeInner.size.height * division / 4.0));
    }
    drawing::Path route = drawing::Path::bezierPath();
    for (uint32_t node = 0; node < count; ++node) {
      const drawing::Point point = drawing::makePoint(
          routeInner.origin.x + score.nodes[node].u * routeInner.size.width,
          routeInner.origin.y +
              (1.0f - score.nodes[node].v) * routeInner.size.height);
      if (node == 0u)
        (route).moveToPoint(point);
      else
        (route).lineToPoint(point);
    }
    drawing::setStroke(votColor(0x949494));
    (route).setLineWidth(1.15);
    (route).stroke();
    drawing::Attrs nodeAttrs = drawing::textAttrs(votColor(0x0b0b0b), 7.0);
    for (uint32_t node = 0; node < count; ++node) {
      const drawing::Point point = drawing::makePoint(
          routeInner.origin.x + score.nodes[node].u * routeInner.size.width,
          routeInner.origin.y +
              (1.0f - score.nodes[node].v) * routeInner.size.height);
      const double size = node == _selectedScoreNode ? 14.0 : 11.0;
      drawing::setFill(drawing::calibratedWhite(
          node == _selectedScoreNode ? 0.90 : 0.62, 1.0));
      const drawing::Rect marker = drawing::makeRect(
          point.x - size * 0.5, point.y - size * 0.5, size, size);
      drawing::rectFill(marker);
      drawing::setStroke(votColor(0x151515));
      drawing::frameRect(marker);
      drawing::String label = drawing::stringFormat("%u", node + 1u);
      const drawing::Size labelSize =
          drawing::sizeWithAttributes(label, nodeAttrs);
      drawing::drawAtPoint_withAttributes(
          label,
          drawing::makePoint(drawing::midX(marker) - labelSize.width * 0.5,
                             drawing::midY(marker) - labelSize.height * 0.5 -
                                 0.5),
          nodeAttrs);
    }

    const auto &selected = score.nodes[_selectedScoreNode];
    drawing::drawSlider("TIME", drawing::stringFormat("%.3f", selected.time),
                        selected.time, rect.origin.y + 648, attrs, valueAttrs,
                        style, rect.origin.x + 8, rect.origin.x + 86,
                        rect.origin.x + 270, 170);
    drawing::drawSlider("U", drawing::stringFormat("%.3f", selected.u),
                        selected.u, rect.origin.y + 674, attrs, valueAttrs,
                        style, rect.origin.x + 8, rect.origin.x + 86,
                        rect.origin.x + 270, 170);
    drawing::drawSlider("V", drawing::stringFormat("%.3f", selected.v),
                        selected.v, rect.origin.y + 700, attrs, valueAttrs,
                        style, rect.origin.x + 8, rect.origin.x + 86,
                        rect.origin.x + 270, 170);
    drawing::drawMenu(
        "CURVE",
        drawing::stringFromUtf8(s3g::ambiVotScoreCurveName(selected.curve)),
        rect.origin.y + 726, attrs, valueAttrs, style, rect.origin.x + 8,
        rect.origin.x + 86, 184);
  }

  void drawMenu_value_y_attrs_valueAttrs_style(drawing::String name,
                                               drawing::String value, double y,
                                               drawing::Attrs attrs,
                                               drawing::Attrs valueAttrs,
                                               const drawing::Style &style) {
    drawMenuAtX_name_value_y_attrs_valueAttrs_style(630, name, value, y, attrs,
                                                    valueAttrs, style);
  }

  void drawMenuAtX_name_value_y_attrs_valueAttrs_style(
      double panelX, drawing::String name, drawing::String value, double y,
      drawing::Attrs attrs, drawing::Attrs valueAttrs,
      const drawing::Style &style) {
    drawing::drawMenu(name, value, y, attrs, valueAttrs, style, panelX + 16,
                      panelX + 108, 124);
  }

  void drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
      drawing::String name, clap_id param, double value, double min, double max,
      double y, drawing::Attrs attrs, drawing::Attrs valueAttrs,
      const drawing::Style &style) {
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        630, name, param, value, min, max, y, attrs, valueAttrs, style);
  }

  void drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
      double panelX, drawing::String name, clap_id param, double value,
      double min, double max, double y, drawing::Attrs attrs,
      drawing::Attrs valueAttrs, const drawing::Style &style) {
    double norm = (value - min) / std::max(0.000001, max - min);
    if (param == kCenterAzimuthParamId)
      norm = s3g::aedAzimuthSliderNorm(static_cast<float>(value));
    else if (param == kMotionRateParamId || param == kScoreDurationParamId)
      norm = std::log(value / min) / std::log(max / min);
    else if (param == kAttackParamId || param == kDecayParamId ||
             param == kReleaseParamId)
      norm = std::log(value / min) / std::log(max / min);
    char text[64]{};
    paramsValueToText(nullptr, param, value, text, sizeof(text));
    drawing::drawSlider(name, drawing::stringFromUtf8(text), norm, y, attrs,
                        valueAttrs, style, panelX + 16, panelX + 108,
                        panelX + 196, 82);
  }

  void drawPanels_valueAttrs_style(drawing::Attrs attrs,
                                   drawing::Attrs valueAttrs,
                                   const drawing::Style &style) {
    const auto p = _snapshot;
    drawing::drawPanelFrame(630, 42, 250, 80, style);
    drawing::drawPanelHeader("OUTPUT", true, 630, 42, 250, 21, attrs, style);
    drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
        "OUT", kOutputParamId, p.outputGainDb, -60, 12, 78, attrs, valueAttrs,
        style);
    drawMenu_value_y_attrs_valueAttrs_style(
        "ORDER", drawing::stringFormat("%uOA", p.order), 104, attrs, valueAttrs,
        style);

    drawing::drawPanelFrame(630, 134, 250, 158, style);
    drawing::drawPanelHeader("SOURCE / SYNTH", true, 630, 134, 250, 21, attrs,
                             style);
    const drawing::Rect synthHeader = drawing::makeRect(630, 134, 250, 21);
    drawing::drawToolboxHeaderActionButton(synthLoadButtonRect(), synthHeader,
                                           "LOAD", attrs, style);
    drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
        "VOICES", kVoicesParamId, p.voices, 1, 64, kVoiceCountRowY, attrs,
        valueAttrs, style);
    drawMenu_value_y_attrs_valueAttrs_style(
        "MODE", drawing::stringFromUtf8(s3g::ambiVotModeName(p.mode)), 196,
        attrs, valueAttrs, style);
    drawMenu_value_y_attrs_valueAttrs_style(
        "WAVE", drawing::stringFromUtf8(s3g::ambiVotPresetName(p.preset)), 222,
        attrs, valueAttrs, style);
    drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
        "BASE", kBaseNoteParamId, p.baseNote, 12, 96, 248, attrs, valueAttrs,
        style);
    drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
        "TUNE", kTuneParamId, p.tuneCents, -1200, 1200, 274, attrs, valueAttrs,
        style);

    drawing::drawPanelFrame(630, 304, 250, 158, style);
    drawing::drawPanelHeader("TUNING", true, 630, 304, 250, 21, attrs, style);
    drawMenu_value_y_attrs_valueAttrs_style(
        "SCALE", drawing::stringFromUtf8(s3g::ambiVotScaleName(p.scale)), 340,
        attrs, valueAttrs, style);
    drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
        "SPREAD", kPitchSpreadParamId, p.pitchSpread, 0, 2, 366, attrs,
        valueAttrs, style);
    drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
        "DEV", kDetuneParamId, p.detune, 0, 1, 392, attrs, valueAttrs, style);
    drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
        "HARM", kHarmonicsParamId, p.harmonicAmount, 0, 1, 418, attrs,
        valueAttrs, style);
    drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
        "SUB", kSubharmonicsParamId, p.subharmonicAmount, 0, 1, 444, attrs,
        valueAttrs, style);

    drawing::drawPanelFrame(630, 474, 250, 132, style);
    drawing::drawPanelHeader("ENVELOPE", true, 630, 474, 250, 21, attrs, style);
    drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
        "ATTACK", kAttackParamId, p.attackMs, 1, 2000, 510, attrs, valueAttrs,
        style);
    drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
        "DECAY", kDecayParamId, p.decayMs, 5, 4000, 536, attrs, valueAttrs,
        style);
    drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
        "SUSTAIN", kSustainParamId, p.sustain, 0, 1, 562, attrs, valueAttrs,
        style);
    drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
        "RELEASE", kReleaseParamId, p.releaseMs, 5, 8000, 588, attrs,
        valueAttrs, style);

    constexpr double motionX = 896;
    drawing::drawPanelFrame(motionX, 42, 246, 418, style);
    drawing::drawPanelHeader("MOTION / PROJECTION", true, motionX, 42, 246, 21,
                             attrs, style);
    drawMenuAtX_name_value_y_attrs_valueAttrs_style(
        motionX, "SCENE",
        drawing::stringFromUtf8(s3g::ambiVotMotionSceneName(p.motionScene)), 78,
        attrs, valueAttrs, style);
    drawMenuAtX_name_value_y_attrs_valueAttrs_style(
        motionX, "CLOCK",
        drawing::stringFromUtf8(s3g::ambiVotMotionClockName(p.motionClock)),
        104, attrs, valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "RATE", kMotionRateParamId, p.motionRateHz, 0.001, 2, 130,
        attrs, valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "DIV", kSyncDivisionParamId, p.syncDivisionBeats, 0.25, 64,
        156, attrs, valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "AMOUNT", kMotionAmountParamId, p.motionAmount, 0, 1, 182,
        attrs, valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "SPREAD", kSpreadParamId, p.motionSpread, 0, 1, 208, attrs,
        valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "COHERE", kCoherenceParamId, p.motionCoherence, 0, 1, 234,
        attrs, valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "CHAOS", kChaosParamId, p.motionChaos, 0, 1, 260, attrs,
        valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "LINK", kLinkParamId, p.motionLink, 0, 1, 286, attrs,
        valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "NEAR", kNeighborRadiusParamId, p.neighborRadius, 0.05, 4, 312,
        attrs, valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "NBR", kRequiredNeighborsParamId, p.requiredNeighbors, 1, 63,
        338, attrs, valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "SMOOTH", kSmoothParamId, p.motionSmooth, 0, 1, 364, attrs,
        valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "AZIMUTH", kCenterAzimuthParamId, p.centerAzimuthDeg, -180,
        180, 390, attrs, valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "ELEV", kCenterElevationParamId, p.centerElevationDeg, -90, 90,
        416, attrs, valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "DISTANCE", kCenterDistanceParamId, p.centerDistance, 0.15, 2,
        442, attrs, valueAttrs, style);

    const auto score = loadScore(*_plugin);
    drawing::drawPanelFrame(motionX, 472, 246, 202, style);
    drawing::drawPanelHeader("VECTOR SCORE", true, motionX, 472, 246, 21, attrs,
                             style);
    const drawing::Rect scoreHeader = drawing::makeRect(motionX, 472, 246, 21);
    drawing::drawToolboxHeaderActionButton(scoreRemoveButtonRect(), scoreHeader,
                                           "-", attrs, style);
    drawing::drawToolboxHeaderActionButton(scoreAddButtonRect(), scoreHeader,
                                           "+", attrs, style);
    drawing::drawToolboxHeaderActionButton(scoreResetButtonRect(), scoreHeader,
                                           "RESET", attrs, style);
    drawMenuAtX_name_value_y_attrs_valueAttrs_style(
        motionX, "MODE",
        drawing::stringFromUtf8(s3g::ambiVotScoreModeName(p.scoreMode)), 508,
        attrs, valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "DURATION", kScoreDurationParamId, p.scoreDurationSec, 0.25,
        60, 534, attrs, valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "DEPTH", kScoreDepthParamId, p.scoreDepth, 0, 1, 560, attrs,
        valueAttrs, style);
    drawing::drawSlider("LOOP A",
                        drawing::stringFormat("N%u", score.sustainStart + 1u),
                        static_cast<double>(score.sustainStart) /
                            static_cast<double>(score.nodeCount - 1u),
                        586, attrs, valueAttrs, style, motionX + 16,
                        motionX + 108, motionX + 196, 82);
    drawing::drawSlider("LOOP B",
                        drawing::stringFormat("N%u", score.sustainEnd + 1u),
                        static_cast<double>(score.sustainEnd) /
                            static_cast<double>(score.nodeCount - 1u),
                        612, attrs, valueAttrs, style, motionX + 16,
                        motionX + 108, motionX + 196, 82);
    drawing::String scoreInfo =
        drawing::stringFormat("%u NODES   LOOP %u-%u", score.nodeCount,
                              score.sustainStart + 1u, score.sustainEnd + 1u);
    drawing::drawAtPoint_withAttributes(
        scoreInfo, drawing::makePoint(motionX + 16, 646), valueAttrs);
  }

  void drawOpenMenu_style(drawing::Attrs attrs, const drawing::Style &style) {
    if (_openMenu <= 0 || _menuItemCount == 0u)
      return;
    static drawing::String modeItems[] = {"FREE", "MIDI", "BOTH"};
    static drawing::String waveItems[] = {"SINE", "CLASSIC", "DIGITAL",
                                          "FORMANT", "USER"};
    static drawing::String orderItems[] = {"1OA", "2OA", "3OA", "4OA",
                                           "5OA", "6OA", "7OA"};
    static drawing::String sceneItems[] = {"MANUAL", "ORBIT", "FLOW", "PATH",
                                           "PULSE"};
    static drawing::String clockItems[] = {"FREE", "SYNC"};
    static std::array<drawing::String, s3g::kMusicalScaleCount> scaleItems =
        [] {
          std::array<drawing::String, s3g::kMusicalScaleCount> items{};
          for (uint32_t menuIndex = 0u; menuIndex < s3g::kMusicalScaleCount;
               ++menuIndex) {
            const uint32_t scale =
                s3g::musicalScaleValueForMenuIndex(menuIndex);
            items[menuIndex] = drawing::stringFromUtf8(
                s3g::musicalScaleDefinition(scale).name);
          }
          return items;
        }();
    static drawing::String scoreModeItems[] = {"OFF", "ONE", "LOOP", "PING"};
    static drawing::String curveItems[] = {"LINEAR", "SMOOTH", "EXP", "HOLD"};
    drawing::String *items = modeItems;
    int selected = 0;
    if (_openMenu == 1)
      selected = static_cast<int>(static_cast<uint32_t>(_snapshot.mode));
    else if (_openMenu == 2) {
      items = waveItems;
      selected = static_cast<int>(static_cast<uint32_t>(_snapshot.preset));
    } else if (_openMenu == 3) {
      items = orderItems;
      selected = static_cast<int>(_snapshot.order - 1u);
    } else if (_openMenu == 4) {
      items = sceneItems;
      selected = static_cast<int>(static_cast<uint32_t>(_snapshot.motionScene));
    } else if (_openMenu == 5) {
      items = clockItems;
      selected = static_cast<int>(static_cast<uint32_t>(_snapshot.motionClock));
    } else if (_openMenu == 6) {
      items = scaleItems.data();
      selected = static_cast<int>(s3g::musicalScaleMenuIndexForValue(
          static_cast<uint32_t>(_snapshot.scale)));
    } else if (_openMenu == 7) {
      items = scoreModeItems;
      selected = static_cast<int>(static_cast<uint32_t>(_snapshot.scoreMode));
    } else {
      items = curveItems;
      const auto score = loadScore(*_plugin);
      selected = static_cast<int>(static_cast<uint32_t>(
          score
              .nodes[std::min<uint32_t>(_selectedScoreNode,
                                        score.nodeCount - 1u)]
              .curve));
    }
    const double itemHeight = 18.0;
    const drawing::Rect menuRect = openMenuRect();
    if (_openMenu == 6) {
      drawing::drawMultiColumnDropdownMenu(menuRect, itemHeight, items,
                                           _menuItemCount, 4u, selected,
                                           _hoverMenuItem, attrs, style);
    } else {
      drawing::drawDropdownMenu(menuRect, itemHeight, items, _menuItemCount,
                                selected, _hoverMenuItem, attrs, style);
    }
  }

  void drawOriginal(drawing::Rect dirtyRect) {
    (void)dirtyRect;
    if (!_plugin)
      return;
    const drawing::Style style = drawing::softTextStyle();
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs titleAttrs = drawing::softTitleAttrs();
    drawing::Attrs labelAttrs = drawing::softLabelAttrs();
    drawing::Attrs valueAttrs = drawing::softValueAttrs();
    drawing::String status = drawing::peakDbText(
        _plugin->outputPeak.load(std::memory_order_relaxed));
    drawing::drawEncoderTitleBand(
        "s3g AMBI ENCODER VOT", drawing::stringFromUtf8(_titlePresetName),
        status, drawing::encoderTitleBand(kGuiW, kGuiH), titleAttrs, labelAttrs,
        valueAttrs, style);

    const drawing::Rect left = leftPanelRect();
    drawing::drawPanelFrame(left.origin.x, left.origin.y, left.size.width,
                            left.size.height, style);
    drawing::drawPanelHeader("VOICE SPACE", true, left.origin.x, left.origin.y,
                             left.size.width, 21, labelAttrs, style);
    drawPageButtons_style(labelAttrs, style);
    drawViewButtons_style(labelAttrs, style);
    if (_leftPage == 0)
      drawField_attrs(leftContentRect(), valueAttrs);
    else if (_leftPage == 1)
      drawVectorPage_attrs_valueAttrs_style(leftContentRect(), labelAttrs,
                                            valueAttrs, style);
    else
      drawScorePage_attrs_valueAttrs_style(leftContentRect(), labelAttrs,
                                           valueAttrs, style);
    drawPanels_valueAttrs_style(labelAttrs, valueAttrs, style);
    drawOpenMenu_style(valueAttrs, style);
  }

  double defaultValueForParam(clap_id param) {
    for (const auto &def : kParams) {
      if (def.id == param)
        return def.def;
    }
    return 0.0;
  }

  void setParam_fromNorm(clap_id param, double norm) {
    norm = std::clamp(norm, 0.0, 1.0);
    double value = 0.0;
    switch (param) {
    case kVoicesParamId:
      value = 1.0 + norm * 63.0;
      break;
    case kBaseNoteParamId:
      value = 12.0 + norm * 84.0;
      break;
    case kTuneParamId:
      value = -1200.0 + norm * 2400.0;
      break;
    case kDetuneParamId:
    case kHarmonicsParamId:
    case kSubharmonicsParamId:
    case kScoreDepthParamId:
    case kMotionAmountParamId:
    case kSpreadParamId:
    case kCoherenceParamId:
    case kChaosParamId:
    case kLinkParamId:
    case kSmoothParamId:
    case kVectorXParamId:
    case kVectorYParamId:
    case kScanParamId:
    case kMorphParamId:
    case kSustainParamId:
      value = norm;
      break;
    case kPitchSpreadParamId:
      value = norm * 2.0;
      break;
    case kAttackParamId:
      value = 1.0 * std::pow(2000.0, norm);
      break;
    case kDecayParamId:
      value = 5.0 * std::pow(800.0, norm);
      break;
    case kReleaseParamId:
      value = 5.0 * std::pow(1600.0, norm);
      break;
    case kMotionRateParamId:
      value = 0.001 * std::pow(2000.0, norm);
      break;
    case kScoreDurationParamId:
      value = 0.25 * std::pow(240.0, norm);
      break;
    case kSyncDivisionParamId:
      value = 0.25 + norm * 63.75;
      break;
    case kCenterAzimuthParamId:
      value = s3g::aedAzimuthFromSliderNorm(static_cast<float>(norm));
      break;
    case kCenterElevationParamId:
      value = -90.0 + norm * 180.0;
      break;
    case kCenterDistanceParamId:
      value = 0.15 + norm * 1.85;
      break;
    case kNeighborRadiusParamId:
      value = 0.05 + norm * 3.95;
      break;
    case kRequiredNeighborsParamId:
      value = 1.0 + norm * 62.0;
      break;
    case kOutputParamId:
      value = -60.0 + norm * 72.0;
      break;
    default:
      return;
    }
    editValue(param, value);
  }

  void updateDraggedSlider(drawing::Point point) {
    if (_dragParam == CLAP_INVALID_ID)
      return;
    const double trackX =
        _dragArea == 2 ? 120.0 : (_dragArea == 7 ? 1004.0 : 738.0);
    const double trackWidth = _dragArea == 2 ? 170.0 : 82.0;
    setParam_fromNorm(_dragParam, (point.x - trackX) / trackWidth);
    setNeedsDisplay(true);
  }

  void openMenu_count_x_y_width(int menu, uint32_t count, double x, double y,
                                double width) {
    _openMenu = menu;
    _menuItemCount = count;
    _hoverMenuItem = -1;
    _menuOrigin = drawing::makePoint(x, y + 18.0);
    _menuWidth = width;
    setNeedsDisplay(true);
  }

  drawing::Rect openMenuRect() {
    const double itemHeight = 18.0;
    const uint32_t columns = _openMenu == 6 ? 4u : 1u;
    const double columnWidth = _openMenu == 6 ? 180.0 : _menuWidth;
    const uint32_t rows = drawing::multiColumnMenuRows(_menuItemCount, columns);
    const double width = columnWidth * static_cast<double>(columns);
    const double height = itemHeight * static_cast<double>(rows);
    const double x =
        std::clamp(_menuOrigin.x, 4.0, bounds().size.width - width - 4.0);
    double y = _menuOrigin.y;
    if (y + height > bounds().size.height - 4.0) {
      y = _menuOrigin.y - itemHeight - height;
    }
    return drawing::makeRect(x, std::max<double>(4.0, y), width, height);
  }

  int openMenuHit(drawing::Point point) {
    const drawing::Rect menuRect = openMenuRect();
    return _openMenu == 6 ? drawing::multiColumnDropdownHitIndex(
                                point, menuRect, 18.0, _menuItemCount, 4u)
                          : drawing::dropdownHitIndex(point, menuRect, 18.0,
                                                      _menuItemCount);
  }

  void closeMenuAtPoint(drawing::Point point) {
    const int hit = openMenuHit(point);
    if (hit >= 0) {
      if (_openMenu == 1)
        editValue(kModeParamId, hit);
      else if (_openMenu == 2)
        editValue(kPresetParamId, hit);
      else if (_openMenu == 3)
        editValue(kOrderParamId, hit + 1);
      else if (_openMenu == 4)
        editValue(kMotionSceneParamId, hit);
      else if (_openMenu == 5)
        editValue(kMotionClockParamId, hit);
      else if (_openMenu == 6) {
        editValue(kScaleParamId, s3g::musicalScaleValueForMenuIndex(
                                     static_cast<uint32_t>(hit)));
      } else if (_openMenu == 7)
        editValue(kScoreModeParamId, hit);
      else if (_openMenu == 8) {
        auto score = loadScore(*_plugin);
        const uint32_t node =
            std::min<uint32_t>(_selectedScoreNode, score.nodeCount - 1u);
        score.nodes[node].curve = static_cast<s3g::AmbiVotScoreCurve>(hit);
        editScore(score);
      }
    }
    _openMenu = 0;
    _hoverMenuItem = -1;
    _menuItemCount = 0;
    _menuWidth = 124.0;
    setNeedsDisplay(true);
  }

  int hitVoiceAtPoint(drawing::Point point) {
    const drawing::Rect rect = leftContentRect();
    const uint32_t voices =
        std::clamp<uint32_t>(_snapshot.voices, 1u, s3g::kAmbiVotMaxVoices);
    int bestVoice = -1;
    double bestDistance = 144.0;
    for (uint32_t voice = 0; voice < voices; ++voice) {
      double depth = 0.0;
      const drawing::Point projected =
          projectMotionPoint_rect_depth(snapshotPoint(voice), rect, &depth);
      const double dx = point.x - projected.x;
      const double dy = point.y - projected.y;
      const double distance = dx * dx + dy * dy;
      if (distance < bestDistance) {
        bestDistance = distance;
        bestVoice = static_cast<int>(voice);
      }
    }
    return bestVoice;
  }

  void selectVectorVoiceAtPoint(drawing::Point point) {
    const drawing::Rect content = leftContentRect();
    const drawing::Rect pathRect = drawing::makeRect(
        content.origin.x + 278, content.origin.y + 8, 270, 252);
    const drawing::Rect inner = drawing::inset(pathRect, 14, 28);
    const uint32_t voices =
        std::clamp<uint32_t>(_snapshot.voices, 1u, s3g::kAmbiVotMaxVoices);
    int bestVoice = -1;
    double bestDistance = 196.0;
    for (uint32_t voice = 0; voice < voices; ++voice) {
      const auto motion = snapshotPoint(voice);
      const drawing::Point projected = drawing::makePoint(
          inner.origin.x + motion.u * inner.size.width,
          inner.origin.y + (1.0f - motion.v) * inner.size.height);
      const double dx = point.x - projected.x;
      const double dy = point.y - projected.y;
      const double distance = dx * dx + dy * dy;
      if (distance < bestDistance) {
        bestDistance = distance;
        bestVoice = static_cast<int>(voice);
      }
    }
    if (bestVoice >= 0)
      _selectedVoice = static_cast<uint32_t>(bestVoice);
  }

  void updateScoreDragAtPoint(drawing::Point point) {
    auto score = loadScore(*_plugin);
    const uint32_t node =
        std::min<uint32_t>(_selectedScoreNode, score.nodeCount - 1u);
    const drawing::Rect content = leftContentRect();
    if (_dragArea == 4) {
      const drawing::Rect timelineRect = drawing::makeRect(
          content.origin.x + 8, content.origin.y + 8, 540, 306);
      const drawing::Rect lane =
          _scoreDragLane == 0
              ? drawing::makeRect(timelineRect.origin.x + 14,
                                  timelineRect.origin.y + 34,
                                  timelineRect.size.width - 28, 112)
              : drawing::makeRect(timelineRect.origin.x + 14,
                                  timelineRect.origin.y + 174,
                                  timelineRect.size.width - 28, 112);
      if (node > 0u && node + 1u < score.nodeCount) {
        const float minimum = score.nodes[node - 1u].time + 0.01f;
        const float maximum = score.nodes[node + 1u].time - 0.01f;
        score.nodes[node].time = std::clamp(
            static_cast<float>((point.x - lane.origin.x) / lane.size.width),
            minimum, maximum);
      }
      const float value =
          std::clamp(static_cast<float>(1.0 - (point.y - lane.origin.y) /
                                                  lane.size.height),
                     0.0f, 1.0f);
      if (_scoreDragLane == 0)
        score.nodes[node].u = value;
      else
        score.nodes[node].v = value;
    } else if (_dragArea == 5) {
      const drawing::Rect routeRect = drawing::makeRect(
          content.origin.x + 8, content.origin.y + 328, 540, 292);
      const drawing::Rect inner = drawing::makeRect(
          routeRect.origin.x + 14, routeRect.origin.y + 30,
          routeRect.size.width - 28, routeRect.size.height - 44);
      score.nodes[node].u = std::clamp(
          static_cast<float>((point.x - inner.origin.x) / inner.size.width),
          0.0f, 1.0f);
      score.nodes[node].v =
          std::clamp(static_cast<float>(1.0 - (point.y - inner.origin.y) /
                                                  inner.size.height),
                     0.0f, 1.0f);
    } else if (_dragArea == 6) {
      const float value = std::clamp(
          static_cast<float>((point.x - (content.origin.x + 270)) / 170.0),
          0.0f, 1.0f);
      if (_scoreDragLane == 2 && node > 0u && node + 1u < score.nodeCount) {
        score.nodes[node].time =
            std::clamp(value, score.nodes[node - 1u].time + 0.01f,
                       score.nodes[node + 1u].time - 0.01f);
      } else if (_scoreDragLane == 3)
        score.nodes[node].u = value;
      else if (_scoreDragLane == 4)
        score.nodes[node].v = value;
    } else if (_dragArea == 8) {
      const float norm =
          std::clamp(static_cast<float>((point.x - 1004.0) / 82.0), 0.0f, 1.0f);
      const uint32_t selected = std::min<uint32_t>(
          static_cast<uint32_t>(
              std::lround(norm * static_cast<float>(score.nodeCount - 1u))),
          score.nodeCount - 1u);
      if (_scoreDragLane == 5)
        score.sustainStart =
            std::min<uint32_t>(selected, score.sustainEnd - 1u);
      else
        score.sustainEnd =
            std::max<uint32_t>(selected, score.sustainStart + 1u);
    }
    editScore(score);
    setNeedsDisplay(true);
  }

  void mouseDown(drawing::Event *event) {
    const drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    const auto titleBand = drawing::encoderTitleBand(kGuiW, kGuiH);
    if (drawing::contains(point, drawing::cocoaRect(titleBand.presetMenu))) {
      const uint32_t order = _snapshot.order;
      const float output = _snapshot.outputGainDb;
      auto initial = s3g::AmbiVotParams{};
      initial.order = order;
      initial.outputGainDb = output;
      for (const auto &def : kParams)
        singleEdit(def.id, portableParamValue(initial, def.id));
      _snapshot = portableParamsSnapshot(*_plugin);
      editScore(s3g::ambiVotDefaultScore());
      std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "INIT");
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, drawing::cocoaRect(titleBand.loadButton))) {
      drawing::String name{};
      if (presetDialog(false, &name)) {
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                      !name.empty() ? name.c_str() : "CUSTOM");
        setNeedsDisplay(true);
      } else {
        beep();
      }
      return;
    }
    if (drawing::contains(point, drawing::cocoaRect(titleBand.saveButton))) {
      drawing::String name{};
      if (presetDialog(true, &name)) {
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                      !name.empty() ? name.c_str() : "CUSTOM");
        setNeedsDisplay(true);
      } else {
        beep();
      }
      return;
    }
    if (drawing::contains(point, drawing::cocoaRect(titleBand.randomButton))) {
      auto randomUnit = [] {
        return static_cast<double>(drawing::randomBits()) / 4294967295.0;
      };
      editValue(kModeParamId, drawing::randomBounded(3u));
      editValue(kBaseNoteParamId, 28.0 + randomUnit() * 40.0);
      editValue(kTuneParamId, -24.0 + randomUnit() * 48.0);
      editValue(kVectorXParamId, randomUnit());
      editValue(kVectorYParamId, randomUnit());
      editValue(kScanParamId, randomUnit());
      editValue(kDetuneParamId, randomUnit() * 0.42);
      editValue(kPitchSpreadParamId, 0.15 + randomUnit() * 1.45);
      editValue(kMotionSceneParamId, drawing::randomBounded(6u));
      editValue(kMotionAmountParamId, 0.18 + randomUnit() * 0.72);
      editValue(kChaosParamId, randomUnit() * 0.72);
      std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "RANDOM");
      setNeedsDisplay(true);
      return;
    }
    if (_openMenu > 0) {
      closeMenuAtPoint(point);
      return;
    }
    if (drawing::contains(point, synthLoadButtonRect())) {
      loadWave();
      return;
    }
    if (drawing::contains(point, scoreResetButtonRect())) {
      editScore(s3g::ambiVotDefaultScore());
      _selectedScoreNode = 0u;
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, scoreRemoveButtonRect())) {
      auto score = loadScore(*_plugin);
      if (score.nodeCount > 2u) {
        --score.nodeCount;
        score.nodes[score.nodeCount - 1u].time = 1.0f;
        score.sustainStart =
            std::min<uint32_t>(score.sustainStart, score.nodeCount - 2u);
        score.sustainEnd =
            std::max<uint32_t>(score.sustainStart + 1u, score.nodeCount - 2u);
        editScore(score);
        _selectedScoreNode =
            std::min<uint32_t>(_selectedScoreNode, score.nodeCount - 1u);
      }
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, scoreAddButtonRect())) {
      auto score = loadScore(*_plugin);
      if (score.nodeCount < s3g::kAmbiVotMaxScoreNodes) {
        const uint32_t oldLast = score.nodeCount - 1u;
        const uint32_t newLast = score.nodeCount;
        score.nodes[newLast] = score.nodes[oldLast];
        score.nodes[newLast].time = 1.0f;
        score.nodes[oldLast].time =
            0.5f * (score.nodes[oldLast - 1u].time + 1.0f);
        ++score.nodeCount;
        score.sustainEnd = score.nodeCount - 2u;
        editScore(score);
        _selectedScoreNode = oldLast;
      }
      setNeedsDisplay(true);
      return;
    }
    for (int i = 0; i < 3; ++i) {
      if (drawing::contains(point, pageButtonRect(i))) {
        _leftPage = i;
        storeViewState();
        setNeedsDisplay(true);
        return;
      }
    }
    if (_leftPage == 0) {
      for (int i = 0; i < 3; ++i) {
        if (drawing::contains(point, viewButtonRect(i))) {
          setViewPreset(i);
          return;
        }
      }
      for (int i = 0; i < 2; ++i) {
        if (drawing::contains(point, zoomButtonRect(i))) {
          _viewZoom =
              std::clamp(_viewZoom * (i == 0 ? 0.82 : 1.22), 0.55, 2.20);
          _viewMode = -1;
          storeViewState();
          setNeedsDisplay(true);
          return;
        }
      }
      if (drawing::contains(point, leftContentRect())) {
        if (pointerShift) {
          _dragView = true;
          _lastDragPoint = point;
          return;
        }
        const int hit = hitVoiceAtPoint(point);
        if (hit >= 0) {
          _selectedVoice = static_cast<uint32_t>(hit);
          setNeedsDisplay(true);
          return;
        }
        _dragView = true;
        _lastDragPoint = point;
        return;
      }
    } else if (_leftPage == 1) {
      const drawing::Rect content = leftContentRect();
      const drawing::Rect bankRect = drawing::makeRect(
          content.origin.x + 8, content.origin.y + 8, 252, 252);
      const drawing::Rect pathRect = drawing::makeRect(
          content.origin.x + 278, content.origin.y + 8, 270, 252);
      if (drawing::contains(point, pathRect)) {
        selectVectorVoiceAtPoint(point);
        setNeedsDisplay(true);
        return;
      }
      if (drawing::contains(point, bankRect)) {
        editValue(kVectorXParamId, std::clamp((point.x - bankRect.origin.x) /
                                                  bankRect.size.width,
                                              0.0, 1.0));
        editValue(kVectorYParamId,
                  std::clamp(1.0 - (point.y - bankRect.origin.y) /
                                       bankRect.size.height,
                             0.0, 1.0));
        _dragParam = kVectorXParamId;
        _dragArea = 3;
        setNeedsDisplay(true);
        return;
      }
    } else {
      const auto score = loadScore(*_plugin);
      const drawing::Rect content = leftContentRect();
      const drawing::Rect timelineRect = drawing::makeRect(
          content.origin.x + 8, content.origin.y + 8, 540, 306);
      const drawing::Rect lanes[]{
          drawing::makeRect(timelineRect.origin.x + 14,
                            timelineRect.origin.y + 34,
                            timelineRect.size.width - 28, 112),
          drawing::makeRect(timelineRect.origin.x + 14,
                            timelineRect.origin.y + 174,
                            timelineRect.size.width - 28, 112),
      };
      double bestDistance = 144.0;
      int bestNode = -1;
      int bestLane = -1;
      for (uint32_t node = 0; node < score.nodeCount; ++node) {
        for (int lane = 0; lane < 2; ++lane) {
          const float value =
              lane == 0 ? score.nodes[node].u : score.nodes[node].v;
          const drawing::Point handle = drawing::makePoint(
              lanes[lane].origin.x +
                  score.nodes[node].time * lanes[lane].size.width,
              lanes[lane].origin.y + (1.0f - value) * lanes[lane].size.height);
          const double dx = point.x - handle.x;
          const double dy = point.y - handle.y;
          const double distance = dx * dx + dy * dy;
          if (distance < bestDistance) {
            bestDistance = distance;
            bestNode = static_cast<int>(node);
            bestLane = lane;
          }
        }
      }
      if (bestNode >= 0) {
        _selectedScoreNode = static_cast<uint32_t>(bestNode);
        _scoreDragLane = bestLane;
        _dragArea = 4;
        setNeedsDisplay(true);
        return;
      }
      const drawing::Rect routeRect = drawing::makeRect(
          content.origin.x + 8, content.origin.y + 328, 540, 292);
      const drawing::Rect routeInner = drawing::makeRect(
          routeRect.origin.x + 14, routeRect.origin.y + 30,
          routeRect.size.width - 28, routeRect.size.height - 44);
      if (drawing::contains(point, routeInner)) {
        bestDistance = 196.0;
        bestNode = -1;
        for (uint32_t node = 0; node < score.nodeCount; ++node) {
          const drawing::Point handle = drawing::makePoint(
              routeInner.origin.x + score.nodes[node].u * routeInner.size.width,
              routeInner.origin.y +
                  (1.0f - score.nodes[node].v) * routeInner.size.height);
          const double dx = point.x - handle.x;
          const double dy = point.y - handle.y;
          const double distance = dx * dx + dy * dy;
          if (distance < bestDistance) {
            bestDistance = distance;
            bestNode = static_cast<int>(node);
          }
        }
        if (bestNode >= 0) {
          _selectedScoreNode = static_cast<uint32_t>(bestNode);
          _dragArea = 5;
          setNeedsDisplay(true);
          return;
        }
      }
    }

    struct MenuHit {
      int menu;
      uint32_t count;
      double x;
      double y;
      double width;
    };
    static constexpr MenuHit menus[]{
        {3, 7, 738, 104, 124},  {1, 3, 738, 196, 124},
        {2, 5, 738, 222, 124},  {6, s3g::kMusicalScaleCount, 738, 340, 124},
        {4, 5, 1004, 78, 124},  {5, 2, 1004, 104, 124},
        {7, 4, 1004, 508, 124},
    };
    for (const auto &menu : menus) {
      if (drawing::contains(
              point, drawing::makeRect(menu.x, menu.y - 1, menu.width, 17))) {
        openMenu_count_x_y_width(menu.menu, menu.count, menu.x, menu.y,
                                 menu.width);
        return;
      }
    }
    if (_leftPage == 2 &&
        drawing::contains(point, drawing::makeRect(120, 797, 184, 17))) {
      openMenu_count_x_y_width(8, 4, 120, 798, 184);
      return;
    }

    struct SliderHit {
      clap_id param;
      double x;
      double y;
      int area;
    };
    static constexpr SliderHit sliders[]{
        {kOutputParamId, 638, 78, 1},
        {kVoicesParamId, 638, kVoiceCountRowY, 1},
        {kBaseNoteParamId, 638, 248, 1},
        {kTuneParamId, 638, 274, 1},
        {kPitchSpreadParamId, 638, 366, 1},
        {kDetuneParamId, 638, 392, 1},
        {kHarmonicsParamId, 638, 418, 1},
        {kSubharmonicsParamId, 638, 444, 1},
        {kAttackParamId, 638, 510, 1},
        {kDecayParamId, 638, 536, 1},
        {kSustainParamId, 638, 562, 1},
        {kReleaseParamId, 638, 588, 1},
        {kMotionRateParamId, 904, 130, 7},
        {kSyncDivisionParamId, 904, 156, 7},
        {kMotionAmountParamId, 904, 182, 7},
        {kSpreadParamId, 904, 208, 7},
        {kCoherenceParamId, 904, 234, 7},
        {kChaosParamId, 904, 260, 7},
        {kLinkParamId, 904, 286, 7},
        {kNeighborRadiusParamId, 904, 312, 7},
        {kRequiredNeighborsParamId, 904, 338, 7},
        {kSmoothParamId, 904, 364, 7},
        {kCenterAzimuthParamId, 904, 390, 7},
        {kCenterElevationParamId, 904, 416, 7},
        {kCenterDistanceParamId, 904, 442, 7},
        {kScoreDurationParamId, 904, 534, 7},
        {kScoreDepthParamId, 904, 560, 7},
    };
    for (const auto &slider : sliders) {
      if (drawing::contains(
              point, drawing::makeRect(slider.x, slider.y - 8, 232, 24))) {
        if ((event)->clickCount() >= 2)
          editValue(slider.param, defaultValueForParam(slider.param));
        else {
          _dragParam = slider.param;
          _dragArea = slider.area;
          updateDraggedSlider(point);
        }
        return;
      }
    }
    const double scoreLoopRows[] = {586, 612};
    for (int i = 0; i < 2; ++i) {
      if (!drawing::contains(
              point, drawing::makeRect(904, scoreLoopRows[i] - 8, 232, 24)))
        continue;
      if ((event)->clickCount() >= 2) {
        auto score = loadScore(*_plugin);
        if (i == 0)
          score.sustainStart = std::min<uint32_t>(2u, score.nodeCount - 2u);
        else
          score.sustainEnd =
              std::max<uint32_t>(score.sustainStart + 1u, score.nodeCount - 2u);
        editScore(score);
        setNeedsDisplay(true);
      } else {
        _dragArea = 8;
        _scoreDragLane = i + 5;
        updateScoreDragAtPoint(point);
      }
      return;
    }

    if (_leftPage == 1) {
      const double localRows[] = {678, 704, 730, 756};
      const clap_id localParams[] = {kVectorXParamId, kVectorYParamId,
                                     kScanParamId, kMorphParamId};
      for (int i = 0; i < 4; ++i) {
        if (drawing::contains(
                point, drawing::makeRect(38, localRows[i] - 8, 280, 24))) {
          if ((event)->clickCount() >= 2)
            editValue(localParams[i], defaultValueForParam(localParams[i]));
          else {
            _dragParam = localParams[i];
            _dragArea = 2;
            updateDraggedSlider(point);
          }
          return;
        }
      }
    } else if (_leftPage == 2) {
      const double localRows[] = {720, 746, 772};
      for (int i = 0; i < 3; ++i) {
        if (!drawing::contains(
                point, drawing::makeRect(38, localRows[i] - 8, 280, 24)))
          continue;
        if ((event)->clickCount() >= 2) {
          auto score = loadScore(*_plugin);
          const auto defaults = s3g::ambiVotDefaultScore();
          const uint32_t node =
              std::min<uint32_t>(_selectedScoreNode, score.nodeCount - 1u);
          if (i == 0 && node > 0u && node + 1u < score.nodeCount) {
            score.nodes[node].time = static_cast<float>(node) /
                                     static_cast<float>(score.nodeCount - 1u);
          } else if (i == 1)
            score.nodes[node].u = defaults.nodes[node].u;
          else if (i == 2)
            score.nodes[node].v = defaults.nodes[node].v;
          editScore(score);
          setNeedsDisplay(true);
        } else {
          _dragArea = 6;
          _scoreDragLane = i + 2;
          updateScoreDragAtPoint(point);
        }
        return;
      }
    }
  }

  void mouseDragged(drawing::Event *event) {
    const drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    if (_dragView) {
      const double dx = point.x - _lastDragPoint.x;
      const double dy = point.y - _lastDragPoint.y;
      _viewAzDeg += dx * 0.35;
      while (_viewAzDeg > 180.0)
        _viewAzDeg -= 360.0;
      while (_viewAzDeg <= -180.0)
        _viewAzDeg += 360.0;
      _viewElDeg = std::clamp(_viewElDeg - dy * 0.30, -85.0, 85.0);
      _viewMode = -1;
      _lastDragPoint = point;
      storeViewState();
      setNeedsDisplay(true);
      return;
    }
    if (_dragArea == 3) {
      const drawing::Rect content = leftContentRect();
      const drawing::Rect bankRect = drawing::makeRect(
          content.origin.x + 8, content.origin.y + 8, 252, 252);
      editValue(kVectorXParamId,
                std::clamp((point.x - bankRect.origin.x) / bankRect.size.width,
                           0.0, 1.0));
      editValue(
          kVectorYParamId,
          std::clamp(1.0 - (point.y - bankRect.origin.y) / bankRect.size.height,
                     0.0, 1.0));
      setNeedsDisplay(true);
      return;
    }
    if ((_dragArea >= 4 && _dragArea <= 6) || _dragArea == 8) {
      updateScoreDragAtPoint(point);
      return;
    }
    updateDraggedSlider(point);
  }

  void mouseUp(drawing::Event *event) {
    (void)event;
    _dragParam = CLAP_INVALID_ID;
    _dragArea = 0;
    _scoreDragLane = -1;
    _dragView = false;
  }

  void mouseMoved(drawing::Event *event) {
    if (_openMenu <= 0 || _menuItemCount == 0u)
      return;
    const drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    const int next = openMenuHit(point);
    if (next != _hoverMenuItem) {
      _hoverMenuItem = next;
      setNeedsDisplay(true);
    }
  }
};
} // namespace score_encoder_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new score_encoder_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
