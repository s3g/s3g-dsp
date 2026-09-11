// Geometry, path weights, cell hit tests and randomization are translated
// directly from S3GGroupMatrix[32]View. The live DSP is never used for drawing.
namespace group_canvas {
using namespace s3g::portable_gui::routing;
namespace foundation = s3g::portable_gui::foundation;
namespace layout = s3g::gui_layout;
constexpr const auto &family = layout::kMatrixFamilyLayout;
#if S3G_AMBI_GROUP_MATRIX_CHANNELS == 64
constexpr const char *pluginName = "Ambi Matrix Group 64";
constexpr const char *pluginTitle = "s3g AMBI MATRIX GROUP 64CH";
constexpr uint32_t initialRandomSeed = 0xa64d5137u;
#elif S3G_AMBI_GROUP_MATRIX_CHANNELS == 128
constexpr const char *pluginName = "Ambi Matrix Group 128";
constexpr const char *pluginTitle = "s3g AMBI MATRIX GROUP 128CH";
constexpr uint32_t initialRandomSeed = 0x1285137u;
#elif S3G_GROUP_MATRIX_CHANNELS == 32
constexpr const char *pluginName = "Matrix Group 32";
constexpr const char *pluginTitle = "s3g MATRIX GROUP 32CH";
constexpr const char *groupNames[] = {"2CH", "4CH", "8CH"};
constexpr uint32_t initialRandomSeed = 0x325eed32u;
#else
constexpr const char *pluginName = "Matrix Group 64";
constexpr const char *pluginTitle = "s3g MATRIX GROUP 64CH";
constexpr const char *groupNames[] = {"4CH", "8CH", "16CH"};
constexpr uint32_t initialRandomSeed = 0x5137c0deu;
#endif
#if defined(S3G_AMBI_GROUP_MATRIX_CHANNELS)
constexpr const auto &patternPanel = family.ambiPattern;
constexpr unsigned patternOffset = 0;
#else
constexpr const auto &patternPanel = family.groupPattern;
constexpr unsigned patternOffset = 1;
#endif
constexpr clap_id preservedOutput = kParamOutput;
constexpr const auto &stateExtension = stateExt;
#include "s3g_routing_parameter_editor.inc"
class Editor final : public ParameterEditor {
public:
  explicit Editor(Plugin &p) : ParameterEditor(p) {
    model = std::make_unique<Plugin>();
    model->plugin.plugin_data = model.get();
    init(&model->plugin);
    refreshModel();
  }
  double refreshPeriodMilliseconds() const override { return 1000. / 24.; }
  void refreshModel() {
    for (auto i : infos)
      applyParam(*model, i.id, value(i.id));
  }
#if defined(S3G_AMBI_GROUP_MATRIX_CHANNELS)
  static unsigned groups() { return S3G_AMBI_GROUP_MATRIX_CHANNELS / 16; }
  static unsigned index(unsigned src, unsigned dst) { return src * groups() + dst; }
#else
  unsigned groups() const { return model->matrix.activeGroups(); }
  static unsigned index(unsigned src, unsigned dst) { return src * 16 + dst; }
#endif
  Box cellBox(unsigned src, unsigned dst) const {
    const double cell = family.matrixGrid.width / groups(),
                 gap = groups() >= 16 ? 2. : groups() >= 8 ? 4. : 8.;
    return box(family.matrixGrid.x + dst * cell,
               family.matrixGrid.y + src * cell, cell - gap, cell - gap);
  }
  Box deviationTrack() const {
    return box(family.matrixGrid.x + 214.,
               family.matrixPanel.y + family.matrixPanel.height - 28., 92.,
               14.);
  }
  float randomUnit() {
    randomState = randomState * 1664525u + 1013904223u;
    return float((randomState >> 8) & 0xffffffu) / 16777215.f;
  }
  void randomizeMatrix() {
    refreshModel();
    std::vector<std::pair<clap_id, double>> v;
    v.reserve(kCrosspointCount);
    std::array<double, kCrosspointCount> values;
    values.fill(-80.);
    for (unsigned src = 0; src < groups(); ++src)
      for (unsigned dst = 0; dst < groups(); ++dst) {
        const bool diagonal = src == dst;
        const float chance = diagonal ? .95f - .35f * float(randomDev)
                                      : .10f + .65f * float(randomDev);
        if (randomUnit() > chance)
          continue;
        const float r = randomUnit();
        values[index(src, dst)] =
            diagonal ? -12. + r * 12.
                     : -42. + r * (36. + 6. * float(randomDev));
      }
    for (unsigned i = 0; i < kCrosspointCount; ++i)
      v.emplace_back(kCrosspointBase + i, values[i]);
    if (batch(v))
      presetName = "RAND";
  }
  void drawFlowPreview(Box r) {
    fill(r, calibratedGray(0x10));
    const double left = r.origin.x + 42., right = maxX(r) - 42.,
#if S3G_AMBI_GROUP_MATRIX_CHANNELS == 64
                 margin = 44.,
#else
                 margin = 30.,
#endif
                 top = r.origin.y + margin,
                 gap =
                     groups() > 1 ? (r.size.height - margin * 2.) / (groups() - 1) : 0.,
                 center = r.origin.x + r.size.width * .5;
    const auto &prm = model->params;
    const auto generated = model->matrix.generatedFlowPreview(
        p.routingPhase.load(std::memory_order_relaxed));
    for (unsigned src = 0; src < groups(); ++src)
      for (unsigned dst = 0; dst < groups(); ++dst) {
        const unsigned idx = index(src, dst);
        if (prm.crosspointDb[idx] <= -79.9f)
          continue;
        const float manual = std::clamp((prm.crosspointDb[idx] + 80.f) / 92.f,
                                        0.f, 1.f),
                    weight = generated[idx],
                    current =
                        manual * ((1.f - prm.motion) + weight * prm.motion),
                    drawWeight = std::max(manual * .30f, current);
        if (drawWeight < .012f)
          continue;
        CPoint a{left, top + src * gap}, b{right, top + dst * gap};
        const double arc = (double(dst) - src) * 18. + prm.vortex * 58.;
        auto path = owned(context->createGraphicsPath());
        if (!path)
          continue;
        path->beginSubpath(a);
        path->addBezierCurve({center - 48., (a.y + b.y) * .5 + arc},
                             {center + 48., (a.y + b.y) * .5 + arc}, b);
        const double mix = std::clamp(prm.motion * weight, 0.f, 1.f),
                     gray = .42 + .42 * manual;
        context->setFrameColor(rgba(gray * (1. - mix) + .10 * mix,
                                    gray * (1. - mix) + .92 * mix,
                                    gray * (1. - mix) + .30 * mix,
                                    std::min(.86, .08 + drawWeight * .74)));
        context->setLineWidth(.45 + 3.2 * drawWeight);
        context->drawGraphicsPath(path, CDrawContext::kPathStroked);
      }
    for (unsigned i = 0; i < groups(); ++i) {
      const double side = groups() > 8 ? 12. : 20.;
      for (bool dest : {false, true}) {
        const double x = dest ? right : left, y = top + i * gap;
        fill(rect(x - side * .5, y - side * .5, side, side), style.strip);
        if (groups() <= 8)
          at(format("%c%u", dest ? 'D' : 'S', i + 1), x - 7., y - 6.,
             style.value);
      }
    }
    at(prm.motion <= .001f
           ? "MOTN 0: static matrix"
           : format("MOTN %.0f%% inside ceiling", prm.motion * 100.),
       r.origin.x + 12., maxY(r) - 24., style.value);
  }
  void drawGlossary() {
    auto r = box(family.glossary);
    r.size.height = showGlossary ? 132. : 21.;
#if defined(S3G_AMBI_GROUP_MATRIX_CHANNELS)
    panel(r, "");
    at(showGlossary ? "-" : "+", r.origin.x + 8., r.origin.y + 3., style.label);
    at("PATTERN TERMS", r.origin.x + 24., r.origin.y + 3., style.label);
#else
    panel(r, "PATTERN TERMS");
    at(showGlossary ? "−" : "+", maxX(r) - 20., r.origin.y + 3., style.label);
#endif
    hit(box(r.origin.x, r.origin.y, r.size.width, 21.),
        [this] { showGlossary = !showGlossary; });
    if (!showGlossary)
      return;
    const char *columns[3][4] = {
        {"CEIL   manual cell is the max/permission for a route",
         "-INF   closed cell: motion cannot pass",
         "MOTN   movement depth; 0 = normal matrix",
         "PHAS   offsets the visible/audio motion cycle"},
        {"DPTH   generated pattern depth",
         "SPRD   widens allowed route distribution",
         "VORT   rotates the generated motion shape",
         "SMTH   gain smoothing for route changes"},
        {"MODE   FREE runs internally; SYNC follows transport",
         "RATE   free-running cycle speed",
         "DIV    synced cycle length in beats",
         "SHAPE  generated gain pattern"}};
    for (unsigned c = 0; c < 3; ++c)
      for (unsigned row = 0; row < 4; ++row)
        text(columns[c][row],
             rect(r.origin.x + 16. + c * 328., r.origin.y + 36. + row * 24.,
                  320., 15.),
             style.value, kLeftText, font);
  }
  void paint() override {
    refreshModel();
    titleBand(layout::matrixTitleBand(family.canvas),
              [this] { randomizeMatrix(); });
    panel(box(family.matrixPanel), "GROUP MATRIX");
    panel(box(family.previewPanel), "PATTERN PREVIEW");
    panel(box(family.output.frame), "OUTPUT");
    panel(box(patternPanel.frame), "PATTERN");
    const auto &prm = model->params;
    const double cell = family.matrixGrid.width / groups();
    const auto generated = model->matrix.generatedFlowPreview(
        p.routingPhase.load(std::memory_order_relaxed));
    for (unsigned i = 0; i < groups(); ++i)
      if (groups() <= 8 || i % 2 == 0) {
        at(format("%u", i + 1), family.matrixGrid.x + i * cell + cell * .38,
           family.matrixGrid.y - 18., style.value);
        at(format("%u", i + 1), family.matrixGrid.x - 24.,
           family.matrixGrid.y + i * cell + cell * .38, style.value);
      }
    for (unsigned src = 0; src < groups(); ++src)
      for (unsigned dst = 0; dst < groups(); ++dst) {
        const unsigned idx = index(src, dst);
        const auto r = cellBox(src, dst);
        const double db = prm.crosspointDb[idx],
                     manual = std::clamp((db + 80.) / 92., 0., 1.);
        fill(r, style.strip);
        if (manual > .01) {
#if defined(S3G_AMBI_GROUP_MATRIX_CHANNELS)
          const double pad = groups() == 4 ? 8. : 5.;
#else
          const double pad = groups() > 8 ? 3. : 7.;
#endif
          auto f = inset(r, pad, pad);
          f.size.height *= manual;
          f.origin.y = maxY(r) - pad - f.size.height;
          fill(f, calibratedGray(unsigned((.18 + .22 * manual) * 255.), .96));
          const double live = manual * ((1.f - prm.motion) +
                                        generated[idx] * prm.motion),
                       mix = std::clamp(prm.motion * generated[idx], 0.f, 1.f);
          f = inset(r, pad, pad);
          f.size.height *= live;
          f.origin.y = maxY(r) - pad - f.size.height;
          fill(f, rgba(.08, 1., .26, .16 + .82 * mix * live));
        }
        if (groups() <= 8)
          at(db <= -79.9 ? "-inf" : format("%+.0f", db), r.origin.x +
#if defined(S3G_AMBI_GROUP_MATRIX_CHANNELS)
             (groups() == 4 ? r.size.width * .26 : 5.),
#else
             11.,
#endif
             r.origin.y + r.size.height * .42, style.label);
        const clap_id id = kCrosspointBase + idx;
        drag(
            r,
            [this, id, src, cell](CPoint pt) {
              change(id, -80. + std::clamp(1. - (pt.y - family.matrixGrid.y -
                                                 src * cell) /
                                                    cell,
                                           0., 1.) *
                                    92.);
            },
            [this, id] { begin(id); }, [this] { end(); },
            [this, id] { set(id, info(id).default_value); });
      }
    const auto tr = deviationTrack();
    slider(
        "DEVIATION", format("%.0f%%", randomDev * 100.), randomDev,
        box(tr.origin.x, tr.origin.y + 1., tr.size.width, 9.),
        inset(tr, -40., -8.),
        box(tr.origin.x - 70., tr.origin.y - 2., 66., 15.),
        box(family.matrixGrid.x + 318., tr.origin.y - 2., 36., 15.),
        [this](double v) { randomDev = v; }, {}, {},
        [this] { randomDev = .50; });
    const auto pr = box(family.previewPanel);
    drawFlowPreview(inset(box(pr.origin.x, pr.origin.y + 20., pr.size.width,
                              pr.size.height - 20.),
                          20., 16.));
    sliderRow("OUT", kParamOutput, family.output, 0);
#if !defined(S3G_AMBI_GROUP_MATRIX_CHANNELS)
    menuRow("GROUP", kParamGroupSize, family.groupPattern, 0,
            {groupNames[0], groupNames[1], groupNames[2]});
#endif
    menuRow("SHAPE", kParamShape, patternPanel, patternOffset,
            {"FLOW", "PULSE", "CHASE", "SWIRL", "SCAT", "HOLD"});
    menuRow("MODE", kParamMode, patternPanel, patternOffset + 1, {"FREE", "SYNC"});
    const char *labels[] = {"DEPTH", "SPREAD",   "VORTEX", "MOTION",
                            "RATE",  "DIVISION", "PHASE",  "SMOOTHING"};
    const clap_id ids[] = {kParamFlow,   kParamSpread,   kParamVortex,
                           kParamMotion, kParamRate,     kParamDivision,
                           kParamPhase,  kParamSmoothing};
    for (unsigned i = 0; i < 8; ++i)
      sliderRow(labels[i], ids[i], patternPanel, i + 2 + patternOffset,
                i == 5 ? format("%.2g", value(ids[i])) : "");
    drawGlossary();
  }
  std::unique_ptr<Plugin> model;
  bool showGlossary = true;
  double randomDev = .5;
  uint32_t randomState = initialRandomSeed;
};
} // namespace group_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new group_canvas::Editor(p);
}
