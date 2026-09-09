// Geometry and drawing formulas follow S3GDrumMixerView and the Cocoa mixer
// primitives literally. The 81-parameter model and logarithmic FREQ mapping are
// shared with the original editor.
namespace fx_canvas {
using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;
constexpr const char *pluginName = "Drum Mixer 16";
constexpr const char *pluginTitle = "s3g DRUM MIXER 16";
constexpr clap_id preservedOutput = CLAP_INVALID_ID;
#include "../common/s3g_drum_effect_canvas.inc"

constexpr LaneParamOffset dialOffsets[]{kLanePanOffset, kLaneLowOffset,
                                        kLaneMidOffset, kLaneMidFrequencyOffset,
                                        kLaneHighOffset};
inline CRect lanePanel(uint32_t lane) {
  return rect(18. + lane * 134., 42., 128., 546.);
}
inline CRect dialRect(uint32_t lane, uint32_t index) {
  return rect(lanePanel(lane).left + 12. + (index % 2) * 56.,
              69. + (index / 2) * 62., 48., 58.);
}
inline CRect faderRect(uint32_t lane) {
  return rect(lanePanel(lane).left + 18., 354., 54., 168.);
}
inline CRect stripTrack(double x, double width, double y) {
  return rect(x + 44., y + 1., std::max(12., width - 105.), 9.);
}
inline CRect stripHit(double x, double width, double y) {
  return rect(x + 8., y - 8., width - 16., 24.);
}
inline double busY(clap_id id) {
  return 188. + 36. + (id - kBusDriveParamId + 1) * 26.;
}

class Editor final : public EffectEditor {
public:
  explicit Editor(Plugin &p) : EffectEditor(p) {}
  double fromNormalized(clap_id id, double norm) const override {
    return paramValueFromNormalized(id, norm);
  }
  void stripSlider(clap_id id, const char *label, double x, double width,
                   double y) {
    slider(id, label, stripHit(x, width, y), stripTrack(x, width, y),
           rect(x + 16., y - 2., 24., 15.),
           rect(x + width - 56., y - 2., 40., 15.),
           normalizedParamValue(id, value(id)));
  }
  void toggle(CRect b, const char *label, clap_id id, double nextValue,
              bool selected) {
    fill(b, color(selected ? 0x3f3f3f : 0x1b1b1b));
    fittedText(label, b, style.value, kCenterText);
    hit(b, [this, id, nextValue] { set(id, nextValue); });
  }
  void dial(clap_id id, const char *label, CRect b) {
    const double norm = normalizedParamValue(id, value(id));
    fittedText(label, rect(b.left, b.top, b.getWidth(), 13.), style.label,
               kCenterText);
    const double diameter = std::min(32., b.getWidth() - 8.);
    const auto face =
        rect(b.getCenter().x - diameter * .5, b.top + 14., diameter, diameter);
    ellipse(face, style.strip);
    ellipse(face, style.grid, false, 1.);
    const double angle = (135. + 270. * norm) * 3.14159265358979323846 / 180.;
    const auto center = face.getCenter();
    line({center.x + std::cos(angle) * diameter * .13,
          center.y + std::sin(angle) * diameter * .13},
         {center.x + std::cos(angle) * diameter * .40,
          center.y + std::sin(angle) * diameter * .40},
         style.text, 2.);
    fill(rect(center.x - 1.5, center.y - 1.5, 3., 3.), style.fill);
    fittedText(display(id), rect(b.left, face.bottom + 3., b.getWidth(), 13.),
               style.value, kCenterText);
    drag(
        b,
        [this, id](CPoint pt) {
          if (dialFirstPoint) {
            dialStart = pt;
            dialFirstPoint = false;
            return;
          }
          const double delta = dialStart.y - pt.y + pt.x - dialStart.x;
          setValue(id,
                   paramValueFromNormalized(id, dialStartNorm + delta / 160.));
        },
        [this, id] {
          dialStartNorm = normalizedParamValue(id, value(id));
          dialFirstPoint = true;
          begin(id);
        },
        [this] { end(); }, [this, id] { set(id, info(id).default_value); });
  }
  void fader(clap_id id, CRect b) {
    const double norm = normalizedParamValue(id, value(id));
    auto track = rect(b.getCenter().x - 3., b.top, 6., b.getHeight());
    fill(track, style.strip);
    auto inner = CRect(track).inset(1., 1.);
    inner.top += inner.getHeight() * (1. - norm);
    fill(inner, style.fill);
    fill(rect(b.left + 5., b.bottom - norm * b.getHeight() - 4.,
              b.getWidth() - 10., 9.),
         style.text);
    drag(
        b,
        [this, id, b](CPoint pt) {
          setValue(id, paramValueFromNormalized(id, 1. - (pt.y - b.top) /
                                                             b.getHeight()));
        },
        [this, id] { begin(id); }, [this] { end(); },
        [this, id] { set(id, info(id).default_value); });
  }
  void meter(float peak, CRect b) {
    const double db = 20. * std::log10(std::max(peak, .000001f));
    const double norm = std::clamp((db + 60.) / 60., 0., 1.);
    fill(b, style.strip);
    const auto inner = CRect(b).inset(1., 1.);
    const auto range = [this, inner, norm](double low, double high, CColor c) {
      const double top = std::min(norm, high);
      if (top > low)
        fill(rect(inner.left, inner.bottom - inner.getHeight() * top,
                  inner.getWidth(), inner.getHeight() * (top - low)),
             c);
    };
    // Cocoa calibrated colors converted to sRGB, like the shared grays.
    const auto green = color(0x5fa876), amber = color(0xd0aa4d),
               red = color(0xcb675f);
    range(0., .76, green);
    range(.76, .92, amber);
    range(.92, 1., red);
    // The original NSFrameRect uses the final active fill color, including
    // the visible amber/red outline above the filled portion of the meter.
    stroke(CRect(b).inset(.5, .5),
           norm > .92 ? red
                      : norm > .76 ? amber : norm > 0. ? green : style.strip);
  }
  void paint() override {
    const bool direct = value(kOutputModeParamId) >= .5;
    paintTitle(std::string(direct ? "DIRECT 8x2" : "SUM 16>2") + "  //  " +
               peakText(p.masterPeak.load(std::memory_order_relaxed)));
    const char *labels[]{"PAN", "LOW", "MID", "FREQ", "HIGH"};
    for (uint32_t lane = 0; lane < s3g::kDrumMixerLaneCount; ++lane) {
      const auto b = lanePanel(lane);
      panel(b, format("%02u  IN %u/%u", lane + 1, lane * 2 + 1, lane * 2 + 2));
      for (uint32_t d = 0; d < 5; ++d)
        dial(laneParamId(lane, dialOffsets[d]), labels[d], dialRect(lane, d));
      stripSlider(laneParamId(lane, kLaneAuxOffset), "AUX", b.left,
                  b.getWidth(), 266.);
      const auto mute = laneParamId(lane, kLaneMuteOffset),
                 solo = laneParamId(lane, kLaneSoloOffset);
      toggle(rect(b.left + 16., 294., 44., 24.), "MUTE", mute,
             value(mute) >= .5 ? 0. : 1., value(mute) >= .5);
      toggle(rect(b.left + 68., 294., 44., 24.), "SOLO", solo,
             value(solo) >= .5 ? 0. : 1., value(solo) >= .5);
      const auto level = laneParamId(lane, kLaneLevelOffset);
      text("LVL", rect(b.left + 16., 332., 50., 15.), style.label);
      fittedText(display(level),
                 rect(b.left + 16., 534., b.getWidth() - 32., 15.), style.value,
                 kRightText);
      fader(level, faderRect(lane));
      meter(p.lanePeaks[lane].load(std::memory_order_relaxed),
            rect(b.left + 98., 354., 10., 168.));
    }
    panel(rect(1096., 42., 206., 134.), "OUTPUT");
    stripSlider(kMasterLevelParamId, "OUT", 1096., 206., 78.);
    text("ROUTE", rect(1112., 104., 48., 24.), style.label);
    toggle(rect(1164., 104., 55., 24.), "SUM", kOutputModeParamId, 0., !direct);
    toggle(rect(1227., 104., 59., 24.), "DIRECT", kOutputModeParamId, 1.,
           direct);
    fittedText("SUM 1/2  //  DIRECT 1-16", rect(1112., 146., 174., 15.),
               style.value);
    panel(rect(1096., 188., 206., 248.), "DRUM BUS");
    const bool enabled = value(kBusEnabledParamId) >= .5;
    toggle(rect(1112., 216., 174., 22.), "AUX BUS", kBusEnabledParamId,
           enabled ? 0. : 1., enabled);
    const char *busLabels[]{"DRV", "GLU", "ROM", "WGT", "TON", "RET"};
    for (clap_id id = kBusDriveParamId; id <= kBusReturnParamId; ++id)
      stripSlider(id, busLabels[id - kBusDriveParamId], 1096., 206., busY(id));
    fittedText(format("BUS %.0f%%  //  GR %+.1f dB",
                      p.busActivity.load(std::memory_order_relaxed) * 100.,
                      double(p.busReductionDb.load(std::memory_order_relaxed))),
               rect(1112., 412., 174., 15.), style.value);
  }
  CPoint dialStart{};
  double dialStartNorm = 0.;
  bool dialFirstPoint = false;
};
} // namespace fx_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new fx_canvas::Editor(p);
}
