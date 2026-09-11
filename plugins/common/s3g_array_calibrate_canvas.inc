namespace calibrate_canvas {
using namespace s3g::portable_gui::routing;
namespace foundation = s3g::portable_gui::foundation;
namespace layout = s3g::gui_layout;
constexpr const auto &family = kArrayLayout;
constexpr const char *pluginName = "Array Calibrate";
const std::string pluginTitle =
    format("s3g ARRAY CALIBRATE %uCH", kChannelCount);
constexpr clap_id preservedOutput = kOutputParamId;
constexpr const auto &stateExtension = stateExt;
#include "s3g_routing_parameter_editor.inc"
class Editor final : public ParameterEditor {
public:
  explicit Editor(Plugin &p) : ParameterEditor(p) {}
  unsigned activeChannels() const {
    return unsigned(
        std::clamp(value(kActiveParamId), 1., double(kChannelCount)));
  }
  unsigned pageCount() const {
    return std::max(1u, (activeChannels() + kRowsPerPage - 1) / kRowsPerPage);
  }
  void clampPage() { page = std::min(page, pageCount() - 1); }
  clap_id stageId() const {
    return tab == 0 ? kHpfBypassParamId
                    : tab == 1 ? kDelayBypassParamId : kTrimBypassParamId;
  }
  void headerButton(Box b, const std::string &label, std::function<void()> fn,
                    bool on = false, bool enabled = true) {
    fill(b, calibratedGray(!enabled ? 0x24 : on ? 0xb8 : 0x3b));
    text(label, b, calibratedGray(!enabled ? 0x68 : on ? 0x15 : 0xc0),
         kCenterText);
    hit(inset(b, 0., -3.), std::move(fn), enabled);
  }
  void drawHeader() {
    const auto r = box(family.editor.frame);
    const double y = r.origin.y + 3.;
    const double offsets[] = {112., 170., 236.}, widths[] = {52., 60., 52.};
    const char *labels[] = {"HPF", "DELAY", "TRIM"};
    for (unsigned i = 0; i < 3; ++i)
      headerButton(
          box(r.origin.x + offsets[i], y, widths[i], 17.), labels[i],
          [this, i] {
            finishNumeric();
            end();
            tab = i;
            page = 0;
          },
          tab == i);
    const bool on = value(stageId()) < .5;
    headerButton(
        box(r.origin.x + 330., y, 82., 17.), on ? "STAGE ON" : "STAGE OFF",
        [this] { set(stageId(), value(stageId()) < .5 ? 1. : 0.); }, on,
        value(kBypassParamId) < .5);
    if (tab != 0 && pageCount() > 1) {
      headerButton(
          box(maxX(r) - 94., y, 26., 17.), "<",
          [this] {
            finishNumeric();
            --page;
          },
          false, page > 0);
      headerButton(
          box(maxX(r) - 32., y, 26., 17.), ">",
          [this] {
            finishNumeric();
            ++page;
          },
          false, page + 1 < pageCount());
      at(format("%u/%u", page + 1, pageCount()), maxX(r) - 61., y + 1.,
         style.value);
    }
  }
  static double freqNorm(double hz) {
    return std::clamp((std::log(hz) - std::log(20.)) /
                          (std::log(20000.) - std::log(20.)),
                      0., 1.);
  }
  static double normFreq(double n) {
    return std::exp(std::log(20.) +
                    std::clamp(n, 0., 1.) * (std::log(20000.) - std::log(20.)));
  }
  static double magnitudeDb(double hz, double cutoff, unsigned poles) {
    const double ratio = std::max(.000001, hz / std::max(1., cutoff)),
                 onePole = ratio / std::sqrt(1. + ratio * ratio);
    return 20. * std::log10(
                     std::max(.000001, std::pow(onePole, std::max(1u, poles))));
  }
  static double dbNorm(double db) {
    return 1. - std::clamp((db + 48.) / 54., 0., 1.);
  }
  void drawFilterGraph() {
    const auto r = box(34., 204., 652., kFilterGraphHeight),
               g = box(r.origin.x + 12., r.origin.y + 30., r.size.width - 24.,
                       r.size.height - 42.);
    panel(r, "RESPONSE");
    fill(g, calibratedGray(0x18));
    for (int i = 1; i < 4; ++i) {
      const double y = g.origin.y + g.size.height * i / 4.;
      line({g.origin.x, y}, {maxX(g), y}, calibratedGray(0x2e, .75));
    }
    const bool bypass =
        value(kHpfBypassParamId) >= .5 || value(kBypassParamId) >= .5;
    std::vector<CPoint> curve;
    for (int i = 0; i <= 160; ++i) {
      const double n = i / 160.,
                   db = bypass ? 0.
                               : magnitudeDb(normFreq(n), value(kCutoffParamId),
                                             unsigned(value(kPolesParamId)));
      curve.push_back({g.origin.x + g.size.width * n,
                       g.origin.y + g.size.height * dbNorm(db)});
    }
    polygon(curve, calibratedGray(bypass ? 0x77 : 0xc8), false, 2.);
    const double x =
        g.origin.x + g.size.width * freqNorm(value(kCutoffParamId));
    line({x, g.origin.y}, {x, maxY(g)}, calibratedGray(0xad, .75));
    at(format("%.0f Hz", value(kCutoffParamId)), x + 6., g.origin.y + 10.,
       style.value);
    at(format("%u dB/oct", unsigned(value(kPolesParamId)) * 6), g.origin.x + 6.,
       maxY(g) - 18., style.value);
  }
  void paint() override {
    clampPage();
    titleBand(layout::arrayTitleBand(family.canvas));
    panel(box(family.output.frame), "OUTPUT");
    panel(box(family.array.frame), "ARRAY");
    panel(box(family.editor.frame), "CALIBRATION");
    sliderRow("OUT", kOutputParamId, family.output, 0);
    sliderRow("ACTIVE", kActiveParamId, family.array, 0);
    {
      const double y = layout::rowY(family.output, 1),
                   x = layout::processorControlX(family.output.frame.x);
      at("BYPASS", layout::processorLabelX(family.output.frame.x), y - 2.,
         style.label);
      headerButton(
          box(x, y - 1., 74., 15.), value(kBypassParamId) >= .5 ? "ON" : "OFF",
          [this] { set(kBypassParamId, value(kBypassParamId) < .5 ? 1. : 0.); },
          value(kBypassParamId) >= .5);
    }
    drawHeader();
    if (tab == 0) {
      drawFilterGraph();
      auto cutoff = family.editor;
      cutoff.frame.x = kFilterCutoffX;
      cutoff.frame.width = kFilterCutoffWidth;
      sliderRow("CUTOFF", kCutoffParamId, cutoff, 0);
      const double y = layout::rowY(family.editor, 0),
                   x = layout::processorControlX(kFilterPolesX),
                   w = layout::processorMenuWidth(kFilterPolesWidth);
      at("POLES", layout::processorLabelX(kFilterPolesX), y - 2., style.label);
      const auto b = rect(x, y - 1., w, 15.);
      foundation::drawMenuBox(*context, b, display(kPolesParamId), font, style);
      hit(CRect(b).inset(0., -4.), [this, b] {
        openPopup(
            b, {"1P", "2P", "3P", "4P"}, int(value(kPolesParamId)) - 1,
            [this](int i) { set(kPolesParamId, i + 1.); }, 1,
            rect(b.left, b.bottom + 2., b.getWidth(), 72.), 18.);
      });
      return;
    }
    drawRows();
  }
  void updateRow(CPoint pt) {
    clampPage();
    const int row = int(std::floor((pt.y - family.channelPlot.y) / kRowHeight));
    if (row < 0 || row >= int(kRowsPerPage) ||
        page * kRowsPerPage + row >= activeChannels())
      return;
    const auto id = (tab == 1 ? kDelayParamBaseId : kGainParamBaseId) +
                    page * kRowsPerPage + unsigned(row);
    if (active != id) {
      end();
      begin(id);
    }
    const double n = std::clamp(
        (pt.x - family.channelPlot.x) / family.channelPlot.width, 0., 1.);
    change(id, tab == 1 ? n * kFixedMaxDelayMs : -60. + n * 78.);
  }
  void drawRows() {
    const auto plot = box(family.channelPlot);
    const bool delay = tab == 1;
    const double zero = plot.origin.x + plot.size.width * (60. / 78.);
    at(delay ? "0 ms" : "-60", plot.origin.x, 154., style.label);
    at(delay ? "4000 ms" : "+18 dB", maxX(plot) - (delay ? 52. : 42.), 154.,
       style.label);
    if (!delay) {
      at("0", zero - 4., 154., style.label);
      line({zero, plot.origin.y}, {zero, maxY(plot)}, calibratedGray(0x5b));
    }
    const unsigned n =
        std::min(kRowsPerPage, activeChannels() - page * kRowsPerPage);
    for (unsigned row = 0; row < n; ++row) {
      const unsigned ch = page * kRowsPerPage + row;
      const double y = plot.origin.y + row * kRowHeight;
      const clap_id id = (delay ? kDelayParamBaseId : kGainParamBaseId) + ch;
      const double val = value(id),
                   norm = delay ? std::clamp(val / kFixedMaxDelayMs, 0., 1.)
                                : std::clamp((val + 60.) / 78., 0., 1.),
                   x = plot.origin.x + plot.size.width * norm;
      at(format("%02u", ch + 1), 34., y - 1., style.label);
      fill(rect(plot.origin.x, y, plot.size.width, 14.), calibratedGray(0x20));
      const bool muted = !delay && value(kMuteParamBaseId + ch) >= .5,
                 inverted = !delay && value(kInvertParamBaseId + ch) >= .5;
      fill(delay ? rect(plot.origin.x, y, std::max(1., x - plot.origin.x), 14.)
                 : rect(std::min(x, zero), y, std::max(1., std::abs(x - zero)),
                        14.),
           calibratedGray(muted ? 0x4a : inverted ? 0x9c : 0xa0));
      drag(
          rect(plot.origin.x, y - 7., plot.size.width, 24.),
          [this](CPoint p) { updateRow(p); }, {}, [this] { end(); },
          [this, id] { set(id, info(id).default_value); });
      const auto nr = rect(family.channelValueColumn.x, y - 4.,
                           family.channelValueColumn.width, 22.);
      fill(nr, style.background);
      text(format(delay ? "%.3f" : "%.2f", val), nr, style.value, kRightText);
      number(
          nr, val, [this, id](double v) { change(id, v); }, true,
          [this, id] { begin(id); }, [this] { end(); });
      if (!delay) {
        headerButton(
            box(family.channelMuteColumn.x, y - 4.,
                family.channelMuteColumn.width, 20.),
            "M",
            [this, ch] {
              set(kMuteParamBaseId + ch,
                  value(kMuteParamBaseId + ch) < .5 ? 1. : 0.);
            },
            muted);
        headerButton(
            box(family.channelInvertColumn.x, y - 4.,
                family.channelInvertColumn.width, 20.),
            "INV",
            [this, ch] {
              set(kInvertParamBaseId + ch,
                  value(kInvertParamBaseId + ch) < .5 ? 1. : 0.);
            },
            inverted);
      }
    }
  }
  unsigned tab = 0, page = 0;
};
} // namespace calibrate_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new calibrate_canvas::Editor(p);
}
