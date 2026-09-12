// Original Vox field, cue sheet, score and control geometry.
namespace complex_processor_canvas {
namespace drawing = s3g::portable_gui::complex_drawing;
namespace foundation = s3g::portable_gui::foundation;
static VSTGUI::CColor votColor(int rgb, double alpha = 1.0) {
  return drawing::color(rgb, alpha);
}

constexpr double kLyricsSliderTrackOffset = 110.0;
constexpr double kGeneratorSliderTrackOffset = 126.0;
constexpr double kLyricsSliderTrackWidth = 170.0;
constexpr int kLyricCueChannelMenuId = 16;
constexpr s3g::gui_layout::Panel kVoiceEncoderPanel{
    s3g::gui_layout::PluginClass::ProceduralEncoder,
    s3g::gui_layout::PanelRole::Engine,
    {630.0, 134.0, 250.0, 132.0},
    36.0,
    26.0,
    4u,
};
static_assert(s3g::gui_layout::sourceCardinalityControlMatches(
    kVoiceEncoderPanel, s3g::gui_layout::SharedControlRole::SourceCardinality));
constexpr double kVoiceCountRowY = static_cast<double>(s3g::gui_layout::rowY(
    kVoiceEncoderPanel,
    s3g::gui_layout::sourceCardinalityRow(
        s3g::gui_layout::SharedControlRole::SourceCardinality)));

double voxSliderNormForPoint(double pointX, double trackX, double trackWidth) {
  return std::clamp(static_cast<double>((pointX - trackX) / trackWidth), 0.0,
                    1.0);
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

static uint32_t voxEnsembleCohort(s3g::AmbiVoxOrchestration orchestration,
                                  uint32_t voice, uint32_t voices) {
  if (orchestration == s3g::AmbiVoxOrchestration::Unison)
    return 0u;
  if (orchestration == s3g::AmbiVoxOrchestration::Chorale ||
      orchestration == s3g::AmbiVoxOrchestration::Chorus ||
      orchestration == s3g::AmbiVoxOrchestration::Round) {
    return voice % std::min<uint32_t>(std::max<uint32_t>(1u, voices), 4u);
  }
  return voice;
}

static drawing::String
voxEnsembleRoleLabel(s3g::AmbiVoxOrchestration orchestration, uint32_t voice,
                     uint32_t voices) {
  const uint32_t cohort = voxEnsembleCohort(orchestration, voice, voices);
  switch (orchestration) {
  case s3g::AmbiVoxOrchestration::Unison:
    return "U";
  case s3g::AmbiVoxOrchestration::Chorale: {
    static drawing::String labels[] = {"S", "A", "T", "B"};
    return labels[cohort % 4u];
  }
  case s3g::AmbiVoxOrchestration::Chorus: {
    static drawing::String labels[] = {"U1", "U2", "+8", "-8"};
    return labels[cohort % 4u];
  }
  case s3g::AmbiVoxOrchestration::Round:
    return drawing::stringFormat("R%u", cohort + 1u);
  case s3g::AmbiVoxOrchestration::Cluster:
    if (voice == 0u)
      return "0";
    return drawing::stringFormat((voice & 1u) != 0u ? "+%u" : "-%u",
                                 (voice + 1u) / 2u);
  case s3g::AmbiVoxOrchestration::Individual:
  default:
    return "";
  }
}

static VSTGUI::CColor
voxEnsembleRoleColor(s3g::AmbiVoxOrchestration orchestration, uint32_t voice,
                     uint32_t voices) {
  if (orchestration == s3g::AmbiVoxOrchestration::Individual)
    return votColor(0x8a8a8a);
  static constexpr int colors[] = {0xc99c9c, 0xc2b074, 0x83aaa4, 0x929dbd};
  const uint32_t cohort = voxEnsembleCohort(orchestration, voice, voices);
  return votColor(colors[cohort % 4u]);
}

static double voxPlaybackPanelHeight(VoxSpeechMode mode) {
  const uint32_t visibleRows = mode == VoxSpeechMode::Texture ? 10u : 7u;
  return static_cast<double>(
      s3g::gui_layout::toolboxHeightForRows(visibleRows));
}

static double voxWorldPanelY(VoxSpeechMode mode) {
  return 278.0 + voxPlaybackPanelHeight(mode) +
         static_cast<double>(s3g::gui_layout::kStandardMetrics.panelGap);
}

static double voxWorldRowY(VoxSpeechMode mode, uint32_t row) {
  return static_cast<double>(
      s3g::gui_layout::toolboxRowY(voxWorldPanelY(mode), row));
}

#include "s3g_complex_processor_vox_files.inc"
#include "s3g_complex_processor_vox_voicebank.inc"
class Editor final : public s3g::portable_gui::routing::Canvas,
                     public VSTGUI::ITextEditorController {
public:
  using Canvas = s3g::portable_gui::routing::Canvas;

  Plugin *_plugin;
  bool _updatingLyricsEditor;
  bool _showLyricGenerator;
  int _generatorDrag;
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
  uint32_t _selectedLyricCue;
  int _scoreDragLane;
  int _viewMode;
  double _viewAzDeg;
  double _viewElDeg;
  double _viewZoom;
  bool _dragView;
  drawing::Point _lastDragPoint;
  std::array<std::array<s3g::AmbiVotMotionPoint, 48>, s3g::kAmbiVoxMaxVoices>
      _trails;
  uint32_t _trailHead;
  uint32_t _trailCount;
#include "s3g_complex_processor_vox_editor.inc"
  explicit Editor(Plugin &plugin) : Canvas(kGuiW, kGuiH) {
    _plugin = &plugin;
    _updatingLyricsEditor = false;
    _showLyricGenerator = false;
    _generatorDrag = 0;
    _dragParam = CLAP_INVALID_ID;
    _dragArea = 0;
    _openMenu = 0;
    _hoverMenuItem = -1;
    _menuItemCount = 0;
    _menuOrigin = drawing::makePoint(0, 0);
    _menuWidth = 124.0;
    _leftPage = _plugin && _plugin->guiPage == 3 ? 3 : 0;
    _selectedVoice = 0;
    _selectedScoreNode = 0;
    _selectedLyricCue =
        _plugin ? _plugin->guiLyricCue.load(std::memory_order_relaxed) : 0u;
    _scoreDragLane = -1;
    _viewMode = _plugin ? _plugin->guiViewMode : 0;
    _viewAzDeg = _plugin ? _plugin->guiViewAzDeg : 38.0;
    _viewElDeg = _plugin ? _plugin->guiViewElDeg : 32.0;
    _viewZoom = _plugin ? _plugin->guiViewZoom : 1.0;
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
    if (_plugin && _viewMode >= 0 && _viewMode <= 2) {
      _plugin->guiViewAzDeg = static_cast<float>(_viewAzDeg);
      _plugin->guiViewElDeg = static_cast<float>(_viewElDeg);
    }
    _dragView = false;
    _lastDragPoint = drawing::makePoint(0, 0);
    _trailHead = 0;
    _trailCount = 0;

    syncVisible();
  }

  void installGeneratedLyricsAppending(bool append) {
    if (!_plugin)
      return;
    clampVoxPhraseGeneratorRecipe(_plugin->generator);
    const auto bank = std::atomic_load_explicit(&_plugin->voicebank,
                                                std::memory_order_acquire);
    std::string generated = generateVoxPhraseGroups(
        _plugin->generator, bank ? bank.get() : nullptr);
    if (generated.empty())
      return;

    std::string lyrics;
    if (append) {
      lyrics = loadVoxLyrics(*_plugin);
      const uint32_t existingCount =
          static_cast<uint32_t>(splitVoxLyricCues(lyrics).size());
      const auto generatedCues = splitVoxLyricCues(generated);
      const uint32_t available = existingCount < kVoxLyricsMaxCues
                                     ? kVoxLyricsMaxCues - existingCount
                                     : 0u;
      if (available == 0u)
        return;
      while (!lyrics.empty() &&
             std::isspace(static_cast<unsigned char>(lyrics.back())))
        lyrics.pop_back();
      if (!lyrics.empty())
        lyrics += "\n\n";
      for (uint32_t index = 0u;
           index < std::min<uint32_t>(
                       available, static_cast<uint32_t>(generatedCues.size()));
           ++index) {
        if (index > 0u)
          lyrics.push_back('\n');
        lyrics += generatedCues[index];
        if (lyrics.size() >= kVoxLyricsMaxChars - 1u) {
          lyrics.resize(kVoxLyricsMaxChars - 1u);
          break;
        }
      }
    } else {
      lyrics = std::move(generated);
    }

    storeVoxLyricsText(*_plugin, lyrics.c_str());
    rebuildVoxLyricScore(*_plugin);
    _selectedLyricCue = 0u;
    requestVoxLyricCue(*_plugin, 0u);
    const std::string firstCue = voxLyricCueText(*_plugin, 0u);
    storeVoxPhrase(*_plugin, firstCue.c_str());
    setPhraseText(drawing::stringFromUtf8(firstCue.c_str()));
    _updatingLyricsEditor = true;
    setLyricsText(drawing::stringFromUtf8(lyrics.c_str()));
    _updatingLyricsEditor = false;
    markPresetCustom();
    setNeedsDisplay(true);
  }

  void updateGeneratorDragAtPoint(drawing::Point point) {
    if (!_plugin || _generatorDrag <= 0)
      return;
    const drawing::Rect content = leftContentRect();
    const double norm = voxSliderNormForPoint(
        point.x, content.origin.x + kGeneratorSliderTrackOffset,
        kLyricsSliderTrackWidth);
    switch (_generatorDrag) {
    case 1:
      _plugin->generator.groups =
          1u + static_cast<uint32_t>(std::lround(norm * 7.0));
      break;
    case 2:
      _plugin->generator.cuesPerGroup =
          1u + static_cast<uint32_t>(std::lround(norm * 7.0));
      break;
    case 3:
      _plugin->generator.syllables =
          1u + static_cast<uint32_t>(std::lround(norm * 7.0));
      break;
    case 4:
      _plugin->generator.variation = static_cast<float>(norm);
      break;
    case 5:
      _plugin->generator.hardness = static_cast<float>(norm);
      break;
    case 6:
      _plugin->generator.rest = static_cast<float>(norm);
      break;
    case 7:
      _plugin->generator.seed =
          1u + static_cast<uint32_t>(std::lround(norm * 999998.0));
      break;
    default:
      return;
    }
    clampVoxPhraseGeneratorRecipe(_plugin->generator);
    markPresetCustom();
    setNeedsDisplay(true);
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
    if (!_plugin || index >= s3g::kAmbiVoxMaxVoices)
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
    for (uint32_t i = 0; i < s3g::kAmbiVoxMaxVoices; ++i)
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

  drawing::Rect presetMenuRect() {
    return drawing::encoderTitleActionRect(
        kGuiW, kGuiH, s3g::gui_layout::EncoderTitleAction::Preset);
  }

  drawing::Rect presetLoadButtonRect() {
    return drawing::encoderTitleActionRect(
        kGuiW, kGuiH, s3g::gui_layout::EncoderTitleAction::Load);
  }

  drawing::Rect presetSaveButtonRect() {
    return drawing::encoderTitleActionRect(
        kGuiW, kGuiH, s3g::gui_layout::EncoderTitleAction::Save);
  }

  drawing::Rect randomizeButtonRect() {
    return drawing::encoderTitleActionRect(
        kGuiW, kGuiH, s3g::gui_layout::EncoderTitleAction::Random);
  }

  drawing::Rect worldResetButtonRect() {
    return drawing::makeRect(1016, 424, 56, 13);
  }

  drawing::Rect encodedLoadButtonRect() {
    return drawing::makeRect(1078, 424, 56, 13);
  }

  drawing::String presetDisplayName() {
    drawing::String name = _plugin && _plugin->presetName[0] != '\0'
                               ? drawing::stringFromUtf8(_plugin->presetName)
                               : "CUSTOM";
    if (name.length() <= 22u)
      return name;
    return (name.substr(0, 19u) + "...");
  }

  void markPresetCustom() {
    if (!_plugin)
      return;
    _plugin->factoryPresetIndex = -1;
    std::snprintf(_plugin->presetName, sizeof(_plugin->presetName), "%s",
                  "CUSTOM");
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
    static drawing::String labels[] = {"FIELD", "LYRICS"};
    static constexpr int pages[] = {0, 3};
    const drawing::Rect header = drawing::makeRect(18, 42, 596, 21);
    for (int i = 0; i < 2; ++i) {
      drawing::drawToolboxHeaderButton(pageButtonRect(i), header, labels[i],
                                       pages[i] == _leftPage, attrs, style);
    }
  }

  void drawViewButtons_style(drawing::Attrs attrs,
                             const drawing::Style &style) {
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
    drawing::GraphicsScope graphics(*context);
    auto clip = foundation::rect(rect.origin.x + 1, rect.origin.y + 1,
                                 rect.size.width - 2, rect.size.height - 2);
    context->setClipRect(clip);

    const double radius = viewScaleForRect(rect);
    drawing::setStroke(votColor(0x343434));
    drawing::Path sphere =
        drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
            drawing::midX(rect) - radius, drawing::midY(rect) - radius,
            radius * 2.0, radius * 2.0));
    sphere.setLineWidth(0.8);
    sphere.stroke();
    drawing::drawAmbisonicOrientationGuides([&](double x, double y, double z) {
      return projectWorldPoint_rect_depth(
          {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
          rect, nullptr);
    });

    const uint32_t voices =
        std::clamp<uint32_t>(view.params.voices, 1u, s3g::kAmbiVoxMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u);
    struct ProjectedPoint {
      uint32_t index;
      double depth;
      drawing::Point point;
      s3g::Vec3 world;
      s3g::AmbiVotMotionPoint motion;
    };
    std::array<ProjectedPoint, s3g::kAmbiVoxMaxVoices> projected{};
    std::array<drawing::Point, s3g::kAmbiVoxMaxVoices> voicePoints{};
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
      voicePoints[voice] = projected[voice].point;
    }
    const auto orchestration = view.vox.orchestration;
    if (orchestration != s3g::AmbiVoxOrchestration::Individual &&
        orchestration != s3g::AmbiVoxOrchestration::Cluster) {
      std::array<int32_t, 4> previous{{-1, -1, -1, -1}};
      for (uint32_t voice = 0u; voice < voices; ++voice) {
        const uint32_t cohort =
            voxEnsembleCohort(orchestration, voice, voices) % 4u;
        if (previous[cohort] >= 0) {
          drawing::Path cohortPath = drawing::Path::bezierPath();
          cohortPath.moveToPoint(
              voicePoints[static_cast<uint32_t>(previous[cohort])]);
          cohortPath.lineToPoint(voicePoints[voice]);
          drawing::setStroke(drawing::withAlpha(
              voxEnsembleRoleColor(orchestration, voice, voices), 0.18));
          cohortPath.setLineWidth(0.65);
          cohortPath.stroke();
        }
        previous[cohort] = static_cast<int32_t>(voice);
      }
    }
    std::sort(projected.begin(), projected.begin() + voices,
              [](const ProjectedPoint &a, const ProjectedPoint &b) {
                return a.depth < b.depth;
              });
    for (uint32_t drawIndex = 0; drawIndex < voices; ++drawIndex) {
      const auto &item = projected[drawIndex];
      const bool selected = item.index == _selectedVoice;
      const bool midiActive = view.params.mode != s3g::AmbiVotMode::Midi ||
                              _plugin->guiMidiGate[item.index].load(
                                  std::memory_order_relaxed) != 0u;
      const double size = selected ? 15.0 : (voices > 32u ? 9.0 : 12.0);
      const drawing::Rect pointRect = drawing::makeRect(
          item.point.x - size * 0.5, item.point.y - size * 0.5, size, size);
      const float phrasePhase =
          std::clamp(_plugin->guiVoxPhrasePhase[item.index].load(
                         std::memory_order_relaxed),
                     0.0f, 1.0f);
      const float phrasePulse =
          0.5f - 0.5f * std::cos(2.0f * s3g::kPi * phrasePhase);
      const double roleSize =
          size + 4.0 + static_cast<double>(phrasePulse) * 5.0;
      const drawing::Rect roleRect =
          drawing::makeRect(item.point.x - roleSize * 0.5,
                            item.point.y - roleSize * 0.5, roleSize, roleSize);
      VSTGUI::CColor roleColor =
          voxEnsembleRoleColor(orchestration, item.index, voices);
      drawing::setStroke(drawing::withAlpha(
          roleColor, midiActive ? 0.20 + phrasePulse * 0.48 : 0.07));
      drawing::Path roleFrame = drawing::Path::bezierPathWithRect(roleRect);
      roleFrame.setLineWidth(selected ? 1.35 : 0.85);
      roleFrame.stroke();
      VSTGUI::CColor pointColor =
          midiActive
              ? votPointColor(item.motion.azimuthDeg, item.motion.elevationDeg,
                              item.motion.distance, selected)
              : votColor(0x343434);
      drawing::setFill(
          drawing::withAlpha(pointColor, midiActive ? 0.96 : 0.72));
      drawing::rectFill(pointRect);
      drawing::setStroke(
          votColor(selected ? 0xe0e0e0 : (midiActive ? 0x111111 : 0x606060)));
      drawing::frameRect(pointRect);
      drawing::String label = drawing::stringFormat("%u", item.index + 1u);
      drawing::Attrs idAttrs = drawing::textAttrs(
          votColor(midiActive ? 0x0b0b0b : 0xa0a0a0), voices > 32u ? 5.5 : 7.0);
      const drawing::Size labelSize =
          drawing::sizeWithAttributes(label, idAttrs);
      drawing::drawAtPoint_withAttributes(
          label,
          drawing::makePoint(drawing::midX(pointRect) - labelSize.width * 0.5,
                             drawing::midY(pointRect) - labelSize.height * 0.5 -
                                 0.5),
          idAttrs);
      drawing::String roleLabel =
          voxEnsembleRoleLabel(orchestration, item.index, voices);
      if (roleLabel.length() > 0u) {
        drawing::Attrs roleAttrs = drawing::textAttrs(
            drawing::withAlpha(roleColor, selected ? 1.0 : 0.82), 7.0);
        drawing::drawAtPoint_withAttributes(
            roleLabel,
            drawing::makePoint(drawing::maxX(roleRect) + 2.0,
                               roleRect.origin.y - 1.0),
            roleAttrs);
      }
    }
    graphics.restore();

    const auto selected = snapshotPoint(_selectedVoice);
    drawing::String roleLabel =
        voxEnsembleRoleLabel(orchestration, _selectedVoice, voices);
    const float phrasePhase =
        std::clamp(_plugin->guiVoxPhrasePhase[_selectedVoice].load(
                       std::memory_order_relaxed),
                   0.0f, 1.0f);
    drawing::String readout =
        roleLabel.length() > 0u
            ? drawing::stringFormat(
                  "V%u %@   AZ %+06.1f   EL %+05.1f   D %.2f   P %.0f%%",
                  _selectedVoice + 1u, roleLabel, selected.azimuthDeg,
                  selected.elevationDeg, selected.distance,
                  phrasePhase * 100.0f)
            : drawing::stringFormat(
                  "V%u   AZ %+06.1f   EL %+05.1f   D %.2f   P %.0f%%",
                  _selectedVoice + 1u, selected.azimuthDeg,
                  selected.elevationDeg, selected.distance,
                  phrasePhase * 100.0f);
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
        path.moveToPoint(drawing::makePoint(x, y));
      else
        path.lineToPoint(drawing::makePoint(x, y));
    }
    drawing::setStroke(color);
    path.setLineWidth(0.75);
    path.stroke();
  }

  void drawVectorPage_attrs_valueAttrs_style(drawing::Rect rect,
                                             drawing::Attrs attrs,
                                             drawing::Attrs valueAttrs,
                                             const drawing::Style &style) {
    auto bank = activeBank(*_plugin);
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
        std::clamp<uint32_t>(view.params.voices, 1u, s3g::kAmbiVoxMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u);
    const auto selected = snapshotPoint(_selectedVoice);
    const auto vectorWeights =
        s3g::ambiVotVectorWeights(selected.u, selected.v, view.params.morph);
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
        hill.setLineWidth(0.65);
        hill.stroke();
        const double influence = std::sqrt(vectorWeights.values[table] /
                                           std::max(0.000001f, maxWeight));
        drawing::setFill(
            drawing::calibratedWhite(0.26 + influence * 0.50, 1.0));
        drawing::rectFill(
            drawing::makeRect(center.x - 2.0, center.y - 2.0, 4.0, 4.0));
      }
    }
    const bool vectorMoving =
        view.params.motionScene != s3g::AmbiVotMotionScene::Manual;
    const float vectorCoverage = vectorMoving ? view.params.scan : 0.0f;
    if (vectorCoverage > 0.001f) {
      const float linkedU =
          0.5f + 0.5f * std::sin(selected.azimuthDeg * s3g::kPi / 180.0f);
      const float linkedV =
          std::clamp((selected.elevationDeg + 90.0f) / 180.0f, 0.0f, 1.0f);
      const float centerU =
          s3g::lerp(view.params.vectorX, linkedU, view.params.motionLink);
      const float centerV =
          s3g::lerp(view.params.vectorY, linkedV, view.params.motionLink);
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
          path.moveToPoint(p);
        else
          path.lineToPoint(p);
      }
      const auto point = snapshotPoint(_selectedVoice);
      drawing::setStroke(
          drawing::withAlpha(votPointColor(point.azimuthDeg, point.elevationDeg,
                                           point.distance, true),
                             0.64));
      path.setLineWidth(1.4);
      path.stroke();
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
        waveform.moveToPoint(drawing::makePoint(x, y));
      else
        waveform.lineToPoint(drawing::makePoint(x, y));
    }
    drawing::setStroke(votPointColor(selected.azimuthDeg, selected.elevationDeg,
                                     selected.distance, true));
    waveform.setLineWidth(1.25);
    waveform.stroke();
    drawing::String waveformReadout = drawing::stringFormat(
        "V%u   U %.3f   V %.3f   LINK %.0f%%", _selectedVoice + 1u, selected.u,
        selected.v, view.params.motionLink * 100.0f);
    const drawing::Size waveformReadoutSize =
        drawing::sizeWithAttributes(waveformReadout, valueAttrs);
    drawing::drawAtPoint_withAttributes(
        waveformReadout,
        drawing::makePoint(drawing::maxX(waveRect) - waveformReadoutSize.width -
                               8,
                           waveRect.origin.y + 7),
        valueAttrs);

    drawing::drawSlider("X", drawing::stringFormat("%.2f", view.params.vectorX),
                        view.params.vectorX, rect.origin.y + 606, attrs,
                        valueAttrs, style, rect.origin.x + 8,
                        rect.origin.x + 86, rect.origin.x + 270, 170);
    drawing::drawSlider("Y", drawing::stringFormat("%.2f", view.params.vectorY),
                        view.params.vectorY, rect.origin.y + 632, attrs,
                        valueAttrs, style, rect.origin.x + 8,
                        rect.origin.x + 86, rect.origin.x + 270, 170);
    drawing::drawSlider(
        "SCAN", drawing::stringFormat("%.0f%%", view.params.scan * 100.0f),
        view.params.scan, rect.origin.y + 658, attrs, valueAttrs, style,
        rect.origin.x + 8, rect.origin.x + 86, rect.origin.x + 270, 170);
    drawing::drawSlider(
        "MORPH", drawing::stringFormat("%.0f%%", view.params.morph * 100.0f),
        view.params.morph, rect.origin.y + 684, attrs, valueAttrs, style,
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
    drawing::String scoreStatus =
        drawing::stringFormat("%u NODES   %@   %.2f S", count,
                              drawing::stringFromUtf8(s3g::ambiVotScoreModeName(
                                  view.params.scoreMode)),
                              view.params.scoreDurationSec);
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
            path.moveToPoint(point);
            first = false;
          } else
            path.lineToPoint(point);
        }
      }
      drawing::setStroke(color);
      path.setLineWidth(1.2);
      path.stroke();
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
        route.moveToPoint(point);
      else
        route.lineToPoint(point);
    }
    drawing::setStroke(votColor(0x949494));
    route.setLineWidth(1.15);
    route.stroke();
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

  drawing::Rect lyricCueRect(uint32_t index) {
    const drawing::Rect content = leftContentRect();
    const uint32_t column = index % 4u;
    const uint32_t row = index / 4u;
    return drawing::makeRect(
        content.origin.x + 8.0 + static_cast<double>(column) * 135.0,
        content.origin.y + 446.0 + static_cast<double>(row) * 20.0, 131.0,
        17.0);
  }

  drawing::Rect lyricEditorModeButtonRect(int index) {
    const drawing::Rect content = leftContentRect();
    const double width = index == 0 ? 54.0 : 82.0;
    const double x = index == 0 ? drawing::maxX(content) - 142.0
                                : drawing::maxX(content) - 84.0;
    return drawing::makeRect(x, content.origin.y + 4.0, width, 17.0);
  }

  drawing::Rect generatorCommandButtonRect(int index) {
    const drawing::Rect content = leftContentRect();
    return drawing::makeRect(content.origin.x + 126.0 +
                                 static_cast<double>(index) * 112.0,
                             content.origin.y + 358.0, 104.0, 20.0);
  }

  void drawLyricGenerator_attrs_valueAttrs_style(drawing::Rect rect,
                                                 drawing::Attrs attrs,
                                                 drawing::Attrs valueAttrs,
                                                 const drawing::Style &style) {
    const auto &generator = _plugin->generator;
    const drawing::Rect generatorRect =
        drawing::makeRect(rect.origin.x + 10.0, rect.origin.y + 32.0,
                          rect.size.width - 20.0, 370.0);
    drawing::setFill(votColor(0x101010));
    drawing::rectFill(generatorRect);
    drawing::setStroke(votColor(0x3f3f3f));
    drawing::frameRect(generatorRect);

    const double labelX = rect.origin.x + 18.0;
    const double trackX = rect.origin.x + kGeneratorSliderTrackOffset;
    const double valueX = rect.origin.x + 300.0;
    constexpr double trackWidth = kLyricsSliderTrackWidth;
    drawing::drawMenu(
        "FORM", drawing::stringFromUtf8(voxPhraseFormName(generator.form)),
        rect.origin.y + 48.0, attrs, valueAttrs, style, labelX, trackX, 180.0);
    drawing::drawMenu(
        "COLOR", drawing::stringFromUtf8(voxPhraseColorName(generator.color)),
        rect.origin.y + 76.0, attrs, valueAttrs, style, labelX, trackX, 180.0);
    drawing::drawSlider("GROUPS", drawing::stringFormat("%u", generator.groups),
                        static_cast<double>(generator.groups - 1u) / 7.0,
                        rect.origin.y + 112.0, attrs, valueAttrs, style, labelX,
                        trackX, valueX, trackWidth);
    drawing::drawSlider("CUES / GROUP",
                        drawing::stringFormat("%u", generator.cuesPerGroup),
                        static_cast<double>(generator.cuesPerGroup - 1u) / 7.0,
                        rect.origin.y + 140.0, attrs, valueAttrs, style, labelX,
                        trackX, valueX, trackWidth);
    drawing::drawSlider("LENGTH",
                        drawing::stringFormat("%u", generator.syllables),
                        static_cast<double>(generator.syllables - 1u) / 7.0,
                        rect.origin.y + 168.0, attrs, valueAttrs, style, labelX,
                        trackX, valueX, trackWidth);
    drawing::drawSlider(
        "VARIATION",
        drawing::stringFormat("%.0f%%", generator.variation * 100.0f),
        generator.variation, rect.origin.y + 196.0, attrs, valueAttrs, style,
        labelX, trackX, valueX, trackWidth);
    drawing::drawSlider(
        "HARDNESS",
        drawing::stringFormat("%.0f%%", generator.hardness * 100.0f),
        generator.hardness, rect.origin.y + 224.0, attrs, valueAttrs, style,
        labelX, trackX, valueX, trackWidth);
    drawing::drawSlider(
        "REST", drawing::stringFormat("%.0f%%", generator.rest * 100.0f),
        generator.rest, rect.origin.y + 252.0, attrs, valueAttrs, style, labelX,
        trackX, valueX, trackWidth);
    drawing::drawSlider("SEED", drawing::stringFormat("%u", generator.seed),
                        static_cast<double>(generator.seed - 1u) / 999998.0,
                        rect.origin.y + 280.0, attrs, valueAttrs, style, labelX,
                        trackX, valueX, trackWidth);

    drawing::String status = drawing::stringFormat(
        "%u CUES   GEN %u",
        std::min<uint32_t>(kVoxLyricsMaxCues,
                           generator.groups * generator.cuesPerGroup),
        generator.generation);
    drawing::drawAtPoint_withAttributes(
        status, drawing::makePoint(labelX, rect.origin.y + 324.0), valueAttrs);
    const drawing::Rect commandRow =
        drawing::makeRect(rect.origin.x + 10.0, rect.origin.y + 354.0,
                          rect.size.width - 20.0, 28.0);
    drawing::drawHeaderActionButton(generatorCommandButtonRect(0), commandRow,
                                    "GENERATE", attrs, style);
    drawing::drawHeaderActionButton(generatorCommandButtonRect(1), commandRow,
                                    "MUTATE", attrs, style);
    drawing::drawHeaderActionButton(generatorCommandButtonRect(2), commandRow,
                                    "APPEND", attrs, style);
  }

  void drawLyricsPage_attrs_valueAttrs_style(drawing::Rect rect,
                                             drawing::Attrs attrs,
                                             drawing::Attrs valueAttrs,
                                             const drawing::Style &style) {
    drawing::setFill(votColor(0x0b0b0b));
    drawing::rectFill(rect);
    drawing::setStroke(votColor(0x575757));
    drawing::frameRect(rect);
    drawing::drawAtPoint_withAttributes(
        "LYRIC CUES", drawing::makePoint(rect.origin.x + 8, rect.origin.y + 7),
        attrs);
    const drawing::Rect modeHeader =
        drawing::makeRect(rect.origin.x, rect.origin.y, rect.size.width, 24.0);
    drawing::drawToolboxHeaderButton(lyricEditorModeButtonRect(0), modeHeader,
                                     "TEXT", !_showLyricGenerator, attrs,
                                     style);
    drawing::drawToolboxHeaderButton(lyricEditorModeButtonRect(1), modeHeader,
                                     "GENERATE", _showLyricGenerator, attrs,
                                     style);
    if (_showLyricGenerator) {
      drawLyricGenerator_attrs_valueAttrs_style(rect, attrs, valueAttrs, style);
    }

    const auto cues = splitVoxLyricCues(loadVoxLyrics(*_plugin));
    const uint32_t cueCount = std::min<uint32_t>(
        static_cast<uint32_t>(cues.size()), kVoxLyricsMaxCues);
    _selectedLyricCue =
        cueCount > 0u ? std::min<uint32_t>(_selectedLyricCue, cueCount - 1u)
                      : 0u;
    const uint32_t activeCue =
        cueCount > 0u
            ? _plugin->guiLyricCue.load(std::memory_order_relaxed) % cueCount
            : 0u;
    drawing::drawAtPoint_withAttributes(
        "CUE MAP", drawing::makePoint(rect.origin.x + 8, rect.origin.y + 422),
        attrs);
    drawing::Attrs cueAttrs = drawing::textAttrs(votColor(0xa8a8a8), 8.5);
    for (uint32_t cue = 0u; cue < cueCount; ++cue) {
      const drawing::Rect cell = lyricCueRect(cue);
      drawing::setFill(votColor(cue == activeCue ? 0x484848 : 0x202020));
      drawing::rectFill(cell);
      drawing::setStroke(
          votColor(cue == _selectedLyricCue ? 0xc4c4c4 : 0x505050));
      drawing::frameRect(cell);
      drawing::String text = drawing::stringFormat(
          "%02u  %@", cue + 1u, drawing::stringFromUtf8(cues[cue].c_str()));
      drawing::drawWrappedText(text, drawing::inset(cell, 4, 2), cueAttrs);
    }

    const double controlsY = rect.origin.y + 618.0;
    drawing::drawMenu(
        "MODE", drawing::stringFromUtf8(voxLyricModeName(view.lyric.mode)),
        controlsY, attrs, valueAttrs, style, rect.origin.x + 8,
        rect.origin.x + 110, 150);
    char display[64]{};
    paramsValueToText(nullptr, kLyricCueBeatsParamId, view.lyric.cueBeats,
                      display, sizeof(display));
    const double beatNorm = (view.lyric.cueBeats - 0.25) / 63.75;
    drawing::drawSlider(
        "CUE BEATS", drawing::stringFormat("%.2f b", view.lyric.cueBeats),
        beatNorm, controlsY + 28, attrs, valueAttrs, style, rect.origin.x + 8,
        rect.origin.x + kLyricsSliderTrackOffset, rect.origin.x + 302,
        kLyricsSliderTrackWidth);
    paramsValueToText(nullptr, kLyricCueNoteParamId, view.lyric.cueBaseNote,
                      display, sizeof(display));
    drawing::drawSlider("BASE NOTE", drawing::stringFromUtf8(display),
                        static_cast<double>(view.lyric.cueBaseNote) / 127.0,
                        controlsY + 56, attrs, valueAttrs, style,
                        rect.origin.x + 8,
                        rect.origin.x + kLyricsSliderTrackOffset,
                        rect.origin.x + 302, kLyricsSliderTrackWidth);
    drawing::drawMenu(
        "MIDI CH", drawing::stringFormat("CHANNEL %u", view.lyric.cueChannel),
        controlsY + 84, attrs, valueAttrs, style, rect.origin.x + 8,
        rect.origin.x + kLyricsSliderTrackOffset, kLyricsSliderTrackWidth);
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
    else if (param == kMotionRateParamId || param == kScoreDurationParamId ||
             param == kVibratoRateParamId)
      norm = std::log(value / min) / std::log(max / min);
    else if (param == kAttackParamId || param == kDecayParamId ||
             param == kReleaseParamId)
      norm = std::log(value / min) / std::log(max / min);
    else if (param == kPvocStretchParamId)
      norm = std::log(value / min) / std::log(max / min);
    char text[64]{};
    paramsValueToText(nullptr, param, value, text, sizeof(text));
    // Cocoa abbreviates beat units in narrow value cells. Keep the number
    // visible with Fira Code rather than clipping it behind the unit suffix.
    if (param == kSyncDivisionParamId)
      std::snprintf(text, sizeof(text), "%.2f b", value);
    drawing::drawSlider(name, drawing::stringFromUtf8(text), norm, y, attrs,
                        valueAttrs, style, panelX + 16, panelX + 108,
                        panelX + 196, 82);
  }

  void drawPanels_valueAttrs_style(drawing::Attrs attrs,
                                   drawing::Attrs valueAttrs,
                                   const drawing::Style &style) {
    const auto p = view.params;
    const auto v = view.vox;
    drawing::drawPanelFrame(630, 42, 250, 80, style);
    drawing::drawPanelHeader("OUTPUT", true, 630, 42, 250, 21, attrs, style);
    drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
        "OUT", kOutputParamId, p.outputGainDb, -60, 12, 78, attrs, valueAttrs,
        style);
    drawMenu_value_y_attrs_valueAttrs_style(
        "ORDER", drawing::stringFormat("%uOA", p.order), 104, attrs, valueAttrs,
        style);

    drawing::drawPanelFrame(630, 134, 250, 132, style);
    drawing::drawPanelHeader("SOURCE / VOICE", true, 630, 134, 250, 21, attrs,
                             style);
    drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
        "VOICES", kVoicesParamId, p.voices, 1, s3g::kAmbiVoxMaxVoices,
        kVoiceCountRowY, attrs, valueAttrs, style);
    drawMenu_value_y_attrs_valueAttrs_style(
        "TRIGGER", drawing::stringFromUtf8(s3g::ambiVotModeName(p.mode)), 196,
        attrs, valueAttrs, style);
    drawMenu_value_y_attrs_valueAttrs_style(
        "VOICE", drawing::stringFromUtf8(voxSpeechModeName(v.speechMode)), 222,
        attrs, valueAttrs, style);
    drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
        "POP FILTER", kPopFilterParamId, view.popFilter, 0, 1, 248, attrs,
        valueAttrs, style);

    const bool textureMode = v.speechMode == VoxSpeechMode::Texture;
    const double playbackHeight = voxPlaybackPanelHeight(v.speechMode);
    drawing::drawPanelFrame(630, 278, 250, playbackHeight, style);
    drawing::drawPanelHeader(textureMode ? "TEXTURE PLAYBACK"
                                         : "PHRASE PLAYBACK",
                             true, 630, 278, 250, 21, attrs, style);
    if (textureMode) {
      drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
          "SPEED", kWorldRateParamId, v.worldRate, 0, 1, 314, attrs, valueAttrs,
          style);
      drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
          "LOOP START", kWorldLoopStartParamId, v.worldLoopStart, 0, 1, 340,
          attrs, valueAttrs, style);
      drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
          "LOOP END", kWorldLoopEndParamId, v.worldLoopEnd, 0, 1, 366, attrs,
          valueAttrs, style);
      drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
          "FREEZE", kWorldFreezeParamId, v.worldFreeze, 0, 1, 392, attrs,
          valueAttrs, style);
      drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
          "POSITION", kWorldScrubParamId, v.worldScrub, 0, 1, 418, attrs,
          valueAttrs, style);
      drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
          "PHR SPREAD", kPhraseSpreadParamId, v.phraseSpread, 0, 1, 444, attrs,
          valueAttrs, style);
      drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
          "STRETCH", kPvocStretchParamId, v.pvocStretch, 0.25, 4, 470, attrs,
          valueAttrs, style);
      drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
          "TRANSIENT", kPvocTransientParamId, v.pvocTransient, 0, 1, 496, attrs,
          valueAttrs, style);
      drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
          "VOICE STEP", kWorldVoiceSpreadParamId, v.worldVoiceSpread, 0, 1, 522,
          attrs, valueAttrs, style);
      drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
          "VOICE DEV", kWorldVoiceDeviationParamId, v.worldVoiceDeviation, 0, 1,
          548, attrs, valueAttrs, style);
    } else {
      drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
          "PACE", kPhraseRateParamId, v.phraseRate, 0, 1, 314, attrs,
          valueAttrs, style);
      drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
          "STRETCH", kPvocStretchParamId, v.pvocStretch, 0.25, 4, 340, attrs,
          valueAttrs, style);
      drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
          "TRANSIENT", kPvocTransientParamId, v.pvocTransient, 0, 1, 366, attrs,
          valueAttrs, style);
      drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
          "BLEND", kTransitionParamId, v.transition, 0, 1, 392, attrs,
          valueAttrs, style);
      drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
          "PHR SPREAD", kPhraseSpreadParamId, v.phraseSpread, 0, 1, 418, attrs,
          valueAttrs, style);
      drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
          "VOICE STEP", kWorldVoiceSpreadParamId, v.worldVoiceSpread, 0, 1, 444,
          attrs, valueAttrs, style);
      drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
          "VOICE DEV", kWorldVoiceDeviationParamId, v.worldVoiceDeviation, 0, 1,
          470, attrs, valueAttrs, style);
    }

    const double worldY = voxWorldPanelY(v.speechMode);
    drawing::drawPanelFrame(630, worldY, 250, 158, style);
    drawing::drawPanelHeader("WORLD / ENSEMBLE", true, 630, worldY, 250, 21,
                             attrs, style);
    drawMenu_value_y_attrs_valueAttrs_style(
        "ENSEMBLE",
        drawing::stringFromUtf8(s3g::ambiVoxOrchestrationName(v.orchestration)),
        voxWorldRowY(v.speechMode, 0u), attrs, valueAttrs, style);
    drawMenu_value_y_attrs_valueAttrs_style(
        "CONTOUR",
        drawing::stringFromUtf8(s3g::ambiVoxContourModeName(v.contourMode)),
        voxWorldRowY(v.speechMode, 1u), attrs, valueAttrs, style);
    drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
        "FORMANT", kFormantMacroParamId, v.formantMacro, -1, 1,
        voxWorldRowY(v.speechMode, 2u), attrs, valueAttrs, style);
    drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
        "PERIODIC", kWorldVoicingParamId, v.worldVoicing, 0, 1,
        voxWorldRowY(v.speechMode, 3u), attrs, valueAttrs, style);
    drawSlider_param_value_min_max_y_attrs_valueAttrs_style(
        "AIR COLOR", kWorldAirColorParamId, v.worldAirColor, -1, 1,
        voxWorldRowY(v.speechMode, 4u), attrs, valueAttrs, style);

    constexpr double motionX = 896;
    drawing::drawPanelFrame(motionX, 42, 246, 366, style);
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
        motionX, "SPEED", kMotionRateParamId, p.motionRateHz, 0.001, 2, 130,
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
        motionX, "SMOOTH", kSmoothParamId, p.motionSmooth, 0, 1, 312, attrs,
        valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "AZIMUTH", kCenterAzimuthParamId, p.centerAzimuthDeg, -180,
        180, 338, attrs, valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "ELEV", kCenterElevationParamId, p.centerElevationDeg, -90, 90,
        364, attrs, valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "DISTANCE", kCenterDistanceParamId, p.centerDistance, 0.15, 2,
        390, attrs, valueAttrs, style);

    drawing::drawPanelFrame(motionX, 420, 246, 152, style);
    drawing::drawPanelHeader("PHRASE", true, motionX, 420, 246, 21, attrs,
                             style);
    const drawing::Rect phraseHeader = drawing::makeRect(motionX, 420, 246, 21);
    drawing::drawToolboxHeaderActionButton(worldResetButtonRect(), phraseHeader,
                                           "RESET", attrs, style);
    drawing::drawToolboxHeaderActionButton(encodedLoadButtonRect(),
                                           phraseHeader, "LOAD", attrs, style);
    const bool hasBank =
        _plugin && static_cast<bool>(std::atomic_load_explicit(
                       &_plugin->voicebank, std::memory_order_acquire));
    const bool hasWorld =
        _plugin && static_cast<bool>(std::atomic_load_explicit(
                       &_plugin->worldSample, std::memory_order_acquire));
    if (phraseField) {
      phraseField->setVisible(true);
    }
    const std::string loadedName =
        _plugin ? loadVoxLoadedName(*_plugin) : std::string();
    std::string resolvedText;
    drawing::String sourceText = "LOAD WAV OR VOICEBANK FOLDER";
    if (hasBank) {
      const auto bank = std::atomic_load_explicit(&_plugin->voicebank,
                                                  std::memory_order_acquire);
      sourceText = drawing::stringFormat(
          "WORLD BANK  %@  %lu entries",
          drawing::stringFromUtf8(loadedName.empty() ? "loaded"
                                                     : loadedName.c_str()),
          bank ? static_cast<unsigned long>(bank->entries.size()) : 0ul);
      if (bank)
        resolvedText = resolvedVoicebankPhrase(*_plugin, *bank);
    } else if (hasWorld) {
      const auto world = std::atomic_load_explicit(&_plugin->worldSample,
                                                   std::memory_order_acquire);
      const double duration = world && world->sampleRate > 0
                                  ? static_cast<double>(world->samples.size()) /
                                        static_cast<double>(world->sampleRate)
                                  : 0.0;
      sourceText = drawing::stringFormat(
          "WORLD WAV  %@  %.2fs",
          drawing::stringFromUtf8(loadedName.empty() ? "loaded"
                                                     : loadedName.c_str()),
          duration);
    }
    drawing::drawAtPoint_withAttributes(
        sourceText, drawing::makePoint(motionX + 16, 488), valueAttrs);
    if (!resolvedText.empty()) {
      drawing::String aliases = drawing::stringFormat(
          "ALIASES  %@", drawing::stringFromUtf8(resolvedText.c_str()));
      drawing::drawAtPoint_withAttributes(
          aliases, drawing::makePoint(motionX + 16, 502), valueAttrs);
    }
    const uint32_t phraseEventCount =
        _plugin ? std::min<uint32_t>(_plugin->voxBankCompiledCount.load(
                                         std::memory_order_acquire),
                                     kVoxCompiledMaxFrames)
                : 0u;
    if (hasBank && phraseEventCount > 0u) {
      const uint32_t phraseEvent = std::min<uint32_t>(
          _plugin->guiVoxPhraseEvent.load(std::memory_order_relaxed),
          phraseEventCount - 1u);
      const float phraseProgress = std::clamp(
          _plugin->guiVoxPhraseProgress.load(std::memory_order_relaxed), 0.0f,
          1.0f);
      drawing::String timelineText = drawing::stringFormat(
          "VOICE 1  %u/%u", phraseEvent + 1u, phraseEventCount);
      drawing::drawAtPoint_withAttributes(
          timelineText, drawing::makePoint(motionX + 16, 550), valueAttrs);
      const drawing::Rect timeline =
          drawing::makeRect(motionX + 16, 564, 214, 3);
      drawing::setFill(style.strip);
      drawing::rectFill(timeline);
      if (phraseEventCount <= 32u) {
        const double step =
            timeline.size.width / static_cast<double>(phraseEventCount);
        drawing::setStroke(style.grid);
        for (uint32_t event = 1u; event < phraseEventCount; ++event) {
          drawing::Rect divider =
              drawing::makeRect(timeline.origin.x + step * event,
                                timeline.origin.y, 1, timeline.size.height);
          drawing::frameRect(divider);
        }
        drawing::setFill(style.fill);
        drawing::rectFill(drawing::makeRect(
            timeline.origin.x + step * phraseEvent, timeline.origin.y,
            std::max<double>(1.0, step * phraseProgress),
            timeline.size.height));
      } else {
        drawing::setFill(style.fill);
        const double absoluteProgress =
            (static_cast<double>(phraseEvent) + phraseProgress) /
            static_cast<double>(phraseEventCount);
        drawing::rectFill(drawing::makeRect(
            timeline.origin.x, timeline.origin.y,
            timeline.size.width * absoluteProgress, timeline.size.height));
      }
      drawing::setStroke(style.grid);
      drawing::frameRect(timeline);
    }

    drawing::drawPanelFrame(motionX, 584, 246, 236, style);
    drawing::drawPanelHeader("PITCH", true, motionX, 584, 246, 21, attrs,
                             style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "ROOT", kBaseNoteParamId, p.baseNote, 12, 96, 620, attrs,
        valueAttrs, style);
    drawMenuAtX_name_value_y_attrs_valueAttrs_style(
        motionX, "SCALE",
        drawing::stringFromUtf8(s3g::ambiVotScaleName(p.scale)), 646, attrs,
        valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "SPREAD", kPitchSpreadParamId, p.pitchSpread, 0, 2, 672, attrs,
        valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "TRANSPOSE", kWorldPitchParamId, v.worldPitchCents, -2400,
        2400, 698, attrs, valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "DEVIATION", kDetuneParamId, p.detune, 0, 1, 724, attrs,
        valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "GLIDE", kPortamentoParamId, v.portamento, 0, 1, 750, attrs,
        valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "VIBRATO", kVibratoDepthParamId, v.vibratoDepth, 0, 1, 776,
        attrs, valueAttrs, style);
    drawSliderAtX_name_param_value_min_max_y_attrs_valueAttrs_style(
        motionX, "VIB SPEED", kVibratoRateParamId, v.vibratoRateHz, 0.1, 12,
        802, attrs, valueAttrs, style);
  }

  void drawOpenMenu_style(drawing::Attrs attrs, const drawing::Style &style) {
    if (_openMenu <= 0 || _menuItemCount == 0u)
      return;
    if (_openMenu == kLyricCueChannelMenuId) {
      drawing::drawMidiChannelDropdownMenu(
          openMenuRect(), 18.0, static_cast<int>(view.lyric.cueChannel) - 1,
          _hoverMenuItem, attrs, style);
      return;
    }
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
    static drawing::String speechItems[] = {"TEXTURE", "SPEAK", "SING"};
    static drawing::String ensembleItems[] = {
        "INDIVIDUAL", "UNISON", "CHORALE", "CHORUS", "ROUND", "CLUSTER"};
    static drawing::String contourItems[] = {"ORIGINAL", "REDUCED", "FLAT",
                                             "QUANTIZED"};
    static drawing::String lyricModeItems[] = {"LOOP", "MIDI STEP", "MIDI CUE",
                                               "TRANSPORT", "AUTO"};
    static drawing::String phraseFormItems[] = {"DRIFT", "CALL / RESPONSE",
                                                "ROUND", "BLOOM", "PULSE"};
    static drawing::String phraseColorItems[] = {"OPEN", "DARK",   "LIQUID",
                                                 "AIR",  "BRIGHT", "MIXED"};
    static drawing::String factoryItems[] = {
        "VOX DEFAULT",   "SOLO SPEAK",  "CLOSE UNISON",    "SATB CHORALE",
        "DOUBLE CHOIR",  "WIDE CHORUS", "FOUR PART ROUND", "SEMITONE CLUSTER",
        "AIR CHOIR",     "DARK VOWELS", "FROZEN TEXTURE",  "ORBITING CHORUS",
        "FULL 3OA CHOIR"};
    drawing::String *items = modeItems;
    int selected = 0;
    if (_openMenu == 1)
      selected = static_cast<int>(static_cast<uint32_t>(view.params.mode));
    else if (_openMenu == 2) {
      items = waveItems;
      selected = static_cast<int>(static_cast<uint32_t>(view.params.preset));
    } else if (_openMenu == 3) {
      items = orderItems;
      selected = static_cast<int>(view.params.order - 1u);
    } else if (_openMenu == 4) {
      items = sceneItems;
      selected =
          static_cast<int>(static_cast<uint32_t>(view.params.motionScene));
    } else if (_openMenu == 5) {
      items = clockItems;
      selected =
          static_cast<int>(static_cast<uint32_t>(view.params.motionClock));
    } else if (_openMenu == 6) {
      items = scaleItems.data();
      selected = static_cast<int>(s3g::musicalScaleMenuIndexForValue(
          static_cast<uint32_t>(view.params.scale)));
    } else if (_openMenu == 7) {
      items = scoreModeItems;
      selected = static_cast<int>(static_cast<uint32_t>(view.params.scoreMode));
    } else if (_openMenu == 9) {
      items = speechItems;
      selected = static_cast<int>(static_cast<uint32_t>(view.vox.speechMode));
    } else if (_openMenu == 10) {
      items = ensembleItems;
      selected =
          static_cast<int>(static_cast<uint32_t>(view.vox.orchestration));
    } else if (_openMenu == 11) {
      items = contourItems;
      selected = static_cast<int>(static_cast<uint32_t>(view.vox.contourMode));
    } else if (_openMenu == 12) {
      items = factoryItems;
      selected = _plugin->factoryPresetIndex;
    } else if (_openMenu == 13) {
      items = lyricModeItems;
      selected = static_cast<int>(static_cast<uint32_t>(view.lyric.mode));
    } else if (_openMenu == 14) {
      items = phraseFormItems;
      selected =
          static_cast<int>(static_cast<uint32_t>(_plugin->generator.form));
    } else if (_openMenu == 15) {
      items = phraseColorItems;
      selected =
          static_cast<int>(static_cast<uint32_t>(_plugin->generator.color));
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

  void applyFactoryPreset(uint32_t index) {
    if (!_plugin || index >= kVoxFactoryPresetCount)
      return;
    const auto preset = voxFactoryPreset(index);
    auto snapshot = preset.snapshot;
    snapshot.params.outputGainDb = view.params.outputGainDb;
    if (!queueVoxPreset(*_plugin, snapshot)) {
      beep();
      return;
    }
    storeVoxPhrase(*_plugin, preset.phrase);
    storeVoxLyricsText(*_plugin, preset.phrase);
    rebuildVoxLyricScore(*_plugin);
    _selectedLyricCue = 0u;
    requestVoxLyricCue(*_plugin, 0u);
    if (phraseField) {
      setPhraseText(drawing::stringFromUtf8(preset.phrase));
    }
    if (lyricsEditor) {
      _updatingLyricsEditor = true;
      setLyricsText(drawing::stringFromUtf8(preset.phrase));
      _updatingLyricsEditor = false;
    }
    _plugin->factoryPresetIndex = static_cast<int32_t>(index);
    std::snprintf(_plugin->presetName, sizeof(_plugin->presetName), "%s",
                  preset.name);
    _selectedVoice = std::min<uint32_t>(
        _selectedVoice, std::max<uint32_t>(1u, snapshot.params.voices) - 1u);
    setNeedsDisplay(true);
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
    drawing::drawAtPoint_withAttributes("s3g AMBI ENCODER VOX",
                                        drawing::makePoint(18, 14), titleAttrs);
    drawing::drawEncoderPresetMenu(presetDisplayName(),
                                   drawing::encoderTitleBand(kGuiW, kGuiH),
                                   labelAttrs, valueAttrs, style);
    const drawing::Rect titleHeader = drawing::makeRect(0, 8, kGuiW, 21);
    drawing::drawHeaderActionButton(presetLoadButtonRect(), titleHeader, "LOAD",
                                    labelAttrs, style);
    drawing::drawHeaderActionButton(presetSaveButtonRect(), titleHeader, "SAVE",
                                    labelAttrs, style);
    drawing::drawHeaderActionButton(randomizeButtonRect(), titleHeader,
                                    "RANDOM", labelAttrs, style);
    drawing::String status = drawing::peakDbText(
        _plugin->outputPeak.load(std::memory_order_relaxed));
    drawing::drawRightStatus(status, kGuiW, 14, valueAttrs);

    const drawing::Rect left = leftPanelRect();
    drawing::drawPanelFrame(left.origin.x, left.origin.y, left.size.width,
                            left.size.height, style);
    drawing::drawPanelHeader("VOICE SPACE", true, left.origin.x, left.origin.y,
                             left.size.width, 21, labelAttrs, style);
    drawPageButtons_style(labelAttrs, style);
    setLyricsHidden(_leftPage != 3 || _showLyricGenerator || _openMenu == 12 ||
                    _openMenu == 14 || _openMenu == 15 ||
                    _openMenu == kLyricCueChannelMenuId);
    if (_leftPage == 0) {
      drawViewButtons_style(labelAttrs, style);
      drawField_attrs(leftContentRect(), valueAttrs);
    } else {
      drawLyricsPage_attrs_valueAttrs_style(leftContentRect(), labelAttrs,
                                            valueAttrs, style);
    }
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
      value = 1.0 + norm * static_cast<double>(s3g::kAmbiVoxMaxVoices - 1u);
      break;
    case kBaseNoteParamId:
      value = 12.0 + norm * 84.0;
      break;
    case kTuneParamId:
      value = -1200.0 + norm * 2400.0;
      break;
    case kWorldPitchParamId:
      value = -2400.0 + norm * 4800.0;
      break;
    case kDetuneParamId:
    case kHarmonicsParamId:
    case kSubharmonicsParamId:
    case kRaspParamId:
    case kBreathParamId:
    case kThroatParamId:
    case kDriveParamId:
    case kJitterParamId:
    case kVowelSpreadParamId:
    case kPitchScoopParamId:
    case kAttackShapeParamId:
    case kArticulationParamId:
    case kConsonantParamId:
    case kPlosiveParamId:
    case kSibilanceParamId:
    case kPhraseRateParamId:
    case kBendMacroParamId:
    case kCrushMacroParamId:
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
    case kWorldRateParamId:
    case kWorldLoopStartParamId:
    case kWorldLoopEndParamId:
    case kWorldVoiceSpreadParamId:
    case kWorldVoiceDeviationParamId:
    case kWorldFreezeParamId:
    case kWorldScrubParamId:
    case kWorldVoicingParamId:
    case kPvocTransientParamId:
    case kPortamentoParamId:
    case kVibratoDepthParamId:
    case kTransitionParamId:
    case kPhraseSpreadParamId:
    case kPopFilterParamId:
      value = norm;
      break;
    case kFormantMacroParamId:
    case kWorldAirColorParamId:
      value = -1.0 + norm * 2.0;
      break;
    case kPitchSpreadParamId:
      value = norm * 2.0;
      break;
    case kPvocStretchParamId:
      value = 0.25 * std::pow(16.0, norm);
      break;
    case kVibratoRateParamId:
      value = 0.1 * std::pow(120.0, norm);
      break;
    case kLyricCueBeatsParamId:
      value = 0.25 + norm * 63.75;
      break;
    case kLyricCueNoteParamId:
      value = norm * 127.0;
      break;
    case kLyricCueChannelParamId:
      value = 1.0 + norm * 15.0;
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
    applyParam(*_plugin, param, value);
    markPresetCustom();
  }

  void updateDraggedSlider(drawing::Point point) {
    if (_dragParam == CLAP_INVALID_ID)
      return;
    const drawing::Rect content = leftContentRect();
    const double trackX =
        _dragArea == 2
            ? 120.0
            : (_dragArea == 9 ? content.origin.x + kLyricsSliderTrackOffset
                              : (_dragArea == 7 ? 1004.0 : 738.0));
    const double trackWidth =
        (_dragArea == 2 || _dragArea == 9) ? kLyricsSliderTrackWidth : 82.0;
    setParam_fromNorm(_dragParam,
                      voxSliderNormForPoint(point.x, trackX, trackWidth));
    setNeedsDisplay(true);
  }

  void openMenu_count_x_y_width(int menu, uint32_t count, double x, double y,
                                double width) {
    _openMenu = menu;
    _menuItemCount = count;
    _hoverMenuItem = -1;
    _menuOrigin = drawing::makePoint(x, y + 18.0);
    _menuWidth = width;
    if (_leftPage == 3 && (menu == 12 || menu == 14 || menu == 15 ||
                           menu == kLyricCueChannelMenuId)) {
      setLyricsHidden(true);
    }
    setNeedsDisplay(true);
  }

  drawing::Rect openMenuRect() {
    const double itemHeight = 18.0;
    const uint32_t columns = _openMenu == 6
                                 ? 4u
                                 : (_openMenu == kLyricCueChannelMenuId
                                        ? drawing::kMidiChannelMenuColumns
                                        : 1u);
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
    if (_openMenu == kLyricCueChannelMenuId) {
      return drawing::midiChannelDropdownHitIndex(point, menuRect, 18.0);
    }
    return _openMenu == 6 ? drawing::multiColumnDropdownHitIndex(
                                point, menuRect, 18.0, _menuItemCount, 4u)
                          : drawing::dropdownHitIndex(point, menuRect, 18.0,
                                                      _menuItemCount);
  }

  void closeMenuAtPoint(drawing::Point point) {
    const int hit = openMenuHit(point);
    if (hit >= 0) {
      if (_openMenu == 12) {
        applyFactoryPreset(static_cast<uint32_t>(hit));
      } else {
        if (_openMenu == 1)
          applyParam(*_plugin, kModeParamId, hit);
        else if (_openMenu == 2)
          applyParam(*_plugin, kPresetParamId, hit);
        else if (_openMenu == 3)
          applyParam(*_plugin, kOrderParamId, hit + 1);
        else if (_openMenu == 4)
          applyParam(*_plugin, kMotionSceneParamId, hit);
        else if (_openMenu == 5)
          applyParam(*_plugin, kMotionClockParamId, hit);
        else if (_openMenu == 6) {
          applyParam(
              *_plugin, kScaleParamId,
              s3g::musicalScaleValueForMenuIndex(static_cast<uint32_t>(hit)));
        } else if (_openMenu == 7)
          applyParam(*_plugin, kScoreModeParamId, hit);
        else if (_openMenu == 9)
          applyParam(*_plugin, kSpeechModeParamId, hit);
        else if (_openMenu == 10)
          applyParam(*_plugin, kOrchestrationParamId, hit);
        else if (_openMenu == 11)
          applyParam(*_plugin, kContourModeParamId, hit);
        else if (_openMenu == 13)
          applyParam(*_plugin, kLyricModeParamId, hit);
        else if (_openMenu == kLyricCueChannelMenuId)
          applyParam(*_plugin, kLyricCueChannelParamId, hit + 1);
        else if (_openMenu == 14) {
          _plugin->generator.form = static_cast<VoxPhraseForm>(hit);
          clampVoxPhraseGeneratorRecipe(_plugin->generator);
        } else if (_openMenu == 15) {
          _plugin->generator.color = static_cast<VoxPhraseColor>(hit);
          clampVoxPhraseGeneratorRecipe(_plugin->generator);
        } else if (_openMenu == 8) {
          auto score = loadScore(*_plugin);
          const uint32_t node =
              std::min<uint32_t>(_selectedScoreNode, score.nodeCount - 1u);
          score.nodes[node].curve = static_cast<s3g::AmbiVotScoreCurve>(hit);
          storeScore(*_plugin, score);
        }
        markPresetCustom();
      }
    }
    _openMenu = 0;
    _hoverMenuItem = -1;
    _menuItemCount = 0;
    _menuWidth = 124.0;
    setLyricsHidden(_leftPage != 3 || _showLyricGenerator);
    setNeedsDisplay(true);
  }

  int hitVoiceAtPoint(drawing::Point point) {
    const drawing::Rect rect = leftContentRect();
    const uint32_t voices =
        std::clamp<uint32_t>(view.params.voices, 1u, s3g::kAmbiVoxMaxVoices);
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
        std::clamp<uint32_t>(view.params.voices, 1u, s3g::kAmbiVoxMaxVoices);
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
    storeScore(*_plugin, score);
    markPresetCustom();
    setNeedsDisplay(true);
  }

  void mouseDown(drawing::Event *event) {
    if (!_plugin)
      return;
    const drawing::Point point =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    if (_openMenu > 0) {
      closeMenuAtPoint(point);
      return;
    }
    if (drawing::contains(point, presetMenuRect())) {
      const drawing::Rect rect = presetMenuRect();
      openMenu_count_x_y_width(12, kVoxFactoryPresetCount, rect.origin.x,
                               rect.origin.y, rect.size.width);
      return;
    }
    if (drawing::contains(point, presetLoadButtonRect())) {
      loadUserPreset();
      return;
    }
    if (drawing::contains(point, presetSaveButtonRect())) {
      saveUserPreset();
      return;
    }
    if (drawing::contains(point, randomizeButtonRect())) {
      auto randomUnit = [] {
        return static_cast<double>(drawing::randomBits()) / 4294967295.0;
      };
      applyParam(*_plugin, kModeParamId, drawing::randomBounded(3u));
      applyParam(*_plugin, kBaseNoteParamId, 30.0 + randomUnit() * 34.0);
      applyParam(*_plugin, kTuneParamId, -28.0 + randomUnit() * 56.0);
      applyParam(*_plugin, kVectorXParamId, randomUnit());
      applyParam(*_plugin, kVectorYParamId, randomUnit());
      applyParam(*_plugin, kScanParamId, randomUnit());
      applyParam(*_plugin, kMorphParamId, 0.20 + randomUnit() * 0.80);
      applyParam(*_plugin, kDetuneParamId, randomUnit() * 0.36);
      applyParam(*_plugin, kSpreadParamId, 0.18 + randomUnit() * 0.76);
      applyParam(*_plugin, kMotionSceneParamId, drawing::randomBounded(5u));
      applyParam(*_plugin, kMotionAmountParamId, 0.16 + randomUnit() * 0.76);
      applyParam(*_plugin, kCoherenceParamId, 0.22 + randomUnit() * 0.72);
      applyParam(*_plugin, kChaosParamId, randomUnit() * 0.68);
      applyParam(*_plugin, kRaspParamId, randomUnit() * 0.72);
      applyParam(*_plugin, kBreathParamId, randomUnit() * 0.72);
      applyParam(*_plugin, kThroatParamId, randomUnit() * 0.72);
      applyParam(*_plugin, kJitterParamId, randomUnit() * 0.46);
      _plugin->factoryPresetIndex = -1;
      std::snprintf(_plugin->presetName, sizeof(_plugin->presetName), "%s",
                    "RANDOM");
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, encodedLoadButtonRect())) {
      loadEncodedPhrase();
      return;
    }
    if (drawing::contains(point, worldResetButtonRect())) {
      _plugin->sourceResetPending.store(true, std::memory_order_release);
      setNeedsDisplay(true);
      return;
    }
    static constexpr int pages[] = {0, 3};
    for (int i = 0; i < 2; ++i) {
      if (!drawing::contains(point, pageButtonRect(i)))
        continue;
      _leftPage = pages[i];
      setLyricsHidden(_leftPage != 3 || _showLyricGenerator);
      storeViewState();
      setNeedsDisplay(true);
      return;
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
        if (shiftDown) {
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
    } else if (_leftPage == 3) {
      for (int index = 0; index < 2; ++index) {
        if (!drawing::contains(point, lyricEditorModeButtonRect(index)))
          continue;
        _showLyricGenerator = index == 1;
        setLyricsHidden(_showLyricGenerator);
        setNeedsDisplay(true);
        return;
      }
      const drawing::Rect content = leftContentRect();
      if (_showLyricGenerator) {
        if (drawing::contains(point, drawing::makeRect(content.origin.x + 126.0,
                                                       content.origin.y + 47.0,
                                                       180.0, 18.0))) {
          openMenu_count_x_y_width(14, 5, content.origin.x + 126.0,
                                   content.origin.y + 48.0, 180.0);
          return;
        }
        if (drawing::contains(point, drawing::makeRect(content.origin.x + 126.0,
                                                       content.origin.y + 75.0,
                                                       180.0, 18.0))) {
          openMenu_count_x_y_width(15, 6, content.origin.x + 126.0,
                                   content.origin.y + 76.0, 180.0);
          return;
        }
        static constexpr double generatorSliderY[]{112.0, 140.0, 168.0, 196.0,
                                                   224.0, 252.0, 280.0};
        for (int index = 0; index < 7; ++index) {
          if (!drawing::contains(
                  point, drawing::makeRect(content.origin.x + 102.0,
                                           content.origin.y +
                                               generatorSliderY[index] - 8.0,
                                           404.0, 24.0)))
            continue;
          _generatorDrag = index + 1;
          updateGeneratorDragAtPoint(point);
          return;
        }
        for (int index = 0; index < 3; ++index) {
          if (!drawing::contains(point, generatorCommandButtonRect(index)))
            continue;
          if (index == 0) {
            _plugin->generator.generation = 0u;
            installGeneratedLyricsAppending(false);
          } else if (index == 1) {
            ++_plugin->generator.generation;
            installGeneratedLyricsAppending(false);
          } else {
            ++_plugin->generator.generation;
            installGeneratedLyricsAppending(true);
          }
          return;
        }
      }
      const auto cues = splitVoxLyricCues(loadVoxLyrics(*_plugin));
      for (uint32_t cue = 0u; cue < cues.size(); ++cue) {
        if (!drawing::contains(point, lyricCueRect(cue)))
          continue;
        _selectedLyricCue = cue;
        requestVoxLyricCue(*_plugin, cue);
        setPhraseText(drawing::stringFromUtf8(cues[cue].c_str()));
        setNeedsDisplay(true);
        return;
      }
      const double controlsY = content.origin.y + 618.0;
      if (drawing::contains(point, drawing::makeRect(content.origin.x + 110,
                                                     controlsY - 1, 150, 17))) {
        openMenu_count_x_y_width(13, 5, content.origin.x + 110, controlsY, 150);
        return;
      }
      if (drawing::contains(
              point,
              drawing::makeRect(content.origin.x + kLyricsSliderTrackOffset,
                                controlsY + 83, kLyricsSliderTrackWidth, 17))) {
        openMenu_count_x_y_width(kLyricCueChannelMenuId,
                                 drawing::kMidiChannelMenuItemCount,
                                 content.origin.x + kLyricsSliderTrackOffset,
                                 controlsY + 84, kLyricsSliderTrackWidth);
        return;
      }
      struct LyricSliderHit {
        clap_id param;
        double y;
      };
      const LyricSliderHit lyricSliders[]{
          {kLyricCueBeatsParamId, controlsY + 28},
          {kLyricCueNoteParamId, controlsY + 56},
      };
      for (const auto &slider : lyricSliders) {
        if (!drawing::contains(point, drawing::makeRect(content.origin.x + 102,
                                                        slider.y - 8, 404, 24)))
          continue;
        if (event->clickCount() >= 2) {
          applyParam(*_plugin, slider.param,
                     defaultValueForParam(slider.param));
          markPresetCustom();
        } else {
          _dragParam = slider.param;
          _dragArea = 9;
          updateDraggedSlider(point);
        }
        setNeedsDisplay(true);
        return;
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
        {9, 3, 738, 222, 124},  {4, 5, 1004, 78, 124},
        {5, 2, 1004, 104, 124}, {6, s3g::kMusicalScaleCount, 1004, 646, 124},
    };
    for (const auto &menu : menus) {
      if (drawing::contains(
              point, drawing::makeRect(menu.x, menu.y - 1, menu.width, 17))) {
        openMenu_count_x_y_width(menu.menu, menu.count, menu.x, menu.y,
                                 menu.width);
        return;
      }
    }
    const MenuHit worldMenus[]{
        {10, 6, 738, voxWorldRowY(view.vox.speechMode, 0u), 124},
        {11, 4, 738, voxWorldRowY(view.vox.speechMode, 1u), 124},
    };
    for (const auto &menu : worldMenus) {
      if (drawing::contains(
              point, drawing::makeRect(menu.x, menu.y - 1, menu.width, 17))) {
        openMenu_count_x_y_width(menu.menu, menu.count, menu.x, menu.y,
                                 menu.width);
        return;
      }
    }
    struct SliderHit {
      clap_id param;
      double x;
      double y;
      int area;
    };
    static constexpr SliderHit commonSliders[]{
        {kOutputParamId, 638, 78, 1},
        {kPopFilterParamId, 638, 248, 1},
        {kVoicesParamId, 638, kVoiceCountRowY, 1},
        {kMotionRateParamId, 904, 130, 7},
        {kSyncDivisionParamId, 904, 156, 7},
        {kMotionAmountParamId, 904, 182, 7},
        {kSpreadParamId, 904, 208, 7},
        {kCoherenceParamId, 904, 234, 7},
        {kChaosParamId, 904, 260, 7},
        {kLinkParamId, 904, 286, 7},
        {kSmoothParamId, 904, 312, 7},
        {kCenterAzimuthParamId, 904, 338, 7},
        {kCenterElevationParamId, 904, 364, 7},
        {kCenterDistanceParamId, 904, 390, 7},
        {kBaseNoteParamId, 904, 620, 7},
        {kPitchSpreadParamId, 904, 672, 7},
        {kWorldPitchParamId, 904, 698, 7},
        {kDetuneParamId, 904, 724, 7},
        {kPortamentoParamId, 904, 750, 7},
        {kVibratoDepthParamId, 904, 776, 7},
        {kVibratoRateParamId, 904, 802, 7},
    };
    static constexpr SliderHit phraseSliders[]{
        {kPhraseRateParamId, 638, 314, 1},
        {kPvocStretchParamId, 638, 340, 1},
        {kPvocTransientParamId, 638, 366, 1},
        {kTransitionParamId, 638, 392, 1},
        {kPhraseSpreadParamId, 638, 418, 1},
        {kWorldVoiceSpreadParamId, 638, 444, 1},
        {kWorldVoiceDeviationParamId, 638, 470, 1},
    };
    static constexpr SliderHit textureSliders[]{
        {kWorldRateParamId, 638, 314, 1},
        {kWorldLoopStartParamId, 638, 340, 1},
        {kWorldLoopEndParamId, 638, 366, 1},
        {kWorldFreezeParamId, 638, 392, 1},
        {kWorldScrubParamId, 638, 418, 1},
        {kPhraseSpreadParamId, 638, 444, 1},
        {kPvocStretchParamId, 638, 470, 1},
        {kPvocTransientParamId, 638, 496, 1},
        {kWorldVoiceSpreadParamId, 638, 522, 1},
        {kWorldVoiceDeviationParamId, 638, 548, 1},
    };
    const auto hitSlider = [&](const SliderHit &slider) {
      if (!drawing::contains(
              point, drawing::makeRect(slider.x, slider.y - 8, 232, 24)))
        return false;
      if (event->clickCount() >= 2) {
        applyParam(*_plugin, slider.param, defaultValueForParam(slider.param));
        markPresetCustom();
      } else {
        _dragParam = slider.param;
        _dragArea = slider.area;
        updateDraggedSlider(point);
      }
      return true;
    };
    for (const auto &slider : commonSliders) {
      if (hitSlider(slider))
        return;
    }
    const SliderHit worldSliders[]{
        {kFormantMacroParamId, 638, voxWorldRowY(view.vox.speechMode, 2u), 1},
        {kWorldVoicingParamId, 638, voxWorldRowY(view.vox.speechMode, 3u), 1},
        {kWorldAirColorParamId, 638, voxWorldRowY(view.vox.speechMode, 4u), 1},
    };
    for (const auto &slider : worldSliders) {
      if (hitSlider(slider))
        return;
    }
    if (view.vox.speechMode == VoxSpeechMode::Texture) {
      for (const auto &slider : textureSliders) {
        if (hitSlider(slider))
          return;
      }
    } else {
      for (const auto &slider : phraseSliders) {
        if (hitSlider(slider))
          return;
      }
    }
  }

  void mouseDragged(drawing::Event *event) {
    if (!_plugin)
      return;
    const drawing::Point point =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    if (_generatorDrag > 0) {
      updateGeneratorDragAtPoint(point);
      return;
    }
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
    updateDraggedSlider(point);
  }

  void mouseUp(drawing::Event *event) {
    (void)event;
    _dragParam = CLAP_INVALID_ID;
    _dragArea = 0;
    _generatorDrag = 0;
    _scoreDragLane = -1;
    _dragView = false;
  }

  void mouseMoved(drawing::Event *event) {
    if (_openMenu <= 0 || _menuItemCount == 0u)
      return;
    const drawing::Point point =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    const int next = openMenuHit(point);
    if (next != _hoverMenuItem) {
      _hoverMenuItem = next;
      setNeedsDisplay(true);
    }
  }
};
} // namespace complex_processor_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new complex_processor_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
