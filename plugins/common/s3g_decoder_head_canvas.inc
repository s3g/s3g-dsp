// Literal Cocoa geometry and interaction adaptation; see
// DECODERS_VSTGUI_PARITY.md.
namespace decoder_canvas {
namespace drawing = s3g::portable_gui::decoder_drawing;
namespace foundation = s3g::portable_gui::foundation;
using namespace VSTGUI;
using namespace s3g::portable_gui::routing;
using Kind = s3g::clap_gui::ParamEventKind;
constexpr const char *pluginName = "Ambi Decoder Head";
constexpr const char *pluginTitle = "s3g AMBI DECODER HEAD";
constexpr clap_id preservedOutput = kParamOutput;
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
    _menuOrigin = drawing::makePoint(0, 0);
    _menuItems = 0;

    _viewMode = 0;
    _viewYawDeg = 0.0;
    _viewPitchDeg = 0.0;
    _viewZoom = 1.0;
    _dragView = false;
    _lastDragPoint = drawing::makePoint(0, 0);
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
  void wheel(MouseWheelEvent &event) override {
    drawing::Event e{
        {event.mousePosition.x, event.mousePosition.y}, 1, event.deltaY};
    scrollWheel(&e);
    event.consumed = true;
    invalid();
  }
  bool pointerEditing = false;
  DecoderParams guiParams{};

  void *_plugin;
  int _dragSlider;
  int _openMenu;
  int _hoverMenuItem;
  drawing::Point _menuOrigin;
  uint32_t _menuItems;

  int _viewMode;
  double _viewYawDeg;
  double _viewPitchDeg;
  double _viewZoom;
  bool _dragView;
  drawing::Point _lastDragPoint;
  char _titlePresetName[64];
  void drawSlider_value_norm_y_attrs_style(drawing::String name,
                                           drawing::String value, double norm,
                                           double y, drawing::Attrs attrs,
                                           drawing::Style &style) {

    (void)attrs;
    drawing::drawSlider(name, value, norm, y, drawing::softLabelAttrs(),
                        drawing::softValueAttrs(), style, 606, 724, 852, 128);
  }
  void drawMenu_value_y_attrs_style(drawing::String name, drawing::String value,
                                    double y, drawing::Attrs attrs,
                                    drawing::Style &style) {

    (void)attrs;
    drawing::drawMenu(name, value, y, drawing::softLabelAttrs(),
                      drawing::softValueAttrs(), style, 606, 724, 176);
  }
  drawing::Rect viewButtonRect_inRect(int index, drawing::Rect rect) {

    const double w = 38.0;
    const double h = 13.0;
    const double gap = 5.0;
    const double x = drawing::maxX(rect) - 10.0 -
                     (3.0 - static_cast<double>(index)) * w -
                     (2.0 - static_cast<double>(index)) * gap;
    return drawing::makeRect(x, rect.origin.y + 4.0, w, h);
  }
  drawing::Rect zoomButtonRect_inRect(int index, drawing::Rect rect) {

    const double w = 18.0;
    const double h = 13.0;
    const double gap = 4.0;
    const double viewStart = viewButtonRect_inRect(0, rect).origin.x;
    const double x = viewStart - 12.0 - (2.0 - static_cast<double>(index)) * w -
                     (1.0 - static_cast<double>(index)) * gap;
    return drawing::makeRect(x, rect.origin.y + 4.0, w, h);
  }
  void drawViewButtonsInRect_attrs(drawing::Rect rect, drawing::Attrs attrs) {

    static drawing::String labels[] = {"TOP", "SIDE", "3/4"};
    drawing::Style style;
    for (int i = 0; i < 3; ++i) {
      drawing::drawToolboxHeaderButton(viewButtonRect_inRect(i, rect), rect,
                                       labels[i], i == _viewMode, attrs, style);
    }
  }
  void drawZoomButtonsInRect_attrs(drawing::Rect rect, drawing::Attrs attrs) {

    static drawing::String labels[] = {"-", "+"};
    drawing::Style style;
    for (int i = 0; i < 2; ++i) {
      drawing::drawToolboxHeaderButton(zoomButtonRect_inRect(i, rect), rect,
                                       labels[i], false, attrs, style);
    }
  }
  void setViewPreset(int mode) {

    _viewMode = mode;
    if (mode == 0) {
      _viewYawDeg = 0.0;
      _viewPitchDeg = 0.0;
    } else if (mode == 1) {
      _viewYawDeg = 0.0;
      _viewPitchDeg = 85.0;
    } else {
      _viewYawDeg = 135.0;
      _viewPitchDeg = 42.0;
    }
    setNeedsDisplay(true);
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
    const float titlePeak = std::max(p->peakL.load(std::memory_order_relaxed),
                                     p->peakR.load(std::memory_order_relaxed));
    drawing::String titleStatus = drawing::peakDbText(titlePeak);
    drawing::drawDecoderTitleBand(
        "s3g AMBI DECODER HEAD", drawing::stringFromUtf8(_titlePresetName),
        titleStatus, drawing::encoderTitleBand(930.0, 720.0), titleAttrs, text,
        small, style);

    std::optional<VSTGUI::CDrawContext::Transform> contentTransform;
    contentTransform.emplace(*context, VSTGUI::CGraphicsTransform{}.translate(
                                           0., kContentTranslation));

    drawing::Rect fieldPanel = drawing::makeRect(12, 34, 568, 664);
    drawing::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y,
                            fieldPanel.size.width, fieldPanel.size.height,
                            style);
    drawing::drawPanelHeader("HEAD FIELD", true, fieldPanel.origin.x,
                             fieldPanel.origin.y, fieldPanel.size.width, 21,
                             text, style);
    drawZoomButtonsInRect_attrs(fieldPanel, small);
    drawViewButtonsInRect_attrs(fieldPanel, small);
    drawing::Rect field = drawing::makeRect(28, 82, 536, 502);
    drawing::setFill(drawing::calibratedColor(0x101010));
    drawing::rectFill(field);
    drawing::setStroke(style.grid);
    drawing::frameRect(field);
    const double cx = field.origin.x + field.size.width * 0.50;
    const double cy = field.origin.y + field.size.height * 0.56;
    const double r = std::min(field.size.width, field.size.height) * 0.26 *
                     std::clamp<double>(_viewZoom, 0.65, 1.80);
    drawing::setStroke(drawing::calibratedColor(0x686868, 0.22));
    drawing::Path::strokeLineFromPoint_toPoint(
        drawing::makePoint(field.origin.x + 28, cy),
        drawing::makePoint(drawing::maxX(field) - 28, cy));
    drawing::Path::strokeLineFromPoint_toPoint(
        drawing::makePoint(cx, field.origin.y + 28),
        drawing::makePoint(cx, drawing::maxY(field) - 28));

    auto project3 = [&](double x, double y, double z,
                        double radius) -> drawing::Point {
      if (_viewMode == 0)
        return drawing::makePoint(cx + x * radius, cy + y * radius);
      if (_viewMode == 1)
        return drawing::makePoint(cx + x * radius * 0.94,
                                  cy - z * radius * 0.94);
      const double yaw = _viewYawDeg * s3g::kPi / 180.0;
      const double pitch = _viewPitchDeg * s3g::kPi / 180.0;
      const double x1 = x * std::cos(yaw) - y * std::sin(yaw);
      const double y1 = x * std::sin(yaw) + y * std::cos(yaw);
      const double y2 = y1 * std::cos(pitch) - z * std::sin(pitch);
      return drawing::makePoint(cx + x1 * radius, cy + y2 * radius * 0.86);
    };
    auto project = [&](float azDeg, float elDeg,
                       double radius) -> drawing::Point {
      const s3g::Vec3 direction = s3g::ambiHeadListenerRelativeDirection(
          azDeg, elDeg, guiParams.yawDeg, guiParams.pitchDeg);
      const double x = -direction.y;
      const double y = -direction.x;
      const double z = direction.z;
      return project3(x, y, z, radius);
    };

    const auto spec = s3g::ambiHeadProfileSpec(guiParams.head);
    const double shapeScale =
        std::clamp<double>(spec.widthCm / 18.0f, 0.86, 1.18);
    const double earScale = 0.85 + (guiParams.headWidthCm - 15.0) / 12.0;
    const double pin =
        std::clamp<double>(guiParams.pinnaPercent / 100.0f, 0.0, 1.0);
    const double headW = r * 0.42 * shapeScale;
    const double headD = r * 0.62;
    const double headH = r * 0.88;
    auto headPoint = [&](double lateral,
                         double longitudinal) -> drawing::Point {
      if (_viewMode == 0) {
        return project3(lateral * headW / r, longitudinal * headD / r, 0.0, r);
      }
      if (_viewMode == 1) {
        return project3(lateral * headW / r, 0.0, longitudinal * headH / r, r);
      }
      return project3(lateral * headW / r, longitudinal * headD * 0.32 / r,
                      longitudinal * headH * 0.82 / r, r);
    };
    const drawing::Point headOutline[]{
        headPoint(-0.32, -0.92), headPoint(-0.78, -0.52),
        headPoint(-0.96, 0.12),  headPoint(-0.62, 0.76),
        headPoint(0.0, 0.98),    headPoint(0.62, 0.76),
        headPoint(0.96, 0.12),   headPoint(0.78, -0.52),
        headPoint(0.32, -0.92),
    };
    drawing::Path silhouette = drawing::Path::bezierPath();
    (silhouette).moveToPoint(headOutline[0]);
    for (size_t i = 1u; i < sizeof(headOutline) / sizeof(headOutline[0]); ++i)
      (silhouette).lineToPoint(headOutline[i]);
    (silhouette).closePath();
    drawing::setFill(drawing::calibratedColor(0x1d1d1d, 1.0));
    (silhouette).fill();
    drawing::setStroke(drawing::calibratedColor(0xc6c6c6, 0.82));
    (silhouette).setLineWidth(1.6);
    (silhouette).stroke();
    drawing::Path guide = drawing::Path::bezierPath();
    if (_viewMode == 0) {
      // The brow/nose guide is edge-on from above. Keep only a short
      // anterior sliver so it cannot be read as a face seen from the front.
      (guide).moveToPoint(headPoint(-0.14, -0.78));
      (guide).lineToPoint(headPoint(0.14, -0.78));
    } else {
      // In side and free 3/4 views the orientation mark remains visually
      // upright: the brow sits above the longer nose/centre stem.
      drawing::Point marker = _viewMode == 1 ? drawing::makePoint(cx, cy)
                                             : project3(0.0, -headD * 0.10 / r,
                                                        headH * 0.04 / r, r);
      const double markerH = _viewMode == 1 ? headH * 0.62 : headH * 0.48;
      const double markerW = _viewMode == 1 ? headW * 0.82 : headW * 0.62;
      const double browY = marker.y - markerH * 0.18;
      (guide).moveToPoint(
          drawing::makePoint(marker.x, marker.y - markerH * 0.46));
      (guide).lineToPoint(
          drawing::makePoint(marker.x, marker.y + markerH * 0.54));
      (guide).moveToPoint(drawing::makePoint(marker.x - markerW * 0.5, browY));
      (guide).lineToPoint(drawing::makePoint(marker.x + markerW * 0.5, browY));
    }
    drawing::setStroke(drawing::calibratedColor(0x777777, 0.58));
    (guide).setLineWidth(0.9);
    (guide).stroke();

    const double earX = headW * earScale;
    const double earRadius = earX / r;
    auto drawPickupDiamond = [&](double sign) {
      const drawing::Point center = project3(sign * earRadius, 0.0, 0.04, r);
      const double half = 7.0 + pin * 2.0;
      drawing::Path diamond = drawing::Path::bezierPath();
      (diamond).moveToPoint(drawing::makePoint(center.x, center.y - half));
      (diamond).lineToPoint(drawing::makePoint(center.x + half, center.y));
      (diamond).lineToPoint(drawing::makePoint(center.x, center.y + half));
      (diamond).lineToPoint(drawing::makePoint(center.x - half, center.y));
      (diamond).closePath();
      drawing::setFill(drawing::calibratedColor(0x262626, 0.96));
      (diamond).fill();
      drawing::setStroke(drawing::calibratedColor(0xd2d2d2, 0.92));
      (diamond).setLineWidth(1.4);
      (diamond).stroke();
      const double inner = half * 0.42;
      drawing::Path inset = drawing::Path::bezierPath();
      (inset).moveToPoint(drawing::makePoint(center.x, center.y - inner));
      (inset).lineToPoint(drawing::makePoint(center.x + inner, center.y));
      (inset).lineToPoint(drawing::makePoint(center.x, center.y + inner));
      (inset).lineToPoint(drawing::makePoint(center.x - inner, center.y));
      (inset).closePath();
      drawing::setStroke(drawing::calibratedColor(0x8e8e8e, 0.72));
      (inset).stroke();
    };
    drawPickupDiamond(-1.0);
    drawPickupDiamond(1.0);
    const bool directDecode =
        guiParams.decodeMode == s3g::AmbiHeadDecodeMode::Direct;
    const uint32_t vcount = directDecode
                                ? s3g::kAmbiHeadMaxVirtualSpeakers
                                : s3g::ambiStereoVirtualCount(guiParams.layout);
    for (uint32_t i = 0; i < vcount; ++i) {
      const auto pt = directDecode
                          ? s3g::ambiHeadDirectPoint(i)
                          : s3g::ambiStereoVirtualPoint(guiParams.layout, i);
      const drawing::Point a =
          project(pt.azimuthDeg, pt.elevationDeg, r * 1.55);
      if (!directDecode || (i % 8u) == 0u) {
        drawing::setStroke(
            drawing::calibratedColor(0x9a9a9a, directDecode ? 0.18 : 0.28));
        drawing::Path::strokeLineFromPoint_toPoint(a,
                                                   drawing::makePoint(cx, cy));
      }
      const double sz =
          directDecode
              ? 3.2
              : 4.0 +
                    2.0 * std::max(0.0f,
                                   std::cos((pt.azimuthDeg - guiParams.yawDeg) *
                                            s3g::kPi / 180.0f));
      drawing::setFill(
          drawing::calibratedColor(0xd6d6d6, directDecode ? 0.38 : 0.62));
      drawing::rectFill(
          drawing::makeRect(a.x - sz * 0.5, a.y - sz * 0.5, sz, sz));
    }

    if (guiParams.mode == s3g::AmbiHeadMode::Transaural) {
      const float angleDeg = guiParams.speakerHalfAngleDeg;
      const double amount =
          std::clamp<double>(guiParams.xtcAmountPercent / 140.0f, 0.0, 1.0);
      const double preserve = std::clamp<double>(
          guiParams.stereoPreservePercent / 100.0f, 0.0, 1.0);
      const double sr = r * 1.90;
      const drawing::Point sl = project(angleDeg, 0.0f, sr);
      const drawing::Point srp = project(-angleDeg, 0.0f, sr);
      const drawing::Point earL = project3(-earRadius, 0.0, 0.02, r);
      const drawing::Point earR = project3(earRadius, 0.0, 0.02, r);
      drawing::setFill(drawing::calibratedColor(0xcfcfcf, 0.84));
      drawing::rectFill(drawing::makeRect(sl.x - 10, sl.y - 10, 20, 20));
      drawing::rectFill(drawing::makeRect(srp.x - 10, srp.y - 10, 20, 20));
      drawing::Path directL = drawing::Path::bezierPath();
      (directL).moveToPoint(sl);
      (directL).lineToPoint(earL);
      (directL).setLineWidth(1.2 + 1.8 * (1.0 - preserve));
      drawing::setStroke(
          drawing::calibratedColor(0x9ecf9c, 0.34 + 0.34 * (1.0 - preserve)));
      (directL).stroke();
      drawing::Path directR = drawing::Path::bezierPath();
      (directR).moveToPoint(srp);
      (directR).lineToPoint(earR);
      (directR).setLineWidth(1.2 + 1.8 * (1.0 - preserve));
      drawing::setStroke(
          drawing::calibratedColor(0x9ecf9c, 0.34 + 0.34 * (1.0 - preserve)));
      (directR).stroke();
      drawing::Path crossL = drawing::Path::bezierPath();
      const drawing::Point crossLC1 = drawing::makePoint(
          sl.x + (earR.x - sl.x) * 0.33, sl.y + (earR.y - sl.y) * 0.18 - 34.0);
      const drawing::Point crossLC2 = drawing::makePoint(
          sl.x + (earR.x - sl.x) * 0.70, sl.y + (earR.y - sl.y) * 0.86 + 34.0);
      (crossL).moveToPoint(sl);
      (crossL).curveToPoint_controlPoint1_controlPoint2(earR, crossLC1,
                                                        crossLC2);
      (crossL).setLineWidth(0.8 + 4.2 * amount);
      double dash[] = {5.0, 4.0};
      (crossL).setLineDash_count_phase(dash, 2, 0.0);
      drawing::setStroke(
          drawing::calibratedColor(0xe08f72, 0.18 + 0.58 * amount));
      (crossL).stroke();
      drawing::Path crossR = drawing::Path::bezierPath();
      const drawing::Point crossRC1 =
          drawing::makePoint(srp.x + (earL.x - srp.x) * 0.33,
                             srp.y + (earL.y - srp.y) * 0.18 - 34.0);
      const drawing::Point crossRC2 =
          drawing::makePoint(srp.x + (earL.x - srp.x) * 0.70,
                             srp.y + (earL.y - srp.y) * 0.86 + 34.0);
      (crossR).moveToPoint(srp);
      (crossR).curveToPoint_controlPoint1_controlPoint2(earL, crossRC1,
                                                        crossRC2);
      (crossR).setLineWidth(0.8 + 4.2 * amount);
      (crossR).setLineDash_count_phase(dash, 2, 0.0);
      drawing::setStroke(
          drawing::calibratedColor(0xe08f72, 0.18 + 0.58 * amount));
      (crossR).stroke();
      drawing::setFill(
          drawing::calibratedColor(0xe08f72, 0.06 + 0.16 * amount));
      (drawing::Path::bezierPathWithOvalInRect(
           drawing::makeRect(earL.x - 18 - amount * 8, earL.y - 28 - amount * 8,
                             36 + amount * 16, 56 + amount * 16)))
          .fill();
      (drawing::Path::bezierPathWithOvalInRect(
           drawing::makeRect(earR.x - 18 - amount * 8, earR.y - 28 - amount * 8,
                             36 + amount * 16, 56 + amount * 16)))
          .fill();
    }
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat(
            "%@ / %@ / %@",
            drawing::stringFromUtf8(
                modeName(static_cast<uint32_t>(guiParams.mode))),
            drawing::stringFromUtf8(
                headName(static_cast<uint32_t>(guiParams.head))),
            drawing::stringFromUtf8(
                directDecode
                    ? "Direct grid"
                    : layoutName(static_cast<uint32_t>(guiParams.layout)))),
        drawing::makePoint(40, 546), small);
    drawing::String cameraText =
        _viewMode == 0
            ? "CAM TOP · AED"
            : (_viewMode == 1
                   ? "CAM SIDE · AZ / EL"
                   : drawing::stringFormat("CAM FREE  AZ %+.0f°  EL %+.0f°",
                                           _viewYawDeg, _viewPitchDeg));
    drawing::drawAtPoint_withAttributes(cameraText, drawing::makePoint(40, 565),
                                        small);

    drawing::Rect side = drawing::makeRect(592, 34, 336, 664);
    drawing::Rect output =
        drawing::makeRect(side.origin.x, 34, side.size.width, 128);
    drawing::Rect decoder =
        drawing::makeRect(side.origin.x, 174, side.size.width, 128);
    drawing::Rect binaural =
        drawing::makeRect(side.origin.x, 314, side.size.width, 198);
    drawing::Rect transaural = drawing::makeRect(
        side.origin.x, binaural.origin.y + 210, side.size.width, 154);
    drawing::drawPanelFrame(decoder.origin.x, decoder.origin.y,
                            decoder.size.width, decoder.size.height, style);
    drawing::drawPanelHeader("DECODER", true, decoder.origin.x,
                             decoder.origin.y, decoder.size.width, 21, text,
                             style);
    drawMenu_value_y_attrs_style(
        "FIELD",
        drawing::stringFromUtf8(
            directDecode ? "Internal grid"
                         : layoutName(static_cast<uint32_t>(guiParams.layout))),
        210, small, style);
    drawMenu_value_y_attrs_style("ORDER",
                                 drawing::stringFormat("%uOA", guiParams.order),
                                 232, small, style);
    drawMenu_value_y_attrs_style(
        "WGT",
        drawing::stringFromUtf8(
            weightingName(static_cast<uint32_t>(guiParams.weighting))),
        254, small, style);
    drawMenu_value_y_attrs_style(
        "AGN",
        drawing::stringFromUtf8(
            autogainName(static_cast<uint32_t>(guiParams.autogain))),
        276, small, style);

    drawing::drawPanelFrame(binaural.origin.x, binaural.origin.y,
                            binaural.size.width, binaural.size.height, style);
    drawing::drawPanelHeader("BINAURAL", true, binaural.origin.x,
                             binaural.origin.y, binaural.size.width, 21, text,
                             style);
    drawMenu_value_y_attrs_style("MODE",
                                 drawing::stringFromUtf8(modeName(
                                     static_cast<uint32_t>(guiParams.mode))),
                                 binaural.origin.y + 40, small, style);
    drawMenu_value_y_attrs_style("HEAD",
                                 drawing::stringFromUtf8(headName(
                                     static_cast<uint32_t>(guiParams.head))),
                                 binaural.origin.y + 62, small, style);
    drawSlider_value_norm_y_attrs_style(
        "YAW",
        drawing::stringFormat("%+.0f", static_cast<double>(guiParams.yawDeg)),
        (guiParams.yawDeg + 180.0) / 360.0, binaural.origin.y + 84, small,
        style);
    drawSlider_value_norm_y_attrs_style(
        "PIT",
        drawing::stringFormat("%+.0f", static_cast<double>(guiParams.pitchDeg)),
        (guiParams.pitchDeg + 90.0) / 180.0, binaural.origin.y + 106, small,
        style);
    drawSlider_value_norm_y_attrs_style(
        "WID",
        drawing::stringFormat("%.0f%%",
                              static_cast<double>(guiParams.widthPercent)),
        guiParams.widthPercent / 200.0, binaural.origin.y + 128, small, style);
    drawSlider_value_norm_y_attrs_style(
        "PIN",
        drawing::stringFormat("%.0f%%",
                              static_cast<double>(guiParams.pinnaPercent)),
        guiParams.pinnaPercent / 100.0, binaural.origin.y + 150, small, style);
    drawSlider_value_norm_y_attrs_style(
        "EAR",
        drawing::stringFormat("%.1f",
                              static_cast<double>(guiParams.headWidthCm)),
        (guiParams.headWidthCm - 12.0) / 12.0, binaural.origin.y + 172, small,
        style);

    drawing::drawPanelFrame(transaural.origin.x, transaural.origin.y,
                            transaural.size.width, transaural.size.height,
                            style);
    drawing::drawPanelHeader("TRANSAURAL", true, transaural.origin.x,
                             transaural.origin.y, transaural.size.width, 21,
                             text, style);
    drawSlider_value_norm_y_attrs_style(
        "ROOM",
        drawing::stringFormat("%.0f%%",
                              static_cast<double>(guiParams.roomPercent)),
        guiParams.roomPercent / 100.0, transaural.origin.y + 40, small, style);
    drawSlider_value_norm_y_attrs_style(
        "XTC",
        drawing::stringFormat("%.0f%%",
                              static_cast<double>(guiParams.xtcAmountPercent)),
        guiParams.xtcAmountPercent / 140.0, transaural.origin.y + 62, small,
        style);
    drawMenu_value_y_attrs_style("XMOD",
                                 drawing::stringFromUtf8(xtcModeName(
                                     static_cast<uint32_t>(guiParams.xtcMode))),
                                 transaural.origin.y + 84, small, style);
    drawSlider_value_norm_y_attrs_style(
        "SPK",
        drawing::stringFormat(
            "%.0f", static_cast<double>(guiParams.speakerHalfAngleDeg)),
        (guiParams.speakerHalfAngleDeg - 10.0) / 50.0,
        transaural.origin.y + 106, small, style);
    drawSlider_value_norm_y_attrs_style(
        "LOW",
        drawing::stringFormat("%.0f",
                              static_cast<double>(guiParams.xtcLowProtectHz)),
        (guiParams.xtcLowProtectHz - 20.0) / 480.0, transaural.origin.y + 128,
        small, style);

    drawing::drawPanelFrame(output.origin.x, output.origin.y, output.size.width,
                            output.size.height, style);
    drawing::drawPanelHeader("OUTPUT", true, output.origin.x, output.origin.y,
                             output.size.width, 21, text, style);
    drawSlider_value_norm_y_attrs_style(
        "OUT",
        drawing::stringFormat("%+.1f dB",
                              static_cast<double>(guiParams.outputGainDb)),
        (guiParams.outputGainDb + 24.0) / 36.0, output.origin.y + 36, small,
        style);
    drawSlider_value_norm_y_attrs_style(
        "KEEP",
        drawing::stringFormat(
            "%.0f%%", static_cast<double>(guiParams.stereoPreservePercent)),
        guiParams.stereoPreservePercent / 100.0, output.origin.y + 58, small,
        style);

    auto meter = [&](double y, drawing::String label, float peak) {
      const double db = 20.0 * std::log10(std::max(0.000001f, peak));
      const double norm = std::clamp<double>((db + 60.0) / 60.0, 0.0, 1.0);
      drawing::drawAtPoint_withAttributes(label, drawing::makePoint(612, y - 1),
                                          small);
      drawing::setFill(style.strip);
      drawing::rectFill(drawing::makeRect(638, y, 190, 14));
      drawing::setFill(style.fill);
      drawing::rectFill(drawing::makeRect(639, y + 1, 188 * norm, 12));
      drawing::setStroke(style.grid);
      drawing::frameRect(drawing::makeRect(638, y, 190, 14));
      drawing::drawAtPoint_withAttributes(drawing::stringFormat("%+4.1f", db),
                                          drawing::makePoint(846, y - 1),
                                          small);
    };
    const float pkL =
        p->peakL.exchange(p->peakL.load(std::memory_order_relaxed) * 0.92f,
                          std::memory_order_relaxed);
    const float pkR =
        p->peakR.exchange(p->peakR.load(std::memory_order_relaxed) * 0.92f,
                          std::memory_order_relaxed);
    meter(output.origin.y + 82, "L", pkL);
    meter(output.origin.y + 104, "R", pkR);

    if (_openMenu > 0 && _menuItems > 0) {
      drawing::String fields[] = {"Internal grid", "Quad virtual",
                                  "8ch cube",      "12ch dodeca",
                                  "24ch dome",     "32ch sphere"};
      drawing::String weights[] = {"Projection", "Energy-normalized", "Max-rE"};
      drawing::String gains[] = {"Off", "Power/sqrt(N)", "Energy sum"};
      drawing::String heads[] = {"Medium head", "Small head", "Large head"};
      drawing::String modes[] = {"Binaural", "Transaural"};
      drawing::String xtcs[] = {"Feedforward", "Matrix inverse"};
      drawing::String orders[] = {"1OA", "2OA", "3OA", "4OA",
                                  "5OA", "6OA", "7OA"};
      drawing::String *items = fields;
      int selected = directDecode ? 0 : static_cast<int>(guiParams.layout) + 1;
      if (_openMenu == 3) {
        items = weights;
        selected = static_cast<int>(guiParams.weighting);
      }
      if (_openMenu == 4) {
        items = gains;
        selected = static_cast<int>(guiParams.autogain);
      }
      if (_openMenu == 5) {
        items = heads;
        selected = static_cast<int>(guiParams.head);
      }
      if (_openMenu == 6) {
        items = modes;
        selected = static_cast<int>(guiParams.mode);
      }
      if (_openMenu == 7) {
        items = xtcs;
        selected = static_cast<int>(guiParams.xtcMode);
      }
      if (_openMenu == 8) {
        items = orders;
        selected = static_cast<int>(guiParams.order - 1u);
      }
      drawing::drawDropdownMenu(drawing::makeRect(_menuOrigin.x, _menuOrigin.y,
                                                  176, 18.0 * _menuItems),
                                18.0, items, _menuItems, selected,
                                _hoverMenuItem, small, style);
    }
    contentTransform.reset();
  }
  void updateSliderAtPoint(drawing::Point pt) {

    const double norm = std::clamp((pt.x - 724.0) / 128.0, 0.0, 1.0);
    switch (_dragSlider) {
    case 0:
      setParam_value(kParamOrder, 1.0 + norm * 6.0);
      break;
    case 7:
      setParam_value(kParamYaw, -180.0 + norm * 360.0);
      break;
    case 8:
      setParam_value(kParamPitch, -90.0 + norm * 180.0);
      break;
    case 9:
      setParam_value(kParamWidth, norm * 200.0);
      break;
    case 10:
      setParam_value(kParamPinna, norm * 100.0);
      break;
    case 11:
      setParam_value(kParamRoom, norm * 100.0);
      break;
    case 12:
      setParam_value(kParamXtcAmount, norm * 140.0);
      break;
    case 14:
      setParam_value(kParamSpeakerAngle, 10.0 + norm * 50.0);
      break;
    case 15:
      setParam_value(kParamHeadWidth, 12.0 + norm * 12.0);
      break;
    case 16:
      setParam_value(kParamLowProtect, 20.0 + norm * 480.0);
      break;
    case 17:
      setParam_value(kParamPreserve, norm * 100.0);
      break;
    case 18:
      setParam_value(kParamOutput, -24.0 + norm * 36.0);
      break;
    default:
      break;
    }
  }
  void updateMenuHover(drawing::Point pt) {

    if (_openMenu <= 0 || _menuItems == 0)
      return;
    const int hover = drawing::dropdownHitIndex(
        pt,
        drawing::makeRect(_menuOrigin.x, _menuOrigin.y, 176, 18.0 * _menuItems),
        18.0, _menuItems);
    if (hover != _hoverMenuItem) {
      _hoverMenuItem = hover;
      setNeedsDisplay(true);
    }
  }
  void mouseDown(drawing::Event *event) {

    drawing::Point pt =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    auto *titlePlugin = static_cast<Plugin *>(_plugin);
    const auto titleBand = drawing::encoderTitleBand(930.0, 720.0);
    if (handleTitleClick(pt, titleBand))
      return;
    pt.y -= kContentTranslation;
    if (_openMenu > 0) {
      const int hit = drawing::dropdownHitIndex(
          pt,
          drawing::makeRect(_menuOrigin.x, _menuOrigin.y, 176,
                            18.0 * _menuItems),
          18.0, _menuItems);
      if (hit >= 0) {
        if (_openMenu == 2) {
          if (hit == 0) {
            setParam_value(kParamDecodeMode, 0.0);
          } else {
            setParam_value(kParamLayout, hit - 1);
            setParam_value(kParamDecodeMode, 1.0);
          }
        }
        if (_openMenu == 3)
          setParam_value(kParamWeighting, hit);
        if (_openMenu == 4)
          setParam_value(kParamAutogain, hit);
        if (_openMenu == 5)
          setParam_value(kParamHead, hit);
        if (_openMenu == 6)
          setParam_value(kParamMode, hit);
        if (_openMenu == 7)
          setParam_value(kParamXtcMode, hit);
        if (_openMenu == 8)
          setParam_value(kParamOrder, hit + 1u);
      }
      _openMenu = 0;
      _hoverMenuItem = -1;
      setNeedsDisplay(true);
      return;
    }
    const drawing::Rect side = drawing::makeRect(592, 34, 336, 664);
    const drawing::Rect fieldPanel = drawing::makeRect(12, 34, 568, 664);
    const drawing::Rect field = drawing::makeRect(28, 82, 536, 502);
    for (int i = 0; i < 2; ++i) {
      if (!drawing::contains(pt, zoomButtonRect_inRect(i, fieldPanel)))
        continue;
      _viewZoom = std::clamp(_viewZoom * (i == 0 ? 0.84 : 1.19), 0.65, 1.80);
      setNeedsDisplay(true);
      return;
    }
    for (int i = 0; i < 3; ++i) {
      if (!drawing::contains(pt, viewButtonRect_inRect(i, fieldPanel)))
        continue;
      setViewPreset(i);
      return;
    }
    if (drawing::contains(pt, field)) {
      if (_viewMode == 0) {
        _viewYawDeg = 0.0;
        _viewPitchDeg = 0.0;
      } else if (_viewMode == 1) {
        _viewYawDeg = 0.0;
        _viewPitchDeg = 85.0;
      }
      _dragView = true;
      _lastDragPoint = pt;
      return;
    }
    const drawing::Rect output =
        drawing::makeRect(side.origin.x, 34, side.size.width, 128);
    const drawing::Rect decoder =
        drawing::makeRect(side.origin.x, 174, side.size.width, 128);
    const drawing::Rect binaural =
        drawing::makeRect(side.origin.x, 314, side.size.width, 198);
    const drawing::Rect transaural = drawing::makeRect(
        side.origin.x, binaural.origin.y + 210, side.size.width, 154);
    (void)decoder;

    struct HitRow {
      int index;
      double y;
      bool menu;
      int openMenu;
      uint32_t menuItems;
    };
    HitRow rows[19];
    int count = 0;
    rows[count++] = {18, output.origin.y + 36, false, 0, 0};
    rows[count++] = {17, output.origin.y + 58, false, 0, 0};
    rows[count++] = {2, 210, true, 2, 6};
    rows[count++] = {0, 232, true, 8, 7};
    rows[count++] = {3, 254, true, 3, 3};
    rows[count++] = {4, 276, true, 4, 3};
    rows[count++] = {6, binaural.origin.y + 40, true, 6, 2};
    rows[count++] = {5, binaural.origin.y + 62, true, 5, 3};
    rows[count++] = {7, binaural.origin.y + 84, false, 0, 0};
    rows[count++] = {8, binaural.origin.y + 106, false, 0, 0};
    rows[count++] = {9, binaural.origin.y + 128, false, 0, 0};
    rows[count++] = {10, binaural.origin.y + 150, false, 0, 0};
    rows[count++] = {15, binaural.origin.y + 172, false, 0, 0};
    rows[count++] = {11, transaural.origin.y + 40, false, 0, 0};
    rows[count++] = {12, transaural.origin.y + 62, false, 0, 0};
    rows[count++] = {13, transaural.origin.y + 84, true, 7, 2};
    rows[count++] = {14, transaural.origin.y + 106, false, 0, 0};
    rows[count++] = {16, transaural.origin.y + 128, false, 0, 0};
    for (int i = 0; i < count; ++i) {
      if (!drawing::contains(pt,
                             drawing::makeRect(596, rows[i].y - 6, 316, 22)))
        continue;
      if (rows[i].menu) {
        _openMenu = rows[i].openMenu;
        _menuItems = rows[i].menuItems;
        _menuOrigin = drawing::makePoint(724, rows[i].y + 17);
        _hoverMenuItem = -1;
        setNeedsDisplay(true);
        return;
      }
      auto sliderParam = [](int index) -> clap_id {
        switch (index) {
        case 7:
          return kParamYaw;
        case 8:
          return kParamPitch;
        case 9:
          return kParamWidth;
        case 10:
          return kParamPinna;
        case 11:
          return kParamRoom;
        case 12:
          return kParamXtcAmount;
        case 14:
          return kParamSpeakerAngle;
        case 15:
          return kParamHeadWidth;
        case 16:
          return kParamLowProtect;
        case 17:
          return kParamPreserve;
        case 18:
          return kParamOutput;
        default:
          return CLAP_INVALID_ID;
        }
      };
      const clap_id param = sliderParam(rows[i].index);
      double defaultValue = 0.0;
      if (drawing::sliderDoubleClickDefault(event, &titlePlugin->plugin, param,
                                            &defaultValue)) {
        setParam_value(param, defaultValue);
        _dragSlider = -1;
        return;
      }
      _dragSlider = rows[i].index;
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
      _viewYawDeg += dx * 0.36;
      _viewPitchDeg = std::clamp(_viewPitchDeg - dy * 0.26, -82.0, 82.0);
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
  void scrollWheel(drawing::Event *event) {

    drawing::Point pt =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    pt.y -= kContentTranslation;
    const drawing::Rect field = drawing::makeRect(28, 82, 536, 502);
    if (!drawing::contains(pt, field))
      return;
    const double delta = event->scrollingDeltaY();
    _viewZoom = std::clamp(_viewZoom * (1.0 + delta * 0.018), 0.65, 1.80);
    setNeedsDisplay(true);
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
