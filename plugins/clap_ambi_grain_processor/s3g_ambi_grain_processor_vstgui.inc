// Literal adaptation of S3GAmbiGrainProcessorView. Coordinates remain Cocoa's
// logical coordinates. The storage footer occupies the previously unused space
// below the waveform; title/font and proportional hosting use the family base.
namespace ambi_grain_canvas {
using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;
class Editor final : public View {
public:
  explicit Editor(Plugin &plugin) : View(kGuiW, kGuiH), p(plugin) {}
  ~Editor() override { stopRefresh(); }
  void service() override { servicePortableSampleLoad(p); }
  double value(clap_id id) {
    double v = 0;
    paramsGetValue(&p.plugin, id, &v);
    return v;
  }
  void event(s3g::clap_gui::ParamEventKind kind, clap_id id, double v = 0) {
    using Kind = s3g::clap_gui::ParamEventKind;
    if (kind == Kind::GestureBegin) {
      if (editing != CLAP_INVALID_ID || p.guiParamEvents.available() < 3)
        return;
    } else if (editing != id ||
               (kind == Kind::Value && p.guiParamEvents.available() <= 1))
      return;
    if (p.guiParamEvents.push({kind, id, v})) {
      if (kind == Kind::GestureBegin)
        editing = id;
      if (kind == Kind::GestureEnd)
        editing = CLAP_INVALID_ID;
      if (kind == s3g::clap_gui::ParamEventKind::Value)
        setParam(p, id, v);
      const auto *hostParams =
          p.host && p.host->get_extension
              ? static_cast<const clap_host_params_t *>(
                    p.host->get_extension(p.host, CLAP_EXT_PARAMS))
              : nullptr;
      s3g::clap_gui::requestParamEventService(p.host, hostParams);
    }
  }
  void change(clap_id id, double v) {
    event(s3g::clap_gui::ParamEventKind::GestureBegin, id);
    event(s3g::clap_gui::ParamEventKind::Value, id, v);
    event(s3g::clap_gui::ParamEventKind::GestureEnd, id);
  }
  std::string valueText(clap_id id) {
    char buffer[96]{};
    paramsValueToText(&p.plugin, id, value(id), buffer, sizeof(buffer));
    return upper(buffer);
  }
  CRect box(double x, double y, double w, double h) {
    return rect(x, y + kContentTranslation, w, h);
  }
  void row(const char *label, clap_id id, double y, double lo, double hi,
           bool enabled = true) {
    auto s = style;
    if (!enabled) {
      s.label = s.value = color(0x686868);
      s.fill = color(0x3f3f3f);
      s.grid = color(0x333333);
    }
    const auto track = box(kSliderTrackX, y + 1, kSliderTrackW, 9);
    const double n =
        id == kGrainMsParamId
            ? std::log(std::max(lo, value(id)) / lo) / std::log(hi / lo)
            : (value(id) - lo) / (hi - lo);
    foundation::drawHorizontalSlider(*context, track, n,
                                     track.getCenter().y - 6.5, 13, s);
    const auto values =
        box(s3g::gui_layout::processorValueX(kToolboxX, kToolboxW), y - 2,
            s3g::gui_layout::kStandardMetrics.processorValueWidth, 15);
    text(label,
         rect(kToolboxX + 16, track.getCenter().y - 7.5,
              kSliderTrackX - kToolboxX - 20, 15),
         s.label);
    text(foundation::sliderValueTextToFit(*context, valueText(id),
                                          values.getWidth(), font),
         rect(values.left, track.getCenter().y - 7.5, values.getWidth(), 15),
         s.value, kRightText);
    drag(
        box(kToolboxX, y - 5, kToolboxW, 23),
        [this, id, lo, hi](CPoint point) {
          const double unit =
              std::clamp((point.x - kSliderTrackX) / kSliderTrackW, 0., 1.);
          event(s3g::clap_gui::ParamEventKind::Value, id,
                id == kGrainMsParamId ? lo * std::pow(hi / lo, unit)
                                      : lo + (hi - lo) * unit);
        },
        [this, id] { event(s3g::clap_gui::ParamEventKind::GestureBegin, id); },
        [this, id] { event(s3g::clap_gui::ParamEventKind::GestureEnd, id); },
        [this, id] {
          for (const auto &def : kParamDefs)
            if (def.id == id)
              change(id, def.def);
        },
        enabled);
  }
  void categorical(const char *label, clap_id id, double y,
                   std::vector<std::string> items, int offset = 0) {
    const auto b = box(kSliderTrackX, y - 1, kMenuWidth, 15);
    text(label,
         rect(kToolboxX + 16, b.getCenter().y - 7.5,
              kSliderTrackX - kToolboxX - 20, 15),
         style.label);
    menu(b, valueText(id), std::move(items),
         static_cast<int>(value(id)) - offset,
         [this, id, offset](int choice) { change(id, choice + offset); });
  }
  void load(bool relink = false) {
    foundation::FileDialogOptions options;
    options.title =
        relink ? "Relink ambisonic sample" : "Load ACN/SN3D ambisonic file";
    const auto path = foundation::runFileDialog(getFrame(), options);
    if (!path.empty())
      queuePortableSampleLoad(p, path, relink);
  }
  bool dropped(const std::vector<std::string> &paths, CPoint) override {
    if (paths.empty())
      return false;
    queuePortableSampleLoad(p, paths[0]);
    return true;
  }
  void preset(bool save) {
    foundation::FileDialogOptions options;
    options.save = save;
    options.title = save ? "Save preset" : "Load preset";
    options.extension = "s3gpreset";
    options.extensionDescription = "s3g preset";
    options.initialDirectory =
        foundation::presetDirectory("Processor Ambi Grain");
    const auto path = foundation::runFileDialog(getFrame(), options);
    if (path.empty())
      return;
    const double gain = value(kGainParamId);
    const bool ok = save ? saveAmbiPresetFile(&p.plugin, stateExt, path)
                         : s3g::clap_gui::portable::loadStateFile(
                               &p.plugin, stateExt, path.c_str());
    if (!save)
      setParam(p, kGainParamId, gain);
    if (ok)
      presetName = foundation::pathFromUtf8(path.c_str()).stem().u8string();
    else
      p.sampleStatus = "PRESET FILE ERROR";
  }
  void paint() override {
    const auto band = s3g::gui_layout::encoderTitleBand({kGuiW, kGuiH});
    const auto b = [](s3g::gui_layout::Rect r) {
      return rect(r.x, r.y, r.width, r.height);
    };
    foundation::drawPluginTitle(*context, "s3g Processor Ambi Grain 16",
        rect(band.titleX, band.titleY - 2,
            band.presetLabelX - band.titleX - 8, 15), titleFont);
    text("PRESET", rect(band.presetLabelX, 12, 58, 15), style.label);
    menu(b(band.presetMenu), presetName, {"INIT"}, -1, [this](int) {
      for (const auto &def : kParamDefs)
        if (def.id != kGainParamId)
          change(def.id, def.def);
      presetName = "INIT";
    });
    button(b(band.loadButton), "LOAD", [this] { preset(false); });
    button(b(band.saveButton), "SAVE", [this] { preset(true); });
    text(format("PK %+.1f",
                20 * std::log10(std::max(.000001f, p.outputPeak.load()))),
         rect(kGuiW - 100, 12, 82, 15), style.value, kRightText);
    button(box(18, 48, 70, 24), "LOAD", [this] { load(); });
    const bool playing = p.playing.load();
    for (int kind = 0; kind < 3; ++kind) {
      const auto r = box(96 + 34 * kind, 49, 26, 22);
      const bool active = kind == 0 ? playing : kind == 1 ? !playing : false;
      fill(r, color(active ? 0xd1d1d1 : 0x141414));
      const auto c = color(active ? 0x0c0c0c : 0xd1d1d1);
      const auto center = r.getCenter();
      if (kind == 0)
        polygon({{center.x - 3, center.y - 5.5},
                 {center.x - 3, center.y + 5.5},
                 {center.x + 6, center.y}},
                c);
      else if (kind == 1) {
        fill(rect(center.x - 5.5, center.y - 5.5, 3.5, 11), c);
        fill(rect(center.x + 2, center.y - 5.5, 3.5, 11), c);
      } else
        fill(rect(center.x - 5.5, center.y - 5.5, 11, 11), c);
      hit(r, [this, kind] {
        p.playing.store(kind == 0 || (kind == 1 && !p.playing.load()));
        if (kind == 2)
          p.resetRequested.store(true);
      });
    }
    auto sample =
        std::atomic_load_explicit(&p.sample, std::memory_order_acquire);
    const auto info =
        !p.sampleStatus.empty()
            ? p.sampleStatus
            : sample ? format("%u CH / %.1f SEC / %.0f HZ / NORM %+.1f DB",
                              sample->channels,
                              double(sample->frames) / sample->sampleRate,
                              sample->sampleRate,
                              20 * std::log10(std::max(
                                       .000001f, sample->normalizationGain)))
                     : "NO AMBISONIC FILE LOADED";
    text(info, box(202, 51, 390, 17), style.label);
    panel(box(18, 92, 552, 458), "WAVEFORM");
    waveform(sample, box(28, 126, 532, 396));
    text("W CHANNEL SOURCE VIEW: CURSOR, JITTER BAND, AND GRAIN WINDOW",
         box(28, 556, 550, 16), style.label);
    menu(box(28, 584, 158, 20),
         std::string("STORE ") + storage::storageModeName(p.storageMode),
         {"PROJECT", "LINK", "EMBED"}, static_cast<int>(p.storageMode),
         [this](int index) {
           setStorageMode(p, static_cast<StorageMode>(index));
         });
    text(storage::abbreviatedPath(p.samplePath, 36), box(198, 585, 270, 18),
         style.value);
    button(
        box(480, 584, 80, 20), "RELINK", [this] { load(true); }, false,
        !p.samplePath.empty());
    text(storageSummary(p), box(28, 610, 532, 17), style.label);
    const auto prm = snapshotParams(p);
    panel(box(kToolboxX, 48, kToolboxW, 54), "OUTPUT");
    row("OUT", kGainParamId, 84, -60, 6);
    panel(box(kToolboxX, 114, kToolboxW, 184), "ENGINE");
    categorical("ORDER", kOrderParamId, 150, {"1OA", "2OA", "3OA"}, 1);
    categorical("MODE", kModeParamId, 176, {"SCAN", "CLOUD", "FREEZE", "JUMP"});
    row("DENS", kDensityParamId, 202, .1, s3g::kAmbiGrainMaxDensity);
    row("GMS", kGrainMsParamId, 228, 8, s3g::kAmbiGrainMaxGrainMs);
    row("RATE", kRateParamId, 254, .125, 4);
    panel(box(kToolboxX, 312, kToolboxW, 210), "SOURCE");
    const bool scan = prm.mode == s3g::AmbiGrainMode::Scan,
               jump = prm.mode == s3g::AmbiGrainMode::Jump;
    row(scan ? "S-SRC" : jump ? "J-SRC" : "SRC", kSourcePosParamId, 348, 0, 1,
        scan || jump);
    row("S-SPD", kScanSpeedParamId, 374, 0, 4, scan);
    row("JIT", kJitterParamId, 400, 0, 1);
    row("RJIT", kRateJitterParamId, 426, 0, 1);
    row("REV", kReverseParamId, 452, 0, 1);
    row("F-SRC", kFreezeParamId, 478, 0, 1,
        prm.mode == s3g::AmbiGrainMode::Freeze);
    row("J-STP", kJumpStepsParamId, 504, 2, 64, jump);
    panel(box(kToolboxX, 536, kToolboxW, 92), "WINDOW");
    categorical("SYNC", kSyncParamId, 572, {"ASYNC", "SYNC"});
    categorical("ENV", kEnvelopeParamId, 598,
                {"PARZ", "SIN", "HANN", "TRI", "GAUS"});
  }
  void waveform(const std::shared_ptr<const s3g::AmbiGrainSample> &sample,
                CRect r) {
    fill(r, color(0x111111));
    line({r.left + 1, r.getCenter().y}, {r.right - 1, r.getCenter().y},
         color(0x2a2a2a));
    if (!sample || sample->frames < 2 || sample->channels == 0 ||
        sample->audio.empty()) {
      text("LOAD ACN/SN3D AMBISONIC FILE",
           rect(r.left + 12, r.top + 12, r.getWidth() - 24, 15), style.label);
      return;
    }
    const auto prm = snapshotParams(p);
    const double waveX = r.left + 6, waveW = r.getWidth() - 12,
                 midY = r.getCenter().y, scaleY = (r.getHeight() - 28) * .46;
    const uint32_t columns =
        static_cast<uint32_t>(std::max(32., std::floor(waveW)));
    context->saveGlobalState();
    CRect clip;
    context->getClipRect(clip);
    clip.bound(CRect(r).inset(1, 1));
    context->setClipRect(clip);
    for (uint32_t x = 0; x < columns; ++x) {
      const uint32_t a =
          static_cast<uint32_t>(uint64_t(x) * sample->frames / columns);
      const uint32_t b =
          std::max(a + 1, static_cast<uint32_t>(uint64_t(x + 1) *
                                                sample->frames / columns));
      float lo = 0, hi = 0;
      for (uint32_t i = a; i < std::min(b, sample->frames);
           i += std::max(1u, (b - a) / 32)) {
        const float v = sample->audio[size_t(i) * sample->channels];
        lo = std::min(lo, v);
        hi = std::max(hi, v);
      }
      line(
          {waveX + x, std::clamp(midY - hi * scaleY, r.top + 6, r.bottom - 22)},
          {waveX + x, std::clamp(midY - lo * scaleY, r.top + 6, r.bottom - 22)},
          color(0x747474), .75);
    }
    double center = prm.sourcePosition;
    const bool autoScan =
        prm.mode == s3g::AmbiGrainMode::Scan && center <= .0001;
    if (autoScan || prm.mode == s3g::AmbiGrainMode::Cloud)
      center = p.visualPhase.load();
    else if (prm.mode == s3g::AmbiGrainMode::Freeze)
      center = prm.freezePosition;
    else if (prm.mode == s3g::AmbiGrainMode::Jump)
      center = std::floor(prm.sourcePosition * prm.jumpSteps) /
               std::max(1u, prm.jumpSteps - 1);
    center = std::clamp(center, 0., 1.);
    const double sourceX = waveX + center * waveW,
                 jitterW = prm.positionJitter * waveW;
    const double unitWidth =
        std::clamp((prm.grainMs * .001) / std::max(.25, double(sample->frames) /
                                                            sample->sampleRate),
                   .002, 1.);
    const double grainW = unitWidth * waveW,
                 pulse = std::clamp(double(p.visualPulse.load()), 0., 1.);
    const uint32_t count =
        std::clamp(static_cast<uint32_t>(std::ceil(
                       std::max(1.f, prm.density * prm.grainMs * .001f))),
                   1u, 32u);
    const double top = r.top + 12, bottom = r.bottom - 66,
                 height = bottom - top;
    const auto scan = rect(std::max(waveX, sourceX - grainW * .5), r.top + 8,
                           std::min(r.right - 6, sourceX + grainW * .5) -
                               std::max(waveX, sourceX - grainW * .5),
                           bottom - r.top - 8);
    fill(scan, rgba(.15, .76, .86, .09 + pulse * .06));
    stroke(scan, rgba(.30, .88, .95, .38));
    for (double x :
         {autoScan ? waveX : std::max(waveX, sourceX - jitterW),
          autoScan ? r.right - 6 : std::min(r.right - 6, sourceX + jitterW)}) {
      line({x, r.top + 8}, {x, bottom}, rgba(.42, .82, .86, .34));
      fill(rect(x - 2, r.top + 8, 4, 3), rgba(.42, .82, .86, .46));
      fill(rect(x - 2, bottom - 3, 4, 3), rgba(.42, .82, .86, .46));
    }
    const auto hash = [](uint32_t seed) {
      seed ^= seed >> 16;
      seed *= 0x7feb352d;
      seed ^= seed >> 15;
      seed *= 0x846ca68b;
      seed ^= seed >> 16;
      return (seed & 0x00ffffff) / double(0x01000000);
    };
    const auto wrap = [](double v) { return v - std::floor(v); };
    for (uint32_t i = 0; i < count; ++i) {
      double unit = center;
      if (prm.mode == s3g::AmbiGrainMode::Cloud)
        unit =
            hash(i * 97 + static_cast<uint32_t>(p.visualPhase.load() * 4096));
      else if (prm.positionJitter > .0001)
        unit += (hash(i * 131 + 17) * 2 - 1) * prm.positionJitter;
      else
        unit += (double(i) / count - .5) * unitWidth;
      if (prm.mode != s3g::AmbiGrainMode::Freeze)
        unit = wrap(unit);
      unit = std::clamp(unit, 0., 1.);
      const double x = waveX + unit * waveW,
                   age = wrap(p.visualPhase.load() + double(i) / count);
      const double y = top + age * height, w = std::clamp(grainW, 5., 68.),
                   alpha = .16 + pulse * .16 + (1 - age) * .10;
      fill(rect(std::max(waveX, x - w * .5), std::max(top, y - 1),
                std::min(r.right - 6, x + w * .5) - std::max(waveX, x - w * .5),
                2),
           rgba(.42, .90, .98, alpha * .36));
      fill(rect(x - 1.5, std::max(top, y - 1.5), 3, 3),
           rgba(.55, .94, 1, alpha));
      line({x, std::max(top, y - 5)}, {x, std::min(bottom, y + 5)},
           rgba(.44, .82, .90, .18 + pulse * .12));
      fill(rect(x - 1, bottom + 12 + hash(i * 43 + 9) * 18, 2, 8),
           rgba(.50, .90, .96, .20 + pulse * .16));
    }
    line({waveX, bottom + 6}, {r.right - 6, bottom + 6}, color(0x333333));
    line({sourceX, r.top + 6}, {sourceX, bottom}, color(0xf0f0f0));
    text(format("W  %.1f SEC", double(sample->frames) / sample->sampleRate),
         rect(r.left + 10, r.bottom - 19, 180, 15), style.label);
    text(format("%u GRAINS", count), rect(r.right - 82, r.bottom - 19, 78, 15),
         style.label);
    context->restoreGlobalState();
  }

private:
  Plugin &p;
  clap_id editing = CLAP_INVALID_ID;
  std::string presetName = "CURRENT";
};
} // namespace ambi_grain_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new ambi_grain_canvas::Editor(p);
}
