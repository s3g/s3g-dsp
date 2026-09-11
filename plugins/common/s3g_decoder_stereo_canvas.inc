// Literal Cocoa geometry and interaction adaptation; see
// DECODERS_VSTGUI_PARITY.md.
namespace decoder_canvas {
namespace drawing = s3g::portable_gui::decoder_drawing;
namespace foundation = s3g::portable_gui::foundation;
using namespace VSTGUI;
using namespace s3g::portable_gui::routing;
using Kind = s3g::clap_gui::ParamEventKind;
constexpr const char *pluginName = "Ambi Decoder Stereo";
constexpr const char *pluginTitle = "s3g AMBI DECODER STEREO";
constexpr clap_id preservedOutput = kParamOutputGain;
constexpr const auto &stateExtension = stateExt;
double routingReadValue(const Plugin &p, clap_id id) {
  return decoderIsParam(id)
             ? p.decoderValues[id].load(std::memory_order_acquire)
             : 0.;
}
void routingPublish(Plugin &p,
                    const std::vector<std::pair<clap_id, double>> &values) {
  auto params = snapshotDecoderParams(p);
  for (auto [id, v] : values)
    setDecoderScalar(params, id, v);
  for (auto [id, v] : values)
    p.decoderValues[id].store(readDecoderScalar(params, id),
                              std::memory_order_release);
  if (decoderHasLayoutCount)
    p.decoderLayoutCount.store(decoderHiddenCount(params),
                               std::memory_order_release);
}
void routingApply(Plugin &p, clap_id id, double v) { applyParam(p, id, v); }
double routingRawValue(Plugin &p, clap_id id) {
  return routingReadValue(p, id);
}
void routingInitialize(Plugin &p) {
  p.plugin.plugin_data = &p;
  init(&p.plugin);
}
#define S3G_ROUTING_CUSTOM_ACCESS 1
#include "s3g_routing_parameter_editor.inc"
#undef S3G_ROUTING_CUSTOM_ACCESS
class Editor final : public ParameterEditor {
public:
  explicit Editor(Plugin &instance) : ParameterEditor(instance) {
    auto *plugin = &p;

    _plugin = plugin;
    _dragSlider = -1;
    _openMenu = 0;
    _hoverMenuItem = -1;
    _viewMode = 0;
    _dragView = false;
    _lastDragPoint = drawing::makePoint(0, 0);
    _viewYawDeg = -35.0f;
    _viewPitchDeg = -42.0f;
    _menuItems = 0;
    _menuOrigin = drawing::makePoint(0, 0);

    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CURRENT");

    service();
  }
  ~Editor() override { stopRefresh(); }
  void stopRefresh() override {
    ParameterEditor::stopRefresh();
    pointerEditing = false;
    _openMenu = 0;
    _dragSlider = -1;
    _dragView = false;
  }
  double refreshPeriodMilliseconds() const override { return 1000. / 24.; }
  void service() override { guiParams = snapshotDecoderParams(p); }
  void preparePresetSave(Plugin &candidate) override {
    candidate.decoderStateBase = p.decoderStateBase;
    candidate.decoderLayoutCount.store(p.decoderLayoutCount.load());
  }
  void finishPresetLoad(const Plugin &candidate) override {
    publishDecoderStateBase(p, candidate.decoderStateBase);
    p.decoderLayoutCount.store(candidate.decoderLayoutCount.load());

    service();
  }
  void guiApplyParam(Plugin &, clap_id id, double v) { setEditorParam(id, v); }
  void setEditorParam(clap_id id, double v) {
    if (pointerEditing) {
      if (begin(id))
        change(id, v);
    } else
      set(id, v);
    service();
    invalid();
  }
  void setParam_value(clap_id id, double v) { setEditorParam(id, v); }
  drawing::Rect bounds() const {
    return drawing::makeRect(0, 0, kGuiWidth, kGuiHeight);
  }
  drawing::Point convertPoint_fromView(drawing::Point pt,
                                       std::nullptr_t) const {
    return pt;
  }
  void setNeedsDisplay(bool) { invalid(); }
  bool pointerDown(MouseDownEvent &event) override {
    if (!event.buttonState.isLeft())
      return false;
    service();
    pointerEditing = true;
    drawing::Event e{{event.mousePosition.x, event.mousePosition.y},
                     int(event.clickCount)};
    mouseDown(&e);
    return true;
  }
  void pointerMove(MouseMoveEvent &event) override {
    drawing::Event e{{event.mousePosition.x, event.mousePosition.y}};
    if (event.buttonState.isLeft())
      mouseDragged(&e);
    else
      mouseMoved(&e);
    invalid();
    event.consumed = true;
  }
  void pointerUp(MouseUpEvent &event) override {
    drawing::Event e{{event.mousePosition.x, event.mousePosition.y}};
    mouseUp(&e);
    end();
    pointerEditing = false;
    invalid();
  }
  bool pointerEditing = false;
  DecoderParams guiParams{};

  void *_plugin;
  int _dragSlider;
  int _openMenu;
  int _hoverMenuItem;
  int _viewMode;
  bool _dragView;
  drawing::Point _lastDragPoint;
  float _viewYawDeg;
  float _viewPitchDeg;
  drawing::Point _menuOrigin;
  uint32_t _menuItems;

  char _titlePresetName[64];
  void drawSlider_value_norm_y_small(drawing::String name,
                                     drawing::String value, double norm,
                                     double y, drawing::Attrs small) {

    (void)small;
    drawing::Style style;
    drawing::drawSlider(name, value, norm, y, drawing::softLabelAttrs(),
                        drawing::softValueAttrs(), style, 606, 712, 852, 122);
  }
  void drawMenu_value_y_small(drawing::String name, drawing::String value,
                              double y, drawing::Attrs small) {

    (void)small;
    drawing::Style style;
    drawing::drawMenu(name, value, y, drawing::softLabelAttrs(),
                      drawing::softValueAttrs(), style, 606, 712, 176);
  }
  drawing::Rect viewButtonRect_inRect(int index, drawing::Rect rect) {

    return drawing::makeRect(rect.origin.x + rect.size.width - 168.0 +
                                 index * 52.0,
                             rect.origin.y + 3.0, 46.0, 15.0);
  }
  drawing::Point projectAzimuth_elevation_radius_inRect(float azDeg,
                                                        float elDeg,
                                                        double radius,
                                                        drawing::Rect field) {

    const double cx = field.origin.x + field.size.width * 0.5;
    const double cy = field.origin.y + field.size.height * 0.54;
    const float az = azDeg * s3g::kPi / 180.0f;
    const float el = elDeg * s3g::kPi / 180.0f;
    const float x = -std::sin(az) * std::cos(el);
    const float y = std::cos(az) * std::cos(el);
    const float z = std::sin(el);
    if (_viewMode == 0) {
      return drawing::makePoint(cx + static_cast<double>(x) * radius,
                                cy - static_cast<double>(y) * radius);
    }
    if (_viewMode == 1) {
      return drawing::makePoint(cx + static_cast<double>(x) * radius * 0.92,
                                cy - static_cast<double>(z) * radius * 0.92);
    }

    const float yaw = _viewYawDeg * s3g::kPi / 180.0f;
    const float pitch = _viewPitchDeg * s3g::kPi / 180.0f;
    const float x1 = x * std::cos(yaw) - y * std::sin(yaw);
    const float y1 = x * std::sin(yaw) + y * std::cos(yaw);
    const float z1 = z * std::cos(pitch) - y1 * std::sin(pitch);
    const float y2 = z * std::sin(pitch) + y1 * std::cos(pitch);
    return drawing::makePoint(cx + static_cast<double>(x1) * radius * 0.92,
                              cy - static_cast<double>(z1 + y2 * 0.10f) *
                                       radius * 0.92);
  }
  drawing::Point projectPoint_inRect(s3g::AmbiStereoVirtualPoint pt,
                                     drawing::Rect field) {

    const double maxR = std::min(field.size.width, field.size.height) * 0.42;
    return projectAzimuth_elevation_radius_inRect(pt.azimuthDeg,
                                                  pt.elevationDeg, maxR, field);
  }
  void paint() override {
    drawing::DrawingScope scope(*this, context, font, titleFont, &error);
    service();

    auto *p = static_cast<Plugin *>(_plugin);
    drawing::Style style;
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::Attrs small = drawing::softValueAttrs();
    drawing::Attrs text = drawing::softLabelAttrs();
    drawing::Attrs titleAttrs = drawing::softTitleAttrs();
    const float titlePeak =
        std::max(p->outputPeakLeft.load(std::memory_order_relaxed),
                 p->outputPeakRight.load(std::memory_order_relaxed));
    drawing::String titleStatus = drawing::peakDbText(titlePeak);
    drawing::drawDecoderTitleBand(
        "s3g AMBI DECODER STEREO", drawing::stringFromUtf8(_titlePresetName),
        titleStatus, drawing::encoderTitleBand(kGuiWidth, kGuiHeight),
        titleAttrs, text, small, style);

    std::optional<VSTGUI::CDrawContext::Transform> contentTransform;
    contentTransform.emplace(*context, VSTGUI::CGraphicsTransform{}.translate(
                                           0., kContentTranslation));

    drawing::Rect fieldPanel = drawing::makeRect(12, 34, 568, 594);
    drawing::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y,
                            fieldPanel.size.width, fieldPanel.size.height,
                            style);
    drawing::drawPanelHeader("STEREO IMAGE", true, fieldPanel.origin.x,
                             fieldPanel.origin.y, fieldPanel.size.width, 21,
                             text, style);
    static drawing::String viewLabels[] = {"TOP", "SIDE", "3/4"};
    for (int i = 0; i < 3; ++i) {
      drawing::drawToolboxHeaderButton(viewButtonRect_inRect(i, fieldPanel),
                                       fieldPanel, viewLabels[i],
                                       i == _viewMode, small, style);
    }
    drawing::Rect field = drawing::makeRect(28, 70, 536, 542);
    drawing::setFill(drawing::calibratedColor(0x101010));
    drawing::rectFill(field);
    drawing::setStroke(style.grid);
    drawing::frameRect(field);
    const double cx = field.origin.x + field.size.width * 0.5;
    const double cy = field.origin.y + field.size.height * 0.54;
    const double maxR = std::min(field.size.width, field.size.height) * 0.42;
    drawing::setStroke(drawing::calibratedColor(0x5d5d5d, 0.20));
    drawing::Path::strokeLineFromPoint_toPoint(
        drawing::makePoint(field.origin.x + 28, cy),
        drawing::makePoint(drawing::maxX(field) - 28, cy));
    drawing::Path::strokeLineFromPoint_toPoint(
        drawing::makePoint(cx, field.origin.y + 24),
        drawing::makePoint(cx, drawing::maxY(field) - 24));
    if (_viewMode == 0) {
      drawing::drawAtPoint_withAttributes(
          "0", drawing::makePoint(cx - 4, cy - maxR - 20), small);
      drawing::drawAtPoint_withAttributes(
          "-90", drawing::makePoint(cx + maxR + 10, cy - 7), small);
      drawing::drawAtPoint_withAttributes(
          "+90", drawing::makePoint(cx - maxR - 34, cy - 7), small);
      drawing::drawAtPoint_withAttributes(
          "180", drawing::makePoint(cx - 10, cy + maxR + 6), small);
    } else if (_viewMode == 1) {
      drawing::drawAtPoint_withAttributes(
          "-90 / R", drawing::makePoint(cx + maxR * 0.72, cy + 8), small);
      drawing::drawAtPoint_withAttributes(
          "+90 / L", drawing::makePoint(cx - maxR * 0.98, cy + 8), small);
      drawing::drawAtPoint_withAttributes(
          "+EL", drawing::makePoint(cx + 8, cy - maxR - 2), small);
      drawing::drawAtPoint_withAttributes(
          "-EL", drawing::makePoint(cx + 8, cy + maxR - 12), small);
    }

    auto viewPointForAzEl = [&](float azDeg, float elDeg,
                                double radius) -> drawing::Point {
      return projectAzimuth_elevation_radius_inRect(azDeg, elDeg, radius,
                                                    field);
    };

    const float widthScale =
        std::clamp(guiParams.stereoWidthPercent / 100.0f, 0.0f, 2.0f);
    const float visualAngle = guiParams.micAngleDeg * 0.5f * widthScale;
    const float directivity =
        std::clamp(guiParams.directivityPercent / 100.0f, 0.0f, 1.0f);
    const auto method = guiParams.method;
    auto visualResponse = [&](s3g::AmbiStereoMethod visualMethod,
                              float relr) -> float {
      if (visualMethod == s3g::AmbiStereoMethod::SpacedOmni) {
        return 1.0f;
      }
      if (visualMethod == s3g::AmbiStereoMethod::Blumlein) {
        return std::cos(relr);
      }
      const float cardioid = 0.5f + 0.5f * std::cos(relr);
      if (visualMethod == s3g::AmbiStereoMethod::DualShotgun) {
        return std::pow((1.0f - directivity) + directivity * cardioid,
                        2.25f + directivity * 1.75f);
      }
      if (visualMethod == s3g::AmbiStereoMethod::WideCardioid) {
        return (1.0f - directivity * 0.42f) + directivity * 0.42f * cardioid;
      }
      if (visualMethod == s3g::AmbiStereoMethod::SupercardioidXy) {
        return 0.37f + 0.63f * std::cos(relr);
      }
      if (visualMethod == s3g::AmbiStereoMethod::HypercardioidXy) {
        return 0.25f + 0.75f * std::cos(relr);
      }
      if (visualMethod == s3g::AmbiStereoMethod::HeightFocus) {
        return ((1.0f - directivity) + directivity * cardioid) * 0.82f;
      }
      return (1.0f - directivity) + directivity * cardioid;
    };
    auto drawPickupLobe = [&](drawing::Point origin, bool leftSide,
                              float facingDeg,
                              s3g::AmbiStereoMethod visualMethod) {
      drawing::Path path = drawing::Path::bezierPath();
      constexpr int steps = 96;
      for (int i = 0; i <= steps; ++i) {
        const float rel = -180.0f + static_cast<float>(i) * 360.0f /
                                        static_cast<float>(steps);
        const float relr = rel * s3g::kPi / 180.0f;
        const float response = std::abs(visualResponse(visualMethod, relr));
        const float az = guiParams.rotationDeg + facingDeg + rel;
        const double radius =
            maxR * (0.06 + (0.30 + 0.10 * widthScale) * response);
        const drawing::Point raw =
            viewPointForAzEl(az, guiParams.micElevationDeg, radius);
        const drawing::Point center =
            viewPointForAzEl(az, guiParams.micElevationDeg, 0.0);
        const drawing::Point pt = drawing::makePoint(
            origin.x + raw.x - center.x, origin.y + raw.y - center.y);
        if (i == 0)
          (path).moveToPoint(pt);
        else
          (path).lineToPoint(pt);
      }
      (path).closePath();
      drawing::setFill(drawing::calibratedColor(leftSide ? 0x8f8f8f : 0xd8d8d8,
                                                leftSide ? 0.17 : 0.13));
      (path).fill();
      drawing::setStroke(drawing::calibratedColor(
          leftSide ? 0xb2b2b2 : 0xf0f0f0, leftSide ? 0.48 : 0.42));
      (path).stroke();
      if (visualMethod == s3g::AmbiStereoMethod::Blumlein ||
          visualMethod == s3g::AmbiStereoMethod::SupercardioidXy ||
          visualMethod == s3g::AmbiStereoMethod::HypercardioidXy) {
        drawing::Path negative = drawing::Path::bezierPath();
        bool open = false;
        for (int i = 0; i <= steps; ++i) {
          const float rel = -180.0f + static_cast<float>(i) * 360.0f /
                                          static_cast<float>(steps);
          const float relr = rel * s3g::kPi / 180.0f;
          const float signedResponse = visualResponse(visualMethod, relr);
          const float response = std::abs(signedResponse);
          const float az = guiParams.rotationDeg + facingDeg + rel;
          const double radius =
              maxR * (0.06 + (0.30 + 0.10 * widthScale) * response);
          const drawing::Point raw =
              viewPointForAzEl(az, guiParams.micElevationDeg, radius);
          const drawing::Point center =
              viewPointForAzEl(az, guiParams.micElevationDeg, 0.0);
          const drawing::Point pt = drawing::makePoint(
              origin.x + raw.x - center.x, origin.y + raw.y - center.y);
          if (signedResponse < 0.0f) {
            if (!open) {
              (negative).moveToPoint(pt);
              open = true;
            } else {
              (negative).lineToPoint(pt);
            }
          } else {
            open = false;
          }
        }
        double dash[] = {5.0, 4.0};
        (negative).setLineDash_count_phase(dash, 2, 0.0);
        (negative).setLineWidth(1.8);
        drawing::setStroke(
            drawing::calibratedColor(leftSide ? 0xb2b2b2 : 0xf0f0f0, 0.72));
        (negative).stroke();
      }
      const drawing::Point axis =
          viewPointForAzEl(guiParams.rotationDeg + facingDeg,
                           guiParams.micElevationDeg, maxR * 0.34);
      const drawing::Point axisCenter = viewPointForAzEl(
          guiParams.rotationDeg + facingDeg, guiParams.micElevationDeg, 0.0);
      const drawing::Point shiftedAxis = drawing::makePoint(
          origin.x + axis.x - axisCenter.x, origin.y + axis.y - axisCenter.y);
      drawing::setStroke(drawing::calibratedColor(
          leftSide ? 0xb2b2b2 : 0xf0f0f0, leftSide ? 0.64 : 0.54));
      drawing::Path::strokeLineFromPoint_toPoint(origin, shiftedAxis);
    };

    const float rot = guiParams.rotationDeg;
    const drawing::Point centerPoint =
        viewPointForAzEl(rot, guiParams.micElevationDeg, 0.0);
    if (method == s3g::AmbiStereoMethod::MsCardioid) {
      drawPickupLobe(centerPoint, true, 0.0f,
                     s3g::AmbiStereoMethod::XyCardioid);
      drawPickupLobe(centerPoint, false, 90.0f,
                     s3g::AmbiStereoMethod::Blumlein);
      const drawing::Point mid =
          viewPointForAzEl(rot, guiParams.micElevationDeg, maxR * 0.34);
      const drawing::Point sidePlus =
          viewPointForAzEl(rot + 90.0f, guiParams.micElevationDeg, maxR * 0.27);
      const drawing::Point sideMinus =
          viewPointForAzEl(rot - 90.0f, guiParams.micElevationDeg, maxR * 0.27);
      drawing::drawAtPoint_withAttributes(
          "M", drawing::makePoint(mid.x + 7, mid.y - 7), small);
      drawing::drawAtPoint_withAttributes(
          "S+", drawing::makePoint(sidePlus.x + 7, sidePlus.y - 7), small);
      drawing::drawAtPoint_withAttributes(
          "S-", drawing::makePoint(sideMinus.x + 7, sideMinus.y - 7), small);
      drawing::setStroke(drawing::calibratedColor(0xbdbdbd, 0.34));
      drawing::Path::strokeLineFromPoint_toPoint(sidePlus, sideMinus);
      drawing::drawAtPoint_withAttributes(
          "L=M+S  R=M-S", drawing::makePoint(cx + 12, cy + 10), small);
    } else {
      double capsuleOffset = 0.0;
      if (method == s3g::AmbiStereoMethod::SpacedOmni) {
        capsuleOffset =
            maxR *
            (0.02 + 0.20 * std::clamp<double>(guiParams.abSpacingCm / 120.0f,
                                              0.0, 1.0));
      } else if (method == s3g::AmbiStereoMethod::OrtfStyle) {
        capsuleOffset = maxR * 0.075;
      }
      const drawing::Point leftOrigin = viewPointForAzEl(
          rot + 90.0f, guiParams.micElevationDeg, capsuleOffset);
      const drawing::Point rightOrigin = viewPointForAzEl(
          rot - 90.0f, guiParams.micElevationDeg, capsuleOffset);
      drawPickupLobe(leftOrigin, true, visualAngle, method);
      drawPickupLobe(rightOrigin, false, -visualAngle, method);
      drawing::setStroke(drawing::calibratedColor(0xd1d1d1, 0.30));
      drawing::Path::strokeLineFromPoint_toPoint(leftOrigin, rightOrigin);
      drawing::setFill(drawing::calibratedColor(0xd1d1d1, 0.78));
      drawing::rectFill(
          drawing::makeRect(leftOrigin.x - 2, leftOrigin.y - 2, 4, 4));
      drawing::rectFill(
          drawing::makeRect(rightOrigin.x - 2, rightOrigin.y - 2, 4, 4));
      if (method != s3g::AmbiStereoMethod::OrtfStyle) {
        auto shiftedAxisPoint = [&](drawing::Point origin,
                                    float facingDeg) -> drawing::Point {
          const drawing::Point axis = viewPointForAzEl(
              rot + facingDeg, guiParams.micElevationDeg, maxR * 0.34);
          const drawing::Point axisCenter =
              viewPointForAzEl(rot + facingDeg, guiParams.micElevationDeg, 0.0);
          return drawing::makePoint(origin.x + axis.x - axisCenter.x,
                                    origin.y + axis.y - axisCenter.y);
        };
        const drawing::Point leftAxis =
            shiftedAxisPoint(leftOrigin, visualAngle);
        const drawing::Point rightAxis =
            shiftedAxisPoint(rightOrigin, -visualAngle);
        drawing::drawAtPoint_withAttributes(
            "L MIC", drawing::makePoint(leftAxis.x + 8, leftAxis.y - 7), small);
        drawing::drawAtPoint_withAttributes(
            "R MIC", drawing::makePoint(rightAxis.x + 8, rightAxis.y - 7),
            small);
      }
      if (method == s3g::AmbiStereoMethod::SpacedOmni) {
        drawing::drawAtPoint_withAttributes(
            drawing::stringFormat("AB %.0fcm",
                                  static_cast<double>(guiParams.abSpacingCm)),
            drawing::makePoint(cx - 28, cy + 24), small);
      }
    }
    drawing::setFill(style.text);
    drawing::rectFill(drawing::makeRect(cx - 3, cy - 3, 6, 6));

    const double elevX = drawing::maxX(field) - 36.0;
    const double elevTop = field.origin.y + 62.0;
    const double elevH = 112.0;
    const double elevMid = elevTop + elevH * 0.5;
    const double elevNorm = std::clamp<double>(
        (guiParams.micElevationDeg + 90.0f) / 180.0f, 0.0, 1.0);
    const double elevY = elevTop + elevH * (1.0 - elevNorm);
    drawing::setStroke(drawing::calibratedColor(0x5d5d5d, 0.32));
    drawing::Path::strokeLineFromPoint_toPoint(
        drawing::makePoint(elevX, elevTop),
        drawing::makePoint(elevX, elevTop + elevH));
    drawing::Path::strokeLineFromPoint_toPoint(
        drawing::makePoint(elevX - 10, elevMid),
        drawing::makePoint(elevX + 10, elevMid));
    drawing::setFill(drawing::calibratedColor(0xd1d1d1, 0.78));
    drawing::rectFill(drawing::makeRect(elevX - 5, elevY - 3, 10, 6));
    drawing::drawAtPoint_withAttributes(
        "M-EL", drawing::makePoint(elevX - 14, elevTop - 17), small);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("%+.0f",
                              static_cast<double>(guiParams.micElevationDeg)),
        drawing::makePoint(elevX - 16, elevTop + elevH + 5), small);

    const uint32_t vcount = s3g::ambiStereoVirtualCount(guiParams.layout);
    for (uint32_t i = 0; i < vcount; ++i) {
      const auto pt = s3g::ambiStereoVirtualPoint(guiParams.layout, i);
      const drawing::Point a = projectPoint_inRect(pt, field);
      for (uint32_t j = i + 1; j < vcount; ++j) {
        const auto other = s3g::ambiStereoVirtualPoint(guiParams.layout, j);
        const float daz = std::abs(
            s3g::ambiStereoWrapSignedDeg(pt.azimuthDeg - other.azimuthDeg));
        const float del = std::abs(pt.elevationDeg - other.elevationDeg);
        const bool sameTier =
            del < 4.0f && daz <= (vcount <= 12u ? 90.5f : 45.5f);
        const bool verticalTier = daz < 8.0f && del <= 70.0f;
        if (sameTier || verticalTier ||
            (vcount <= 12u && daz <= 90.5f && del <= 72.0f)) {
          const drawing::Point b = projectPoint_inRect(other, field);
          drawing::setStroke(drawing::calibratedColor(0x9a9a9a, 0.22));
          drawing::Path::strokeLineFromPoint_toPoint(a, b);
        }
      }
    }
    for (uint32_t i = 0; i < vcount; ++i) {
      const auto pt = s3g::ambiStereoVirtualPoint(guiParams.layout, i);
      const drawing::Point a = projectPoint_inRect(pt, field);
      float l = 0.0f;
      float r = 0.0f;
      s3g::ambiStereoPickup(guiParams, pt.azimuthDeg, pt.elevationDeg, l, r);
      const double energy =
          std::clamp<double>((std::abs(l) + std::abs(r)) * 0.42, 0.18, 1.0);
      const double elevationTone =
          std::clamp<double>((pt.elevationDeg + 90.0f) / 180.0f, 0.0, 1.0);
      const double size = 5.0 + energy * 3.0;
      drawing::setFill(
          drawing::calibratedColor(0xc8c8c8, 0.42 + energy * 0.48));
      drawing::rectFill(
          drawing::makeRect(a.x - size * 0.5, a.y - size * 0.5, size, size));
      drawing::setStroke(drawing::calibratedColor(
          elevationTone > 0.55 ? 0xf0f0f0 : 0x8a8a8a, 0.64));
      drawing::frameRect(
          drawing::makeRect(a.x - size * 0.5, a.y - size * 0.5, size, size));
      if (i < 8u || (i % 4u) == 0u) {
        drawing::drawAtPoint_withAttributes(
            drawing::stringFormat("%u", i + 1),
            drawing::makePoint(a.x + 7, a.y - 6), small);
      }
    }
    drawing::String viewName =
        _viewMode == 0
            ? "TOP VIEW / VIRTUAL MICS + SPEAKER FIELD"
            : (_viewMode == 1 ? "SIDE VIEW / HEIGHT FOLD + VIRTUAL FIELD"
                              : "3/4 VIEW / VIRTUAL FIELD GEOMETRY");
    drawing::drawAtPoint_withAttributes(viewName, drawing::makePoint(40, 84),
                                        small);
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat("%@ / %@ / %@",
                              drawing::stringFromUtf8(layoutName(
                                  static_cast<uint32_t>(guiParams.layout))),
                              drawing::stringFromUtf8(methodName(
                                  static_cast<uint32_t>(guiParams.method))),
                              drawing::stringFromUtf8(weightingName(
                                  static_cast<uint32_t>(guiParams.weighting)))),
        drawing::makePoint(field.origin.x, drawing::maxY(field) - 42.0), small);
    drawing::String cameraText =
        _viewMode == 0
            ? "CAM TOP · AED"
            : (_viewMode == 1
                   ? "CAM SIDE · AZ / EL"
                   : drawing::stringFormat("CAM FREE  AZ %+.0f°  EL %+.0f°",
                                           _viewYawDeg, -_viewPitchDeg));
    drawing::drawAtPoint_withAttributes(
        cameraText,
        drawing::makePoint(field.origin.x, drawing::maxY(field) - 23.0), small);

    drawing::Rect side = drawing::makeRect(592, 34, 336, 594);
    drawing::Rect output =
        drawing::makeRect(side.origin.x, 34, side.size.width, 118);
    drawing::Rect decoder =
        drawing::makeRect(side.origin.x, 164, side.size.width, 128);
    drawing::Rect pickup =
        drawing::makeRect(side.origin.x, 304, side.size.width, 184);
    drawing::Rect fieldMix =
        drawing::makeRect(side.origin.x, 500, side.size.width, 128);

    drawing::drawPanelFrame(output.origin.x, output.origin.y, output.size.width,
                            output.size.height, style);
    drawing::drawPanelHeader("OUTPUT", true, output.origin.x, output.origin.y,
                             output.size.width, 21, text, style);
    drawSlider_value_norm_y_small(
        "OUT",
        drawing::stringFormat("%+.1f dB",
                              static_cast<double>(guiParams.outputGainDb)),
        (guiParams.outputGainDb + 24.0) / 48.0, 70, small);

    const float pkL = p->outputPeakLeft.exchange(
        p->outputPeakLeft.load(std::memory_order_relaxed) * 0.92f,
        std::memory_order_relaxed);
    const float pkR = p->outputPeakRight.exchange(
        p->outputPeakRight.load(std::memory_order_relaxed) * 0.92f,
        std::memory_order_relaxed);
    auto drawMeter = [&](double y, drawing::String name, float peak) {
      const double x = 638.0;
      const double w = 196.0;
      const double db = 20.0 * std::log10(std::max(0.000001f, peak));
      const double norm = std::clamp<double>((db + 60.0) / 60.0, 0.0, 1.0);
      drawing::Rect r = drawing::makeRect(x, y, w, 15);
      drawing::setFill(style.strip);
      drawing::rectFill(r);
      drawing::setFill(style.fill);
      drawing::rectFill(drawing::makeRect(r.origin.x + 1, r.origin.y + 1,
                                          (r.size.width - 2) * norm,
                                          r.size.height - 2));
      drawing::setStroke(style.grid);
      drawing::frameRect(r);
      drawing::drawAtPoint_withAttributes(name, drawing::makePoint(612, y - 1),
                                          small);
      drawing::drawAtPoint_withAttributes(drawing::stringFormat("%+4.1f", db),
                                          drawing::makePoint(850, y - 1),
                                          small);
    };
    drawMeter(98, "L", pkL);
    drawMeter(120, "R", pkR);

    drawing::drawPanelFrame(decoder.origin.x, decoder.origin.y,
                            decoder.size.width, decoder.size.height, style);
    drawing::drawPanelHeader("DECODER", true, decoder.origin.x,
                             decoder.origin.y, decoder.size.width, 21, text,
                             style);
    drawMenu_value_y_small("FIELD",
                           drawing::stringFromUtf8(layoutName(
                               static_cast<uint32_t>(guiParams.layout))),
                           204, small);
    drawMenu_value_y_small(
        "ORDER", drawing::stringFormat("%uOA", guiParams.order), 226, small);
    drawMenu_value_y_small("WGT",
                           drawing::stringFromUtf8(weightingName(
                               static_cast<uint32_t>(guiParams.weighting))),
                           248, small);
    drawMenu_value_y_small("AGN",
                           drawing::stringFromUtf8(autogainName(
                               static_cast<uint32_t>(guiParams.autogain))),
                           270, small);

    drawing::drawPanelFrame(pickup.origin.x, pickup.origin.y, pickup.size.width,
                            pickup.size.height, style);
    drawing::drawPanelHeader("STEREO PICKUP", true, pickup.origin.x,
                             pickup.origin.y, pickup.size.width, 21, text,
                             style);
    drawMenu_value_y_small("MTHD",
                           drawing::stringFromUtf8(methodName(
                               static_cast<uint32_t>(guiParams.method))),
                           344, small);
    drawSlider_value_norm_y_small(
        "WDTH",
        drawing::stringFormat(
            "%.0f%%", static_cast<double>(guiParams.stereoWidthPercent)),
        guiParams.stereoWidthPercent / 200.0, 366, small);
    drawSlider_value_norm_y_small(
        "ANG",
        drawing::stringFormat("%.0f",
                              static_cast<double>(guiParams.micAngleDeg)),
        (guiParams.micAngleDeg - 20.0) / 120.0, 388, small);
    drawSlider_value_norm_y_small(
        "ROT",
        drawing::stringFormat("%+.0f",
                              static_cast<double>(guiParams.rotationDeg)),
        (guiParams.rotationDeg + 180.0) / 360.0, 410, small);
    drawSlider_value_norm_y_small(
        "DIR",
        drawing::stringFormat(
            "%.0f%%", static_cast<double>(guiParams.directivityPercent)),
        guiParams.directivityPercent / 100.0, 432, small);
    drawSlider_value_norm_y_small(
        "AB",
        drawing::stringFormat("%.0fcm",
                              static_cast<double>(guiParams.abSpacingCm)),
        guiParams.abSpacingCm / 120.0, 454, small);
    drawSlider_value_norm_y_small(
        "M-EL",
        drawing::stringFormat("%+.0f",
                              static_cast<double>(guiParams.micElevationDeg)),
        (guiParams.micElevationDeg + 90.0) / 180.0, 476, small);

    drawing::drawPanelFrame(fieldMix.origin.x, fieldMix.origin.y,
                            fieldMix.size.width, fieldMix.size.height, style);
    drawing::drawPanelHeader("FIELD MIX", true, fieldMix.origin.x,
                             fieldMix.origin.y, fieldMix.size.width, 21, text,
                             style);
    drawSlider_value_norm_y_small(
        "REAR",
        drawing::stringFormat("%.0f%%",
                              static_cast<double>(guiParams.rearRejectPercent)),
        guiParams.rearRejectPercent / 100.0, 540, small);
    drawSlider_value_norm_y_small(
        "HGT",
        drawing::stringFormat("%.0f%%",
                              static_cast<double>(guiParams.heightFoldPercent)),
        guiParams.heightFoldPercent / 100.0, 562, small);
    drawSlider_value_norm_y_small(
        "DIF",
        drawing::stringFormat(
            "%.0f%%", static_cast<double>(guiParams.diffuseBlendPercent)),
        guiParams.diffuseBlendPercent / 100.0, 584, small);
    drawSlider_value_norm_y_small(
        "BASS",
        drawing::stringFormat("%.0f",
                              static_cast<double>(guiParams.bassMonoHz)),
        guiParams.bassMonoHz / 300.0, 606, small);

    if (_openMenu > 0 && _menuItems > 0) {
      drawing::String layoutItems[] = {"Quad virtual", "8ch cube",
                                       "12ch dodeca", "24ch dome",
                                       "32ch sphere"};
      drawing::String orderItems[] = {"1OA", "2OA", "3OA", "4OA",
                                      "5OA", "6OA", "7OA"};
      drawing::String methodItems[] = {"XY cardioid",      "ORTF-style",
                                       "MS cardioid",      "Blumlein",
                                       "Spaced omni",      "Dual shotgun",
                                       "Wide cardioid",    "Supercardioid XY",
                                       "Hypercardioid XY", "Height focus"};
      drawing::String weightItems[] = {"Projection", "Energy-normalized",
                                       "Max-rE", "Custom"};
      drawing::String gainItems[] = {"Off", "Power/sqrt(N)", "Energy sum"};
      drawing::String *items = layoutItems;
      int selected = static_cast<int>(guiParams.layout);
      if (_openMenu == 2) {
        items = methodItems;
        selected = static_cast<int>(guiParams.method);
      }
      if (_openMenu == 3) {
        items = weightItems;
        selected = static_cast<int>(guiParams.weighting);
      }
      if (_openMenu == 4) {
        items = gainItems;
        selected = static_cast<int>(guiParams.autogain);
      }
      if (_openMenu == 5) {
        items = orderItems;
        selected = static_cast<int>(guiParams.order) - 1;
      }
      const double itemH = 18.0;
      drawing::Rect menu = drawing::makeRect(_menuOrigin.x, _menuOrigin.y, 176,
                                             itemH * _menuItems);
      drawing::drawDropdownMenu(menu, itemH, items, _menuItems, selected,
                                _hoverMenuItem, small, style);
    }
    contentTransform.reset();
  }
  void updateSliderAtPoint(drawing::Point pt) {

    const double norm = std::clamp((pt.x - 712.0) / 122.0, 0.0, 1.0);
    switch (_dragSlider) {
    case 0:
      setParam_value(kParamOrder, 1.0 + norm * 6.0);
      break;
    case 3:
      setParam_value(kParamWidth, norm * 200.0);
      break;
    case 4:
      setParam_value(kParamAngle, 20.0 + norm * 120.0);
      break;
    case 5:
      setParam_value(kParamRotation, -180.0 + norm * 360.0);
      break;
    case 6:
      setParam_value(kParamDirectivity, norm * 100.0);
      break;
    case 7:
      setParam_value(kParamRearReject, norm * 100.0);
      break;
    case 8:
      setParam_value(kParamHeightFold, norm * 100.0);
      break;
    case 9:
      setParam_value(kParamDiffuse, norm * 100.0);
      break;
    case 12:
      setParam_value(kParamAbSpacing, norm * 120.0);
      break;
    case 13:
      setParam_value(kParamBassMono, norm * 300.0);
      break;
    case 14:
      setParam_value(kParamMicElevation, -90.0 + norm * 180.0);
      break;
    case 15:
      setParam_value(kParamOutputGain, -24.0 + norm * 48.0);
      break;
    default:
      break;
    }
  }
  void updateMenuHover(drawing::Point pt) {

    if (_openMenu <= 0 || _menuItems == 0)
      return;
    drawing::Rect menu =
        drawing::makeRect(_menuOrigin.x, _menuOrigin.y, 176, 18.0 * _menuItems);
    const int hover = drawing::dropdownHitIndex(pt, menu, 18.0, _menuItems);
    if (hover != _hoverMenuItem) {
      _hoverMenuItem = hover;
      setNeedsDisplay(true);
    }
  }
  void mouseDown(drawing::Event *event) {

    drawing::Point pt =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    auto *titlePlugin = static_cast<Plugin *>(_plugin);
    const auto titleBand = drawing::encoderTitleBand(kGuiWidth, kGuiHeight);
    if (handleTitleClick(pt, titleBand))
      return;
    pt.y -= kContentTranslation;
    if (_openMenu > 0) {
      drawing::Rect menu = drawing::makeRect(_menuOrigin.x, _menuOrigin.y, 176,
                                             18.0 * _menuItems);
      const int hit = drawing::dropdownHitIndex(pt, menu, 18.0, _menuItems);
      if (hit >= 0) {
        if (_openMenu == 1)
          setParam_value(kParamLayout, hit);
        if (_openMenu == 2)
          setParam_value(kParamMethod, hit);
        if (_openMenu == 3)
          setParam_value(kParamWeighting, hit);
        if (_openMenu == 4)
          setParam_value(kParamAutogain, hit);
        if (_openMenu == 5)
          setParam_value(kParamOrder, hit + 1u);
      }
      _openMenu = 0;
      _hoverMenuItem = -1;
      setNeedsDisplay(true);
      return;
    }
    const drawing::Rect fieldPanel = drawing::makeRect(12, 34, 568, 594);
    for (int i = 0; i < 3; ++i) {
      if (drawing::contains(pt, viewButtonRect_inRect(i, fieldPanel))) {
        _viewMode = i;
        if (i == 0) {
          _viewYawDeg = 0.0f;
          _viewPitchDeg = 0.0f;
        } else if (i == 1) {
          _viewYawDeg = 0.0f;
          _viewPitchDeg = 0.0f;
        } else {
          _viewYawDeg = -35.0f;
          _viewPitchDeg = -42.0f;
        }
        setNeedsDisplay(true);
        return;
      }
    }
    const drawing::Rect field = drawing::makeRect(28, 70, 536, 542);
    if (drawing::contains(pt, field)) {
      if (_viewMode == 0) {
        _viewYawDeg = 0.0f;
        _viewPitchDeg = -89.0f;
      } else if (_viewMode == 1) {
        _viewYawDeg = 0.0f;
        _viewPitchDeg = 0.0f;
      }
      _dragView = true;
      _lastDragPoint = pt;
      return;
    }
    struct HitRow {
      int index;
      double y;
      bool menu;
      int openMenu;
      uint32_t menuItems;
    };
    const HitRow rows[] = {
        {15, 70, false, 0, 0},  {1, 204, true, 1, 5},   {0, 226, true, 5, 7},
        {10, 248, true, 3, 4},  {11, 270, true, 4, 3},  {2, 344, true, 2, 10},
        {3, 366, false, 0, 0},  {4, 388, false, 0, 0},  {5, 410, false, 0, 0},
        {6, 432, false, 0, 0},  {12, 454, false, 0, 0}, {14, 476, false, 0, 0},
        {7, 540, false, 0, 0},  {8, 562, false, 0, 0},  {9, 584, false, 0, 0},
        {13, 606, false, 0, 0},
    };
    for (const auto &row : rows) {
      drawing::Rect r = drawing::makeRect(596, row.y - 6, 316, 22);
      if (!drawing::contains(pt, r))
        continue;
      if (row.menu) {
        _openMenu = row.openMenu;
        _menuItems = row.menuItems;
        _menuOrigin = drawing::makePoint(712, row.y + 17);
        _hoverMenuItem = -1;
        setNeedsDisplay(true);
        return;
      }
      auto sliderParam = [](int index) -> clap_id {
        switch (index) {
        case 3:
          return kParamWidth;
        case 4:
          return kParamAngle;
        case 5:
          return kParamRotation;
        case 6:
          return kParamDirectivity;
        case 7:
          return kParamRearReject;
        case 8:
          return kParamHeightFold;
        case 9:
          return kParamDiffuse;
        case 12:
          return kParamAbSpacing;
        case 13:
          return kParamBassMono;
        case 14:
          return kParamMicElevation;
        case 15:
          return kParamOutputGain;
        default:
          return CLAP_INVALID_ID;
        }
      };
      const clap_id param = sliderParam(row.index);
      double defaultValue = 0.0;
      if (drawing::sliderDoubleClickDefault(event, &titlePlugin->plugin, param,
                                            &defaultValue)) {
        setParam_value(param, defaultValue);
        _dragSlider = -1;
        return;
      }
      _dragSlider = row.index;
      updateSliderAtPoint(pt);
      return;
    }
  }
  void mouseDragged(drawing::Event *event) {

    drawing::Point pt =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    pt.y -= kContentTranslation;
    updateMenuHover(pt);
    if (_dragView) {
      const double dx = pt.x - _lastDragPoint.x;
      const double dy = pt.y - _lastDragPoint.y;
      if (_viewMode != 2)
        _viewMode = 2;
      _viewYawDeg += static_cast<float>(dx) * 0.45f;
      _viewPitchDeg = std::clamp(_viewPitchDeg + static_cast<float>(dy) * 0.35f,
                                 -89.0f, 89.0f);
      _lastDragPoint = pt;
      setNeedsDisplay(true);
      return;
    }
    if (_dragSlider >= 0)
      updateSliderAtPoint(pt);
  }
  void mouseMoved(drawing::Event *event) {

    drawing::Point pt =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    pt.y -= kContentTranslation;
    updateMenuHover(pt);
  }
  void mouseUp(drawing::Event *event) {
    (void)event;
    _dragSlider = -1;
    _dragView = false;
  }
  void markStateDirty() {
    if (p.host && p.host->get_extension) {
      auto *state = static_cast<const clap_host_state_t *>(
          p.host->get_extension(p.host, CLAP_EXT_STATE));
      if (state && state->mark_dirty)
        state->mark_dirty(p.host);
    }
  }
  bool handleTitleClick(drawing::Point point,
                        const s3g::gui_layout::EncoderTitleBand &band) {
    if (drawing::contains(point, drawing::cocoaRect(band.presetMenu)))
      resetDecoder();
    else if (drawing::contains(point, drawing::cocoaRect(band.loadButton)))
      presetDialog(false);
    else if (drawing::contains(point, drawing::cocoaRect(band.saveButton)))
      presetDialog(true);
    else
      return false;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                  presetName.c_str());
    service();
    invalid();
    return true;
  }
  void resetDecoder() {
    const double out = value(preservedOutput);
    DecoderParams defaults{};
    std::vector<std::pair<clap_id, double>> values;
    for (const auto &i : infos)
      if (i.id != preservedOutput)
        values.emplace_back(i.id, readDecoderScalar(defaults, i.id));
    if (batch(values)) {
      setDecoderScalar(defaults, preservedOutput, out);
      publishDecoderStateBase(p, defaults);
      p.decoderLayoutCount.store(decoderHiddenCount(defaults));
      presetName = "INIT";
    }
  }
};
} // namespace decoder_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new decoder_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
