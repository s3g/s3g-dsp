namespace node_canvas {
using namespace s3g::portable_gui::routing;
namespace foundation = s3g::portable_gui::foundation;
namespace layout = s3g::gui_layout;
constexpr const auto &family = layout::kMixerFamilyLayout;
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
constexpr const char *pluginName = "Ambi Mixer Node Bus 128";
constexpr const char *pluginTitle = "s3g AMBI MIXER NODE BUS 128CH";
#else
constexpr const char *pluginName = "Mixer Node Bus 128";
constexpr const char *pluginTitle = "s3g MIXER NODE BUS 128CH";
#endif
constexpr clap_id preservedOutput = kParamOutputGain;
constexpr const auto &stateExtension = stateExt;
double routingReadValue(const Plugin &p, clap_id id) {
  return latestPublishedValue(p, id);
}
void routingPublish(Plugin &p,
                    const std::vector<std::pair<clap_id, double>> &values) {
  auto next = latestParamsSnapshot(p, p.params);
  for (auto [id, v] : values)
    applyParam(next, id, v);
  publishControlParams(p, next);
}
void routingApply(Plugin &p, clap_id id, double v) {
  routingPublish(p, {{id, v}});
}
double routingRawValue(Plugin &p, clap_id id) {
  return latestPublishedValue(p, id);
}
void routingInitialize(Plugin &p) {
  p.plugin.plugin_data = &p;
  initializeDefaultParams(p);
  publishControlParams(p, p.params);
  init(&p.plugin);
}
#define S3G_ROUTING_CUSTOM_ACCESS 1
#include "s3g_routing_parameter_editor.inc"
#undef S3G_ROUTING_CUSTOM_ACCESS
class Editor final : public ParameterEditor {
public:
  explicit Editor(Plugin &p) : ParameterEditor(p) { refreshState(); }
  ~Editor() override { stopRefresh(); }
  void stopRefresh() override {
    ParameterEditor::stopRefresh();
    spatialDrag = 0;
  }
  double refreshPeriodMilliseconds() const override { return 1000. / 24.; }
  void refreshState() {
    state = latestParamsSnapshot(p, state);
    selectedNode = std::min(selectedNode, state.nodeCount - 1);
  }
  void service() override {
    refreshState();
    if (spatialDrag == 1 && draggedNode >= state.nodeCount) {
      end();
      spatialDrag = 0;
    }
  }
  double viewScaleForRect(Box r) const {
    return std::min(r.size.width, r.size.height) * .31 * viewZoom;
  }
  CPoint project(float x, float y, float z, Box r) const {
    const double scale = viewScaleForRect(r),
                 cx = r.origin.x + r.size.width * .5,
                 cy = r.origin.y + r.size.height * .52;
    if (viewMode == 0)
      return {cx - y * scale, cy - x * scale};
    if (viewMode == 1)
      return {cx - y * scale, cy - z * scale};
    const double yaw = viewYaw * (s3g::kPi / 180.),
                 pitch = viewPitch * (s3g::kPi / 180.),
                 xr = x * std::cos(yaw) - y * std::sin(yaw),
                 yr = x * std::sin(yaw) + y * std::cos(yaw),
                 v = z * std::cos(pitch) - xr * std::sin(pitch);
    return {cx - yr * scale, cy - v * scale};
  }
  CPoint nodePoint(unsigned i) const {
    auto n = state.nodes[i];
    return project(n.x, n.y, n.z, box(family.fieldPlot));
  }
  void setView(int index) {
    if (index < 3) {
      viewMode = index;
      if (index == 2) {
        viewYaw = -35.;
        viewPitch = 30.;
      }
    } else if (index == 3)
      viewZoom = std::max(.5, viewZoom / 1.25);
    else
      viewZoom = std::min(3., viewZoom * 1.25);
  }
  double nodeRadius(unsigned i) const {
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    return state.nodes[i].radius;
#else
    return state.nodes[i].scale;
#endif
  }
  void drawField() {
    const auto r = box(family.fieldPlot);
    fill(r, calibratedGray(0x10));
    const double cx = r.origin.x + r.size.width * .5,
                 cy = r.origin.y + r.size.height * .52,
                 scale = viewScaleForRect(r);
    line({r.origin.x + 22., cy}, {maxX(r) - 22., cy},
         calibratedGray(0x77, .22));
    line({cx, r.origin.y + 22.}, {cx, maxY(r) - 22.},
         calibratedGray(0x77, .22));
    for (unsigned i = 0; i < weights.size(); ++i)
      weights[i] = p.publishedNodeWeights[i].load(std::memory_order_relaxed);
    context->saveGlobalState();
    CRect clip;
    context->getClipRect(clip);
    clip.bound(r);
    context->setClipRect(clip);
    for (unsigned i = 0; i < state.nodeCount; ++i) {
      const auto n = state.nodes[i];
      if (!n.active)
        continue;
      const auto pt = nodePoint(i);
      const bool selected = i == selectedNode;
      const double active = std::clamp(weights[i], 0.f, 1.f),
                   radius = std::max(6., nodeRadius(i) * scale);
      const auto ringColor =
          overlaps(i) ? rgba(.08, 1., .26, selected ? .46 : .14 + active * .36)
                      : calibratedGray(selected ? 0xd8 : 0x8f,
                                       selected ? .34 : .10 + active * .18);
      ellipse(rect(pt.x - radius, pt.y - radius, radius * 2., radius * 2.),
              ringColor, false, selected ? 1.2 : .75);
    }
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
    for (unsigned i = 0; i < state.nodeCount; ++i) {
      const auto n = state.nodes[i];
      if (!n.active)
        continue;
      const bool selected = i == selectedNode;
      const unsigned count = std::min(std::max(1u, n.sourceChannels), 32u);
      std::array<CPoint, 32> points{};
      for (unsigned ch = 0; ch < count; ++ch) {
        auto rel = s3g::nodeTrackLayoutPoint(ch, count, n.sourceLayout);
        rel = s3g::nodeTrackRotatePoint(rel, n.rotateAzDeg, n.rotateElDeg);
        points[ch] = project(n.x + rel.x * n.scale, n.y + rel.y * n.scale,
                             n.z + rel.z * n.scale, r);
      }
      const auto edgeColor =
          calibratedGray(selected ? 0xd8 : 0x9a, selected ? .44 : .18);
      nodeMesh(
          n, count,
          [&](unsigned a, unsigned b) {
            if (a < count && b < count)
              line(points[a], points[b], edgeColor);
          },
          [&](unsigned a, unsigned b, unsigned c) {
            if (a < count && b < count && c < count)
              polygon({points[a], points[b], points[c]},
                      calibratedGray(selected ? 0xbf : 0x8a,
                                     selected ? .055 : .030));
          },
          [&](unsigned a, unsigned b, unsigned c, unsigned d) {
            if (a < count && b < count && c < count && d < count)
              polygon({points[a], points[b], points[c], points[d]},
                      calibratedGray(selected ? 0xbf : 0x8a,
                                     selected ? .050 : .026));
          });
      for (unsigned ch = 0; ch < count; ++ch) {
        double side = selected ? 4. : 2.8;
        stroke(rect(points[ch].x - side * .5, points[ch].y - side * .5, side,
                    side),
               edgeColor);
      }
    }
#endif
    context->restoreGlobalState();
    const auto cur = project(state.cursorX, state.cursorY, state.cursorZ, r);
    ellipse(rect(cur.x - 6.5, cur.y - 6.5, 13., 13.), rgba(.08, 1., .26, .88));
    line({cur.x - 10., cur.y}, {cur.x + 10., cur.y},
         rgba(.027, .063, .031, .92));
    line({cur.x, cur.y - 10.}, {cur.x, cur.y + 10.},
         rgba(.027, .063, .031, .92));
    at("CUR", cur.x + 10., cur.y - 7., style.value);
    for (unsigned i = 0; i < state.nodeCount; ++i) {
      const auto n = state.nodes[i];
      const auto pt = nodePoint(i);
      const bool selected = i == selectedNode;
      const double w = std::clamp(weights[i], 0.f, 1.f),
                   side = selected ? 17. : 12.;
      CColor c;
      if (n.active && overlaps(i)) {
        const double gray = .30 + w * .22;
        c = rgba(gray * (1. - w) + .08 * w, gray * (1. - w) + w,
                 gray * (1. - w) + .26 * w);
      } else
        c = calibratedGray(unsigned((n.active ? .34 + w * .56 : .18) * 255.));
      ellipse(rect(pt.x - side * .5, pt.y - side * .5, side, side), c);
      ellipse(rect(pt.x - side * .5, pt.y - side * .5, side, side),
              calibratedGray(selected ? 0xf0 : 0x0c, .95), false);
      at(format("%u", i + 1), pt.x + side * .62, pt.y - 7., style.value);
    }
  }
  bool overlaps(unsigned i) const {
    auto n = state.nodes[i];
    const float dx = n.x - state.cursorX, dy = n.y - state.cursorY,
                dz = n.z - state.cursorZ;
    return std::sqrt(dx * dx + dy * dy + dz * dz) <= nodeRadius(i);
  }
  void readout(const char *label, const std::string &val,
               const layout::Panel &panel, unsigned row) {
    const double y = layout::rowY(panel, row);
    at(label, layout::processorLabelX(panel.frame.x), y - 2., style.label);
    at(upper(val), layout::processorControlX(panel.frame.x), y - 2.,
       style.value);
  }
  void toggle(const char *label, clap_id id, const layout::Panel &panel,
              unsigned row) {
    const double y = layout::rowY(panel, row),
                 x = layout::processorControlX(panel.frame.x);
    at(label, layout::processorLabelX(panel.frame.x), y - 2., style.label);
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    const bool on = value(id) >= .5;
    const auto b = box(x, y - 1., 64., 15.);
    fill(b, calibratedGray(on ? 0x30 : 0x15));
    text(on ? "ON" : "OFF", b, style.value, kCenterText);
    fill(rect(x + 1., y, 2., 13.), style.fill);
    hit(b, [this, id] { set(id, value(id) < .5 ? 1. : 0.); });
#else
    button(
        rect(x, y - 1., 64., 17.), value(id) >= .5 ? "ON" : "OFF",
        [this, id] { set(id, value(id) < .5 ? 1. : 0.); }, value(id) >= .5);
#endif
  }
  void layoutMenu(const char *label, clap_id id, clap_id channelId,
                  const layout::Panel &panel, unsigned row) {
    std::vector<std::string> items;
    for (unsigned i = 0; i < s3g::kNodeTrackRegularLayoutCount; ++i)
      items.emplace_back(upper(
          s3g::nodeTrackLayoutName(s3g::nodeTrackRegularLayoutFromIndex(i))));
    const double y = layout::rowY(panel, row),
                 x = layout::processorControlX(panel.frame.x),
                 w = layout::processorMenuWidth(panel.frame.width);
    at(label, layout::processorLabelX(panel.frame.x), y - 2., style.label);
    const auto b = rect(x, y - 1., w, 17.);
    foundation::drawMenuBox(*context, b, upper(display(id)), font, style);
    hit(box(layout::sliderHitRect(panel, row)), [this, id, channelId, items, b,
                                                 y, x, w] {
      openPopup(
          b, items, int(value(id)),
          [this, id, channelId](int v) {
            batch({{id, double(v)},
                   {channelId, double(s3g::nodeTrackDefaultChannelsForLayout(
                                   s3g::nodeTrackRegularLayoutFromIndex(v)))}});
          },
          1, rect(x, y - 7., w, 21. * items.size()), 21.);
    });
  }
  void meter(const std::string &label, float value, Box r, bool peak) {
    const double db = 20. * std::log10(std::max(1.e-6f, value)),
                 norm = peak ? std::clamp((db + 60.) / 60., 0., 1.)
                             : std::clamp(double(value), 0., 1.),
                 x = r.origin.x + 34., w = r.size.width - (peak ? 70. : 78.);
    at(label, r.origin.x, r.origin.y - 1., style.value);
    fill(rect(x, r.origin.y + (peak ? 1. : 2.), w, peak ? 10. : 8.),
         style.strip);
    fill(rect(x + 1., r.origin.y + (peak ? 2. : 3.),
              std::max(0., (w - 2.) * norm), peak ? 8. : 6.),
         peak ? style.fill : style.text);
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    // NSFrameRect in Cocoa uses the last fill color, not the stroke color.
    framed(box(x, r.origin.y + (peak ? 1. : 2.), w, peak ? 10. : 8.),
           peak ? style.fill : style.text);
#endif
    at(peak ? format("%+3.0f", db) : format("%3.0f%%", norm * 100.),
       maxX(r) - (peak ? 30. : 38.), r.origin.y - 1., style.value);
  }
  void paint() override {
    refreshState();
    titleBand(layout::mixerTitleBand(family.canvas));
    panel(box(family.fieldPanel), "NODE FIELD");
    const Box buttons[] = {box(372, 47, 44, 18), box(418, 47, 44, 18),
                           box(464, 47, 52, 18), box(522, 47, 20, 18),
                           box(546, 47, 20, 18)};
    const char *titles[] = {"TOP", "SIDE", "3/4", "-", "+"};
    for (int i = 0; i < 5; ++i) {
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
      const bool selected = viewMode == i && i < 3;
      fill(buttons[i], selected ? style.text : calibratedGray(0x1d));
      text(titles[i], buttons[i], selected ? calibratedGray(0x08) : style.value, kCenterText);
      hit(buttons[i], [this, i] { setView(i); });
#else
      header(
          buttons[i], titles[i], [this, i] { setView(i); },
          viewMode == i && i < 3);
#endif
    }
    drawField();
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    at(format("%u NODES / 3OA / 8 x 16CH / 128CH", state.nodeCount), 34., 610., style.value);
#else
    at(format("%u NODES / %u OUT / 128CH", state.nodeCount, state.outputChannels), 34., 610., style.value);
#endif
    panel(box(family.output.frame), "OUTPUT");
    sliderRow("OUT", kParamOutputGain, family.output, 0,
              format("%+.1f dB", value(kParamOutputGain)));
    const auto &cp = kAmbi ? family.ambiBusCursor : family.busCursor;
    const auto &np = kAmbi ? family.ambiSelectedNode : family.selectedNode;
    panel(box(cp.frame), "BUS / CURSOR");
    panel(box(np.frame), format("NODE %02u", selectedNode + 1));
 #if !defined(S3G_AMBI_NODE_TRACK_MIXER)
    layoutMenu("BED", kParamLayoutOrOrder, kParamOutputChannels, cp, 0);
    readout("CHANNELS", format("%u", state.outputChannels), cp, 1);
 #endif
    const clap_id ids[] = {kParamNodeCount,   kParamCursorInfluence,
                           kParamCursorX,     kParamCursorY,
                           kParamCursorZ,     kParamCursorRadius,
                           kParamCursorFocus, kParamCursorGate};
    const char *labels[] = {"NODES",    "INFLUENCE", "CURSOR X", "CURSOR Y",
                            "CURSOR Z", "RADIUS",    "FOCUS",    "GATE"};
    for (unsigned i = 0; i < (kAmbi ? 7u : 8u); ++i) {
      if (i == 4 && state.lockZ)
        readout(labels[i], "LOCKED", cp, i + (kAmbi ? 0 : 2));
      else
        sliderRow(
            labels[i], ids[i], cp, i + (kAmbi ? 0 : 2),
            i == 0 ? format("%u", state.nodeCount)
                   : i == 1 ? format("%.0f%%", value(ids[i]) * 100.)
                            : format(i < 5 ? "%+.2f" : "%.2f", value(ids[i])));
    }
    toggle("LOCK Z", kParamLockZ, cp, kAmbi ? 7 : 10);
    const clap_id base = kParamNodeBase + selectedNode * kParamNodeStride;
    toggle("ACTIVE", base, np, 0);
 #if !defined(S3G_AMBI_NODE_TRACK_MIXER)
    layoutMenu("SOURCE", base + 2, base + 3, np, 1);
    const auto n = state.nodes[selectedNode];
    readout("BUS",
            n.inputStart > 128
                ? "OVER"
                : format("%03u-%03u", n.inputStart,
                         std::min(128u, n.inputStart + n.sourceChannels - 1)),
            np, 2);
#else
    readout("BUS", format("%03u-%03u", selectedNode * 16 + 1, (selectedNode + 1) * 16), np, 1);
#endif
    const clap_id coordinate = base + (kAmbi ? 2 : 5);
    const clap_id nodeIds[] = {base + 1,
                               coordinate + 0,
                               coordinate + 1,
                               coordinate + 2,
                               coordinate + 3,
                               coordinate + 4,
                               kParamNodeRotateAzBase + selectedNode,
                               kParamNodeRotateElBase + selectedNode};
    const char *nodeLabels[] = {"LEVEL", "X",     "Y",      "Z",
                                kAmbi ? "RADIUS" : "SCALE", "FOCUS", "ROT AZ", "ROT EL"};
    for (unsigned i = 0; i < (kAmbi ? 6u : 8u); ++i) {
      if (i == 3 && state.lockZ)
        readout("Z", "LOCKED", np, i + (kAmbi ? 2 : 3));
      else
        sliderRow(nodeLabels[i], nodeIds[i], np, i + (kAmbi ? 2 : 3),
                  format(i == 0 ? "%+.1f dB"
                                : i >= 6 ? "%+.0f°" : i >= 4 ? "%.2f" : "%+.2f",
                         value(nodeIds[i])));
    }
    at("NODE GAIN / POST CURSOR", 34., 624., style.label);
    at("PEAK METER / POST CURSOR", 34., 754., style.label);
    meter("OUT", p.outputPeak.load(), box(34., 777., 520., 13.), true);
    for (unsigned i = 0; i < kParamNodeLimit; ++i) {
      const double meterWidth = kAmbi ? 252. : 120.;
      const double x = 34. + (i / 4) * (kAmbi ? 268. : 128.), y = (i % 4) * 20.;
      meter(format("N%02u", i + 1), i < state.nodeCount ? weights[i] : 0.f,
            box(x, 647. + y, meterWidth, 13.), false);
      meter(format("N%02u", i + 1),
            i < state.nodeCount ? p.nodePeaks[i].load() : 0.f,
            box(x, 800. + y, meterWidth, 13.), true);
    }
  }
  void updateSpatial(CPoint pt) {
    refreshState();
    const bool cursor = spatialDrag == 2;
    const auto r = box(family.fieldPlot);
    const double scale = viewScaleForRect(r),
                 cx = r.origin.x + r.size.width * .5,
                 cy = r.origin.y + r.size.height * .52;
    const auto n = state.nodes[draggedNode];
    double x = cursor ? state.cursorX : n.x, y = cursor ? state.cursorY : n.y,
           z = cursor ? state.cursorZ : n.z;
    if (viewMode == 0) {
      x = std::clamp((cy - pt.y) / scale, -2., 2.);
      y = std::clamp((cx - pt.x) / scale, -2., 2.);
    } else if (viewMode == 1) {
      y = std::clamp((cx - pt.x) / scale, -2., 2.);
      z = std::clamp((cy - pt.y) / scale, -2., 2.);
    } else {
      const double yaw = viewYaw * (s3g::kPi / 180.),
                   pitch = viewPitch * (s3g::kPi / 180.),
                   yr = (cx - pt.x) / scale,
                   denom = std::max(.15, std::abs(std::sin(pitch))),
                   xr = (z * std::cos(pitch) - (cy - pt.y) / scale) / denom;
      x = std::clamp(xr * std::cos(yaw) + yr * std::sin(yaw), -2., 2.);
      y = std::clamp(-xr * std::sin(yaw) + yr * std::cos(yaw), -2., 2.);
    }
    if (state.lockZ)
      z = 0.;
    const clap_id base =
        cursor ? kParamCursorX
               : kParamNodeBase + draggedNode * kParamNodeStride + (kAmbi ? 2 : 5);
    change(base, x);
    change(base + 1, y);
    change(base + 2, z);
  }
  bool pointerDown(MouseDownEvent &e) override {
    if (!e.buttonState.isLeft() ||
        !foundation::contains(box(family.fieldPlot), e.mousePosition))
      return false;
    refreshState();
    lastPoint = e.mousePosition;
    if (e.modifiers.has(ModifierKey::Shift)) {
      spatialDrag = 3;
      return true;
    }
    const auto cur = project(state.cursorX, state.cursorY, state.cursorZ,
                             box(family.fieldPlot));
    if (std::hypot(cur.x - lastPoint.x, cur.y - lastPoint.y) <= 15.)
      spatialDrag = 2;
    else
      for (unsigned i = 0; i < state.nodeCount; ++i) {
        auto pt = nodePoint(i);
        if (std::hypot(pt.x - lastPoint.x, pt.y - lastPoint.y) <= 14.) {
          selectedNode = draggedNode = i;
          spatialDrag = 1;
          break;
        }
      }
    if (spatialDrag == 1 || spatialDrag == 2) {
      const clap_id base =
          spatialDrag == 2
              ? kParamCursorX
              : kParamNodeBase + draggedNode * kParamNodeStride + (kAmbi ? 2 : 5);
      if (p.guiParamEvents.available() < 9) {
        spatialDrag = 0;
        return true;
      }
      for (unsigned i = 0; i < 3; ++i)
        begin(base + i);
      if (spatialDrag == 2)
        updateSpatial(lastPoint);
    }
    return true;
  }
  void pointerMove(MouseMoveEvent &e) override {
    if (spatialDrag == 1 || spatialDrag == 2)
      updateSpatial(e.mousePosition);
    else if (spatialDrag == 3) {
      viewMode = 2;
      viewYaw += (e.mousePosition.x - lastPoint.x) * .35;
      viewPitch = std::clamp(
          viewPitch + (e.mousePosition.y - lastPoint.y) * .35, -75., 75.);
      lastPoint = e.mousePosition;
    }
    if (spatialDrag)
      invalid();
  }
  void pointerUp(MouseUpEvent &) override {
    end();
    spatialDrag = 0;
  }
  Params state{};
  std::array<float, 16> weights{};
  unsigned selectedNode = 0, draggedNode = 0;
  int viewMode = 2, spatialDrag = 0;
  double viewYaw = -35., viewPitch = 30., viewZoom = 1.;
  CPoint lastPoint;
};
} // namespace node_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new node_canvas::Editor(p);
}
