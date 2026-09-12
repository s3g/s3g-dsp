// Literal Cocoa adaptation: original geometry, paths, draw order and hit maps.
namespace score_encoder_canvas {
namespace drawing = s3g::portable_gui::resonator_drawing;
namespace foundation = s3g::portable_gui::foundation;
using Kind = s3g::clap_gui::ParamEventKind;
// GUI-only aggregate that maps the stable Order and Output Mode parameters to
// the same FORMAT menu used by the Membrane Kick family member.
constexpr clap_id kFormatMenuId = 0x7ffffff0u;

struct AcidUiRow {
  clap_id id;
  const char *label;
  uint32_t page;
  double y;
};

constexpr double kPatternX = 16.0;
constexpr double kPatternY = 46.0;
constexpr double kPatternWidth = 888.0;
constexpr double kPatternHeight = 214.0;
constexpr double kStepStartX = 24.0;
constexpr double kStepPitch = 54.5;
constexpr double kStepWidth = 51.5;
constexpr double kControlFirstRowY = 304.0;
constexpr double kControlRowPitch = 26.0;
constexpr double kOutputSectionDividerY = 518.0;

constexpr double controlRowY(uint32_t row) {
  return kControlFirstRowY + kControlRowPitch * row;
}

static_assert(kControlRowPitch >= 22.0 && kControlRowPitch <= 26.0,
              "Acid toolbox rows must follow the standard GUI pitch");
static_assert(controlRowY(7u) + 10.0 < kOutputSectionDividerY,
              "The densest Acid page must clear the output section divider");

// The three retired listener parameters stay hidden. Order and output mode are
// represented by one persistent FORMAT menu, and output level is persistent.
constexpr uint32_t kVisibleParamCount = kBaseParamCount - 6u;
constexpr std::array<AcidUiRow, kVisibleParamCount> kUiRows{{
    {kTempoParamId, "TEMPO", 0u, controlRowY(0u)},
    {kTransportSyncParamId, "CLOCK", 0u, controlRowY(1u)},
    {kDivisionParamId, "DIVIDE", 0u, controlRowY(2u)},
    {kLengthParamId, "LENGTH", 0u, controlRowY(3u)},
    {kRootParamId, "ROOT", 0u, controlRowY(4u)},
    {kScaleParamId, "SCALE", 0u, controlRowY(5u)},
    {kGateLengthParamId, "GATE", 0u, controlRowY(6u)},

    {kWaveParamId, "WAVE", 1u, controlRowY(0u)},
    {kPulseWidthParamId, "PULSE", 1u, controlRowY(1u)},
    {kSubOctaveParamId, "SUB OCT", 1u, controlRowY(2u)},
    {kSubLevelParamId, "SUB LEVEL", 1u, controlRowY(3u)},
    {kSlideParamId, "SLIDE", 1u, controlRowY(4u)},

    {kCutoffParamId, "CUTOFF", 2u, controlRowY(0u)},
    {kResonanceParamId, "RESO", 2u, controlRowY(1u)},
    {kFilterEnvelopeParamId, "ENV", 2u, controlRowY(2u)},
    {kFilterDecayParamId, "DECAY", 2u, controlRowY(3u)},
    {kAccentParamId, "ACCENT", 2u, controlRowY(4u)},
    {kDriveParamId, "DRIVE", 2u, controlRowY(5u)},
    {kDriveCircuitParamId, "CIRCUIT", 2u, controlRowY(6u)},
    {kDriveMixParamId, "DRY/WET", 2u, controlRowY(7u)},

    {kCenterAzimuthParamId, "AZIM", 3u, controlRowY(0u)},
    {kPathTurnsParamId, "TURNS", 3u, controlRowY(1u)},
    {kElevationSpreadParamId, "ELEV", 3u, controlRowY(2u)},
    {kSpatialSpreadParamId, "SPREAD", 3u, controlRowY(3u)},
    {kEdgeLeadParamId, "EDGE", 3u, controlRowY(4u)},
    {kWakeAmountParamId, "WAKE", 3u, controlRowY(5u)},
    {kWakeTimeParamId, "WAKE T", 3u, controlRowY(6u)},
}};

constexpr AcidUiRow kFormatRow{kFormatMenuId, "FORMAT", 4u, 535.0};

constexpr AcidUiRow kOutputLevelRow{kOutputParamId, "OUT LEVEL", 4u, 568.0};

drawing::Rect stepCellRect(uint32_t step) {
  return drawing::makeRect(kStepStartX + kStepPitch * step, 74.0, kStepWidth,
                           178.0);
}

drawing::Rect stepNoteRect(uint32_t step) {
  const drawing::Rect cell = stepCellRect(step);
  return drawing::makeRect(cell.origin.x + 4.0, 96.0, cell.size.width - 8.0,
                           104.0);
}

drawing::Rect stepToggleRect(uint32_t step, StepParamKind kind) {
  const drawing::Rect cell = stepCellRect(step);
  const double width = (cell.size.width - 8.0) / 3.0;
  const uint32_t toggle = static_cast<uint32_t>(kind) - 1u;
  return drawing::makeRect(cell.origin.x + 4.0 + width * toggle, 226.0,
                           width - 1.0, 16.0);
}

drawing::Rect controlPageButtonRect(uint32_t page) {
  return drawing::makeRect(106.0 + page * 48.0, 272.0, 44.0, 14.0);
}

drawing::Rect spatialPathFieldRect(uint32_t view) {
  return drawing::makeRect(view == 0u ? 319.0 : 613.0, 302.0, 282.0, 282.0);
}

drawing::Rect spatialPathButtonRect(uint32_t button) {
  return drawing::makeRect(730.0 + button * 82.0, 272.0, 76.0, 14.0);
}

drawing::Point spatialPointScreenPosition(const Plugin &plugin, uint32_t step,
                                          drawing::Rect rect, uint32_t view) {
  const clap_id base = kSpatialParamBase + step * kSpatialParamStride;
  const double scale = std::min(rect.size.width, rect.size.height) * 0.42;
  const double x = static_cast<double>(paramValue(plugin, base));
  const double y = static_cast<double>(paramValue(plugin, base + 1u));
  const double z = static_cast<double>(paramValue(plugin, base + 2u));
  if (view == 0u) {
    // Top view: +X/front is up, +Y/listener-left is screen-left.
    return drawing::makePoint(drawing::midX(rect) - y * scale,
                              drawing::midY(rect) - x * scale);
  }
  // Side/elevation view: listener-left is screen-left and height is up.
  return drawing::makePoint(drawing::midX(rect) - y * scale,
                            drawing::midY(rect) - z * scale);
}

uint32_t controlPageForRow(const AcidUiRow &row) { return row.page; }

const ParamSpec *paramSpec(clap_id id) {
  if (id < kOrderParamId || id > kOutputModeParamId)
    return nullptr;
  return &kParamSpecs[id - kOrderParamId];
}

bool logarithmicUiParam(clap_id id) {
  return id == kCutoffParamId || id == kFilterDecayParamId ||
         id == kSlideParamId || id == kWakeTimeParamId ||
         id == kListenMemoryParamId;
}

double uiNormalizedValue(clap_id id, double value) {
  const auto *spec = paramSpec(id);
  if (!spec)
    return 0.0;
  if (id == kCenterAzimuthParamId)
    return s3g::aedAzimuthSliderNorm(static_cast<float>(value));
  if (logarithmicUiParam(id) && spec->minimum > 0.0) {
    return std::clamp(std::log(value / spec->minimum) /
                          std::log(spec->maximum / spec->minimum),
                      0.0, 1.0);
  }
  return std::clamp((value - spec->minimum) /
                        std::max(1.0e-12, spec->maximum - spec->minimum),
                    0.0, 1.0);
}

double uiValueFromNormalized(clap_id id, double normalized) {
  const auto *spec = paramSpec(id);
  if (!spec)
    return 0.0;
  normalized = std::clamp(normalized, 0.0, 1.0);
  if (id == kCenterAzimuthParamId)
    return s3g::aedAzimuthFromSliderNorm(static_cast<float>(normalized));
  const double value =
      logarithmicUiParam(id) && spec->minimum > 0.0
          ? spec->minimum * std::pow(spec->maximum / spec->minimum, normalized)
          : spec->minimum + (spec->maximum - spec->minimum) * normalized;
  return clampParamValue(id, value);
}

drawing::String midiNoteName(int note) {
  static constexpr const char *names[]{
      "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
  };
  note = std::clamp(note, 0, 127);
  return drawing::stringFormat("%s%d", names[note % 12], note / 12 - 1);
}

void drawCenteredText(drawing::String text, drawing::Rect rect,
                      drawing::Attrs attrs) {
  const drawing::Size size = drawing::sizeWithAttributes(text, attrs);
  drawing::drawAtPoint_withAttributes(
      text,
      drawing::makePoint(rect.origin.x + (rect.size.width - size.width) * 0.5,
                         rect.origin.y +
                             (rect.size.height - size.height) * 0.5),
      attrs);
}

drawing::Rect patternPresetButtonRect() {
  const auto band = drawing::encoderTitleBand(kGuiWidth, kGuiHeight);
  return drawing::cocoaRect(band.presetMenu);
}

drawing::Rect patternPresetMenuRect() {
  const drawing::Rect anchor = patternPresetButtonRect();
  return drawing::makeRect(anchor.origin.x, drawing::maxY(anchor) + 2.0,
                           anchor.size.width,
                           18.0 * s3g::kAmbiAcidPatternPresets.size());
}

constexpr double kScaleMenuItemHeight = 18.0;
constexpr uint32_t kScaleMenuColumns = 4u;

drawing::Rect musicalScaleMenuRect() {
  const uint32_t rows =
      drawing::multiColumnMenuRows(s3g::kMusicalScaleCount, kScaleMenuColumns);
  return drawing::makeRect(124.0, 1.0, 720.0,
                           kScaleMenuItemHeight * static_cast<double>(rows));
}

drawing::String const *musicalScaleMenuItems() {
  static const std::array<drawing::String, s3g::kMusicalScaleCount> items = [] {
    std::array<drawing::String, s3g::kMusicalScaleCount> result{};
    for (uint32_t index = 0u; index < result.size(); ++index) {
      const uint32_t scale = s3g::musicalScaleValueForMenuIndex(index);
      result[index] =
          drawing::stringFromUtf8(s3g::musicalScaleDefinition(scale).name);
    }
    return result;
  }();
  return items.data();
}

bool discreteUiMenuParam(clap_id id) {
  return id == kTransportSyncParamId || id == kDivisionParamId ||
         id == kSubOctaveParamId || id == kDriveCircuitParamId;
}

uint32_t discreteUiMenuItemCount(clap_id id) {
  if (id == kFormatMenuId)
    return 4u;
  if (id == kTransportSyncParamId)
    return 2u;
  if (id == kSubOctaveParamId)
    return 3u;
  if (id == kDivisionParamId)
    return 8u;
  if (id == kDriveCircuitParamId)
    return s3g::kAmbiAcidDriveCircuitCount;
  return 0u;
}

double discreteUiMenuValue(clap_id id, uint32_t index) {
  if (id == kDivisionParamId)
    return index + 1u;
  if (id == kSubOctaveParamId)
    return static_cast<int32_t>(index) - 2;
  return index;
}

int discreteUiMenuSelectedIndex(clap_id id, double value) {
  const int rounded = static_cast<int>(std::lround(value));
  if (id == kDivisionParamId)
    return rounded - 1;
  if (id == kSubOctaveParamId)
    return rounded + 2;
  return rounded;
}

drawing::String discreteUiMenuItem(clap_id id, uint32_t index) {
  if (id == kFormatMenuId) {
    static drawing::String const items[]{"1OA / 4CH", "2OA / 9CH", "3OA / 16CH",
                                         "MONO 1+2"};
    return items[std::min<uint32_t>(index, 3u)];
  }
  if (id == kTransportSyncParamId)
    return index == 0u ? "INT" : "HOST";
  if (id == kDivisionParamId)
    return drawing::stringFormat("%u", index + 1u);
  if (id == kSubOctaveParamId)
    return drawing::stringFormat("%+d OCT", static_cast<int>(index) - 2);
  if (id == kDriveCircuitParamId) {
    return drawing::stringFromUtf8(s3g::ambiAcidDriveCircuitName(
        static_cast<s3g::AmbiAcidDriveCircuit>(index)));
  }
  return "";
}

int formatMenuSelectedIndex(const Plugin &plugin) {
  if (paramValue(plugin, kOutputModeParamId) >= 0.5)
    return 3;
  return std::clamp(
      static_cast<int>(std::lround(paramValue(plugin, kOrderParamId))) - 1, 0,
      2);
}

drawing::String formatMenuSelectedText(const Plugin &plugin) {
  const int index = formatMenuSelectedIndex(plugin);
  return index == 3 ? "MONO 1+2" : drawing::stringFormat("%dOA", index + 1);
}

double discreteUiMenuRowY(clap_id id) {
  if (id == kFormatMenuId)
    return kFormatRow.y;
  for (const auto &row : kUiRows) {
    if (row.id == id)
      return row.y;
  }
  return 0.0;
}

drawing::Rect discreteUiMenuRect(clap_id id) {
  const double height = 18.0 * discreteUiMenuItemCount(id);
  return drawing::makeRect(124.0, discreteUiMenuRowY(id) - height, 156.0,
                           height);
}

int publishedPatternPresetIndex(const Plugin &plugin) {
  for (uint32_t preset = 0u; preset < s3g::kAmbiAcidPatternPresets.size();
       ++preset) {
    bool matches = true;
    for (uint32_t step = 0u; step < s3g::kAmbiAcidStepCount && matches;
         ++step) {
      const auto &expected = s3g::kAmbiAcidPatternPresets[preset].steps[step];
      const clap_id base = kStepParamBase + step * kStepParamStride;
      matches =
          std::lround(paramValue(plugin, base)) == expected.semitoneOffset &&
          (paramValue(plugin, base + 1u) >= 0.5) == expected.gate &&
          (paramValue(plugin, base + 2u) >= 0.5) == expected.accent &&
          (paramValue(plugin, base + 3u) >= 0.5) == expected.slide;
    }
    if (matches)
      return static_cast<int>(preset);
  }
  return -1;
}

class Editor final : public s3g::portable_gui::routing::Canvas {
public:
  Plugin *_plugin;
  int _dragParam;
  int _dragStep;
  int _dragSpatialView;
  int _controlPage;
  int _selectedStep;
  int _presetIndex;
  bool _presetMenuOpen;
  int _presetHover;
  bool _scaleMenuOpen;
  int _scaleMenuHover;
  clap_id _parameterMenuId;
  int _parameterMenuHover;
  char _patternName[64];
  char _titlePresetName[64];

#include "s3g_score_encoder_editor.inc"
  Editor(Plugin &instance) : Canvas(kGuiWidth, kGuiHeight) {
    auto *plugin = &instance;

    _plugin = plugin;
    _dragParam = -1;
    _dragStep = -1;
    _dragSpatialView = -1;
    _controlPage = 0;
    _selectedStep = 0;
    _presetIndex = publishedPatternPresetIndex(*static_cast<Plugin *>(plugin));
    _presetMenuOpen = false;
    _presetHover = -1;
    _scaleMenuOpen = false;
    _scaleMenuHover = -1;
    _parameterMenuId = CLAP_INVALID_ID;
    _parameterMenuHover = -1;
    std::snprintf(
        _patternName, sizeof(_patternName), "%s",
        _presetIndex >= 0
            ? s3g::kAmbiAcidPatternPresets[static_cast<uint32_t>(_presetIndex)]
                  .name
            : "CUSTOM");
    std::snprintf(
        _titlePresetName, sizeof(_titlePresetName), "%s",
        _presetIndex >= 0
            ? s3g::kAmbiAcidPatternPresets[static_cast<uint32_t>(_presetIndex)]
                  .name
            : "CUSTOM");

    setWantsFocus(true);
    service();
  }
  ~Editor() override { stopRefresh(); }
  void service() override {

    auto *p = static_cast<Plugin *>(_plugin);
    if (p && !_presetMenuOpen && !_scaleMenuOpen &&
        _parameterMenuId == CLAP_INVALID_ID && _dragStep < 0) {
      const int detected = publishedPatternPresetIndex(*p);
      if (detected >= 0 && detected != _presetIndex) {
        _presetIndex = detected;
        std::snprintf(
            _patternName, sizeof(_patternName), "%s",
            s3g::kAmbiAcidPatternPresets[static_cast<uint32_t>(detected)].name);
      } else if (detected < 0 && _presetIndex >= 0) {
        markCustomPattern();
      }
    }
    setNeedsDisplay(true);
  }
  int selectedStep() { return _selectedStep; }

  int controlPage() { return _controlPage; }

  int presetIndex() { return _presetIndex; }

  bool presetMenuOpen() { return _presetMenuOpen; }

  bool scaleMenuOpen() { return _scaleMenuOpen; }

  bool circuitMenuOpen() { return _parameterMenuId == kDriveCircuitParamId; }

  bool parameterMenuOpen() { return _parameterMenuId != CLAP_INVALID_ID; }

  clap_id parameterMenuId() { return _parameterMenuId; }

  void markCustomPattern() {
    _presetIndex = -1;
    std::snprintf(_patternName, sizeof(_patternName), "%s", "CUSTOM");
    markCustomState();
  }

  void markCustomState() {
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CUSTOM");
  }

  void applyPatternPreset(int index) {
    auto *p = static_cast<Plugin *>(_plugin);
    if (!p)
      return;
    index = std::clamp(
        index, 0, static_cast<int>(s3g::kAmbiAcidPatternPresets.size() - 1u));
    if (!queueGuiPattern(
            *p,
            s3g::kAmbiAcidPatternPresets[static_cast<uint32_t>(index)].steps)) {
      beep();
      return;
    }
    if (!queueGuiSpatialPath(*p, s3g::kAmbiAcidDefaultSpatialPath)) {
      beep();
      return;
    }
    singleEdit(kScaleParamId, 0.0);
    _presetIndex = index;
    std::snprintf(
        _patternName, sizeof(_patternName), "%s",
        s3g::kAmbiAcidPatternPresets[static_cast<uint32_t>(index)].name);
    std::snprintf(
        _titlePresetName, sizeof(_titlePresetName), "%s",
        s3g::kAmbiAcidPatternPresets[static_cast<uint32_t>(index)].name);
    setNeedsDisplay(true);
  }

  void randomizePattern() {
    auto *p = static_cast<Plugin *>(_plugin);
    if (!p)
      return;
    static constexpr int octaves[]{-12, 0, 0, 0, 0, 12};
    const uint32_t scaleValue =
        static_cast<uint32_t>(std::lround(paramValue(*p, kScaleParamId)));
    const auto &scale = s3g::musicalScaleDefinition(scaleValue);
    std::array<s3g::AmbiAcidStep, s3g::kAmbiAcidStepCount> pattern{};
    for (uint32_t step = 0u; step < pattern.size(); ++step) {
      const int degree = scale.semitones[drawing::randomBounded(scale.size)];
      const int octave = octaves[drawing::randomBounded(
          static_cast<uint32_t>(std::size(octaves)))];
      pattern[step].semitoneOffset = std::clamp(degree + octave, -24, 24);
      pattern[step].gate = step == 0u || drawing::randomBounded(100u) < 82u;
      pattern[step].accent =
          pattern[step].gate &&
          (step % 4u == 0u || drawing::randomBounded(100u) < 22u);
    }
    pattern[0u].semitoneOffset = 0;
    pattern[0u].gate = true;
    pattern[0u].accent = true;
    for (uint32_t step = 0u; step < pattern.size(); ++step) {
      const uint32_t next = (step + 1u) % pattern.size();
      pattern[step].slide = pattern[step].gate && pattern[next].gate &&
                            drawing::randomBounded(100u) < 30u;
    }
    if (!queueGuiPattern(*p, pattern)) {
      beep();
      return;
    }
    randomizeSpatialPath();
    _presetIndex = -1;
    std::snprintf(_patternName, sizeof(_patternName), "%s", "RANDOM");
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "RANDOM");
    setNeedsDisplay(true);
  }

  void resetSpatialPath() {
    auto *p = static_cast<Plugin *>(_plugin);
    if (!p || !queueGuiSpatialPath(*p, s3g::kAmbiAcidDefaultSpatialPath)) {
      beep();
      return;
    }
    markCustomState();
    setNeedsDisplay(true);
  }

  void randomizeSpatialPath() {
    auto *p = static_cast<Plugin *>(_plugin);
    if (!p)
      return;
    std::array<s3g::AmbiAcidSpatialPoint, s3g::kAmbiAcidStepCount> path{};
    const float direction = drawing::randomBounded(2u) == 0u ? -1.0f : 1.0f;
    const float start = static_cast<float>(drawing::randomBits()) /
                        4294967295.0f * 2.0f * s3g::kPi;
    float angle = start;
    float height =
        static_cast<float>(drawing::randomBits()) / 4294967295.0f * 1.4f - 0.7f;
    for (uint32_t step = 0u; step < path.size(); ++step) {
      if (step > 0u) {
        const float jitter = 0.62f + static_cast<float>(drawing::randomBits()) /
                                         4294967295.0f * 0.76f;
        angle += direction * 2.0f * s3g::kPi / 16.0f * jitter;
      }
      const float radius = 0.72f + static_cast<float>(drawing::randomBits()) /
                                       4294967295.0f * 0.28f;
      const float targetHeight =
          static_cast<float>(drawing::randomBits()) / 4294967295.0f * 2.0f -
          1.0f;
      height = std::clamp(height * 0.62f + targetHeight * 0.38f, -1.0f, 1.0f);
      path[step] = {
          std::cos(angle) * radius,
          std::sin(angle) * radius,
          height,
      };
    }
    if (!queueGuiSpatialPath(*p, path)) {
      beep();
      return;
    }
    markCustomState();
    setNeedsDisplay(true);
  }

  void drawOriginal(drawing::Rect dirty) {
    (void)dirty;
    auto *p = static_cast<Plugin *>(_plugin);
    if (!p)
      return;
    drawing::Style style;
    drawing::Attrs titleAttrs = drawing::softTitleAttrs();
    drawing::Attrs labelAttrs = drawing::softLabelAttrs();
    drawing::Attrs valueAttrs = drawing::softValueAttrs();
    drawing::Attrs noteAttrs =
        drawing::textAttrs(drawing::calibratedColor(0xd4d4d4), 11.0);
    drawing::Attrs tinyAttrs =
        drawing::textAttrs(drawing::calibratedColor(0x8f8f8f), 8.5);
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    drawing::setFill(style.strip);
    drawing::rectFill(drawing::makeRect(0.0, 0.0, kGuiWidth, 40.0));
    drawing::setFill(style.grid);
    drawing::rectFill(drawing::makeRect(0.0, 39.0, kGuiWidth, 1.0));
    const float activity = p->visualActivity.load(std::memory_order_relaxed);
    const int32_t midiRoot = p->visualMidiNote.load(std::memory_order_relaxed);
    const bool transportSync = paramValue(*p, kTransportSyncParamId) >= 0.5;
    const bool transportPlaying =
        p->visualTransportPlaying.load(std::memory_order_relaxed);
    const bool dualMono = paramValue(*p, kOutputModeParamId) >= 0.5;
    drawing::String performanceRoot =
        midiRoot >= 0 ? drawing::stringFormat("MIDI %@", midiNoteName(midiRoot))
                      : "ROOT";
    const auto titleBand = drawing::encoderTitleBand(kGuiWidth, kGuiHeight);
    drawing::drawEncoderTitleBand(
        "s3g AMBI ENCODER ACID 16", drawing::stringFromUtf8(_titlePresetName),
        drawing::stringFormat(
            "%@%@  %@  BODY %3.0f%%  %@", transportSync ? "HOST" : "INT",
            transportSync && transportPlaying ? " >" : "",
            dualMono ? "MONO 1+2" : "3OA", activity * 100.0f, performanceRoot),
        titleBand, titleAttrs, labelAttrs, valueAttrs, style);

    drawing::drawPanelFrame(kPatternX, kPatternY, kPatternWidth, kPatternHeight,
                            style);
    drawing::drawPanelHeader(
        "16-STEP NOTE LINE  ·  DRAG PITCH  ·  GATE / ACCENT / SLIDE", true,
        kPatternX, kPatternY, kPatternWidth, 22.0, labelAttrs, style);
    const uint32_t currentStep =
        p->visualCurrentStep.load(std::memory_order_relaxed);
    const uint32_t patternLength =
        static_cast<uint32_t>(std::lround(paramValue(*p, kLengthParamId)));
    const int root =
        midiRoot >= 0
            ? midiRoot
            : static_cast<int>(std::lround(paramValue(*p, kRootParamId)));
    const uint32_t scaleValue =
        static_cast<uint32_t>(std::lround(paramValue(*p, kScaleParamId)));
    for (uint32_t step = 0u; step < s3g::kAmbiAcidStepCount; ++step) {
      const drawing::Rect cell = stepCellRect(step);
      const drawing::Rect noteRect = stepNoteRect(step);
      const bool selected = step == static_cast<uint32_t>(_selectedStep);
      const bool playing = step == currentStep && activity > 0.002f;
      const bool inLength = step < patternLength;
      const clap_id noteId = kStepParamBase + step * kStepParamStride;
      const int rawOffset =
          static_cast<int>(std::lround(paramValue(*p, noteId)));
      const int offset =
          s3g::ambiAcidQuantizeSemitoneOffset(rawOffset, scaleValue);
      const bool gate = paramValue(*p, noteId + 1u) >= 0.5;
      const bool accent = paramValue(*p, noteId + 2u) >= 0.5;
      const bool slide = paramValue(*p, noteId + 3u) >= 0.5;

      drawing::setFill(
          drawing::calibratedColor(inLength ? 0x191919 : 0x111111));
      drawing::rectFill(cell);
      drawing::setStroke(
          drawing::calibratedColor(selected ? 0xb8b8b8 : 0x3d3d3d));
      drawing::frameRect(cell);
      if (playing) {
        drawing::setFill(drawing::calibratedColor(0xd5d5d5));
        drawing::rectFill(drawing::makeRect(cell.origin.x, cell.origin.y,
                                            cell.size.width, 3.0));
      }
      drawCenteredText(
          drawing::stringFormat("%02u", step + 1u),
          drawing::makeRect(cell.origin.x, 78.0, cell.size.width, 12.0),
          playing ? noteAttrs : tinyAttrs);

      drawing::setFill(drawing::calibratedColor(gate ? 0x202020 : 0x141414));
      drawing::rectFill(noteRect);
      drawing::setStroke(drawing::calibratedColor(0x373737));
      drawing::frameRect(noteRect);
      const double rootY = noteRect.origin.y + noteRect.size.height * 0.5;
      drawing::setFill(drawing::calibratedColor(0x4b4b4b));
      drawing::rectFill(drawing::makeRect(noteRect.origin.x + 1.0, rootY,
                                          noteRect.size.width - 2.0, 1.0));
      const double markerY =
          noteRect.origin.y + 2.0 +
          (36.0 - offset) / 72.0 * (noteRect.size.height - 5.0);
      drawing::setFill(drawing::calibratedColor(gate ? 0xb0b0b0 : 0x4d4d4d));
      drawing::rectFill(drawing::makeRect(noteRect.origin.x + 3.0, markerY,
                                          noteRect.size.width - 6.0,
                                          playing ? 3.0 : 2.0));
      drawCenteredText(midiNoteName(root + offset),
                       drawing::makeRect(noteRect.origin.x, 112.0,
                                         noteRect.size.width, 18.0),
                       noteAttrs);
      drawCenteredText(drawing::stringFormat("%+d", offset),
                       drawing::makeRect(noteRect.origin.x, 137.0,
                                         noteRect.size.width, 14.0),
                       tinyAttrs);
      if (!gate) {
        drawCenteredText("REST",
                         drawing::makeRect(noteRect.origin.x, 173.0,
                                           noteRect.size.width, 14.0),
                         tinyAttrs);
      }

      const bool toggles[]{gate, accent, slide};
      drawing::String labels[]{"G", "A", "S"};
      for (uint32_t toggle = 0u; toggle < 3u; ++toggle) {
        const auto kind = static_cast<StepParamKind>(toggle + 1u);
        const drawing::Rect rect = stepToggleRect(step, kind);
        drawing::setFill(drawing::calibratedColor(
            toggles[toggle] ? (toggle == 1u ? 0x454545 : 0x353535) : 0x151515));
        drawing::rectFill(rect);
        drawing::setStroke(
            drawing::calibratedColor(toggles[toggle] ? 0xa8a8a8 : 0x383838));
        drawing::frameRect(rect);
        drawCenteredText(labels[toggle], rect,
                         toggles[toggle] ? valueAttrs : tinyAttrs);
      }
    }

    const drawing::Rect controlPanel =
        drawing::makeRect(16.0, 268.0, 286.0, 336.0);
    const drawing::Rect spatialPanel =
        drawing::makeRect(309.0, 268.0, 595.0, 336.0);
    const auto drawPanel = [&](drawing::String title, drawing::Rect rect) {
      drawing::drawPanelFrame(rect.origin.x, rect.origin.y, rect.size.width,
                              rect.size.height, style);
      drawing::drawPanelHeader(title, true, rect.origin.x, rect.origin.y,
                               rect.size.width, 22.0, labelAttrs, style);
    };
    drawPanel("CONTROL BANK", controlPanel);
    drawPanel(drawing::stringFormat(
                  dualMono ? "SPATIAL STEP PATH · BYPASSED · STEP %02ld"
                           : "SPATIAL STEP PATH · STEP %02ld",
                  static_cast<long>(_selectedStep + 1)),
              spatialPanel);
    static drawing::String controlLabels[]{"LINE", "OSC", "DRIVE", "FIELD"};
    for (uint32_t page = 0u; page < 4u; ++page) {
      drawing::drawToolboxHeaderButton(
          controlPageButtonRect(page), controlPanel, controlLabels[page],
          _controlPage == static_cast<int>(page), tinyAttrs, style);
    }
    drawing::drawToolboxHeaderActionButton(
        spatialPathButtonRect(0u), spatialPanel, "RESET", tinyAttrs, style);
    drawing::drawToolboxHeaderActionButton(
        spatialPathButtonRect(1u), spatialPanel, "RANDOM", tinyAttrs, style);
    for (const auto &row : kUiRows) {
      if (controlPageForRow(row) != static_cast<uint32_t>(_controlPage))
        continue;
      const double value = paramValue(*p, row.id);
      char text[64]{};
      paramsValueToText(&p->plugin, row.id, value, text, sizeof(text));
      if (row.id == kTempoParamId) {
        std::snprintf(text, sizeof(text), "%.1f", value);
      } else if (row.id == kTransportSyncParamId) {
        std::snprintf(text, sizeof(text), "%s", value >= 0.5 ? "HOST" : "INT");
      }
      if (row.id == kScaleParamId || discreteUiMenuParam(row.id)) {
        drawing::drawProcessorMenu(
            drawing::stringFromUtf8(row.label), drawing::stringFromUtf8(text),
            row.y, controlPanel.origin.x, controlPanel.size.width, labelAttrs,
            valueAttrs, style);
      } else {
        drawing::drawProcessorSlider(
            drawing::stringFromUtf8(row.label), drawing::stringFromUtf8(text),
            uiNormalizedValue(row.id, value), row.y, controlPanel.origin.x,
            controlPanel.size.width, labelAttrs, valueAttrs, style);
      }
    }
    drawing::setFill(style.grid);
    drawing::rectFill(drawing::makeRect(controlPanel.origin.x + 16.0,
                                        kOutputSectionDividerY,
                                        controlPanel.size.width - 32.0, 1.0));
    drawing::drawAtPoint_withAttributes(
        "OUTPUT",
        drawing::makePoint(controlPanel.origin.x + 16.0,
                           kOutputSectionDividerY + 2.0),
        labelAttrs);
    drawing::drawProcessorMenu(drawing::stringFromUtf8(kFormatRow.label),
                               formatMenuSelectedText(*p), kFormatRow.y,
                               controlPanel.origin.x, controlPanel.size.width,
                               labelAttrs, valueAttrs, style);
    {
      const double value = paramValue(*p, kOutputLevelRow.id);
      char text[64]{};
      paramsValueToText(&p->plugin, kOutputLevelRow.id, value, text,
                        sizeof(text));
      drawing::drawProcessorSlider(
          drawing::stringFromUtf8(kOutputLevelRow.label),
          drawing::stringFromUtf8(text),
          uiNormalizedValue(kOutputLevelRow.id, value), kOutputLevelRow.y,
          controlPanel.origin.x, controlPanel.size.width, labelAttrs,
          valueAttrs, style);
    }

    for (uint32_t view = 0u; view < 2u; ++view) {
      const drawing::Rect field = spatialPathFieldRect(view);
      drawing::setFill(drawing::calibratedColor(0x111111));
      drawing::rectFill(field);
      drawing::setStroke(style.grid);
      drawing::frameRect(field);
      const double scale = std::min(field.size.width, field.size.height) * 0.42;
      const drawing::Point center =
          drawing::makePoint(drawing::midX(field), drawing::midY(field));
      drawing::setStroke(drawing::calibratedColor(0x292929));
      drawing::Path::strokeLineFromPoint_toPoint(
          drawing::makePoint(center.x, center.y - scale),
          drawing::makePoint(center.x, center.y + scale));
      drawing::Path::strokeLineFromPoint_toPoint(
          drawing::makePoint(center.x - scale, center.y),
          drawing::makePoint(center.x + scale, center.y));
      drawing::Path boundary =
          drawing::Path::bezierPathWithOvalInRect(drawing::makeRect(
              center.x - scale, center.y - scale, scale * 2.0, scale * 2.0));
      (boundary).setLineWidth(0.7);
      (boundary).stroke();
      drawing::drawAtPoint_withAttributes(
          (view == 0u ? "TOP · X/Y" : "SIDE · Y/Z"),
          drawing::makePoint(field.origin.x + 7.0, field.origin.y + 5.0),
          tinyAttrs);
      if (view == 0u) {
        drawing::drawAtPoint_withAttributes(
            "0 / F", drawing::makePoint(center.x - 12.0, field.origin.y + 5.0),
            tinyAttrs);
        drawing::drawAtPoint_withAttributes(
            "180 / B",
            drawing::makePoint(center.x - 18.0, drawing::maxY(field) - 15.0),
            tinyAttrs);
        drawing::drawAtPoint_withAttributes(
            "+90 / L", drawing::makePoint(field.origin.x + 6.0, center.y - 5.0),
            tinyAttrs);
        drawing::drawAtPoint_withAttributes(
            "-90 / R",
            drawing::makePoint(drawing::maxX(field) - 39.0, center.y - 5.0),
            tinyAttrs);
      } else {
        drawing::drawAtPoint_withAttributes(
            "+90 EL", drawing::makePoint(center.x - 18.0, field.origin.y + 5.0),
            tinyAttrs);
        drawing::drawAtPoint_withAttributes(
            "-90 EL",
            drawing::makePoint(center.x - 18.0, drawing::maxY(field) - 15.0),
            tinyAttrs);
        drawing::drawAtPoint_withAttributes(
            "+90 / L", drawing::makePoint(field.origin.x + 6.0, center.y - 5.0),
            tinyAttrs);
        drawing::drawAtPoint_withAttributes(
            "-90 / R",
            drawing::makePoint(drawing::maxX(field) - 39.0, center.y - 5.0),
            tinyAttrs);
      }

      drawing::Path path = drawing::Path::bezierPath();
      (path).setLineWidth(1.1);
      for (uint32_t step = 0u; step < s3g::kAmbiAcidStepCount; ++step) {
        const drawing::Point point =
            spatialPointScreenPosition(*p, step, field, view);
        if (step == 0u)
          (path).moveToPoint(point);
        else
          (path).lineToPoint(point);
      }
      (path).lineToPoint(spatialPointScreenPosition(*p, 0u, field, view));
      drawing::setStroke(drawing::calibratedColor(0x777777, 0.75));
      (path).stroke();

      for (uint32_t step = 0u; step < s3g::kAmbiAcidStepCount; ++step) {
        const drawing::Point point =
            spatialPointScreenPosition(*p, step, field, view);
        const clap_id base = kSpatialParamBase + step * kSpatialParamStride;
        const float height = static_cast<float>(
            paramValue(*p, base + static_cast<uint32_t>(SpatialParamKind::Z)));
        const bool selected = step == static_cast<uint32_t>(_selectedStep);
        const bool playing = step == currentStep && activity > 0.002f;
        const double radius = selected ? 5.0 : playing ? 4.5 : 3.2;
        const int shade =
            static_cast<int>(std::lround(112.0 + (height + 1.0f) * 48.0));
        drawing::setFill(
            drawing::calibratedColor((shade << 16) | (shade << 8) | shade));
        const drawing::Rect marker = drawing::makeRect(
            std::round(point.x - radius), std::round(point.y - radius),
            radius * 2.0, radius * 2.0);
        drawing::rectFill(marker);
        drawing::setStroke(drawing::calibratedColor(
            selected || playing ? 0xe0e0e0 : 0x303030));
        drawing::frameRect(marker);
        if (selected) {
          drawing::setStroke(drawing::calibratedColor(0xbcbcbc));
          drawing::frameRect(drawing::inset(marker, -2.0, -2.0));
        }
        drawing::String number = drawing::stringFormat("%u", step + 1u);
        drawing::drawAtPoint_withAttributes(
            number, drawing::makePoint(point.x + 5.0, point.y - 10.0),
            tinyAttrs);
      }

      const float directionX =
          p->visualDirectionX.load(std::memory_order_relaxed);
      const float directionY =
          p->visualDirectionY.load(std::memory_order_relaxed);
      const float directionZ =
          p->visualDirectionZ.load(std::memory_order_relaxed);
      const drawing::Point heard =
          view == 0u ? drawing::makePoint(center.x - directionY * scale,
                                          center.y - directionX * scale)
                     : drawing::makePoint(center.x - directionY * scale,
                                          center.y - directionZ * scale);
      drawing::setStroke(drawing::calibratedColor(0xe8e8e8));
      drawing::frameRect(
          drawing::makeRect(heard.x - 3.0, heard.y - 3.0, 6.0, 6.0));
    }
    if (_presetMenuOpen) {
      drawing::String items[s3g::kAmbiAcidPatternPresets.size()]{};
      for (uint32_t index = 0u; index < s3g::kAmbiAcidPatternPresets.size();
           ++index) {
        items[index] =
            drawing::stringFromUtf8(s3g::kAmbiAcidPatternPresets[index].name);
      }
      drawing::drawDropdownMenu(patternPresetMenuRect(), 18.0, items,
                                static_cast<uint32_t>(std::size(items)),
                                _presetIndex, _presetHover, valueAttrs, style);
    }
    if (_scaleMenuOpen) {
      const uint32_t scale =
          static_cast<uint32_t>(std::lround(paramValue(*p, kScaleParamId)));
      drawing::drawMultiColumnDropdownMenu(
          musicalScaleMenuRect(), kScaleMenuItemHeight, musicalScaleMenuItems(),
          s3g::kMusicalScaleCount, kScaleMenuColumns,
          static_cast<int>(s3g::musicalScaleMenuIndexForValue(scale)),
          _scaleMenuHover, valueAttrs, style);
    }
    if (_parameterMenuId != CLAP_INVALID_ID) {
      constexpr uint32_t kMaximumMenuItems = s3g::kAmbiAcidDriveCircuitCount;
      drawing::String items[kMaximumMenuItems]{};
      const uint32_t count = discreteUiMenuItemCount(_parameterMenuId);
      for (uint32_t index = 0u; index < count; ++index) {
        items[index] = discreteUiMenuItem(_parameterMenuId, index);
      }
      drawing::drawDropdownMenu(
          discreteUiMenuRect(_parameterMenuId), 18.0, items, count,
          _parameterMenuId == kFormatMenuId
              ? formatMenuSelectedIndex(*p)
              : discreteUiMenuSelectedIndex(_parameterMenuId,
                                            paramValue(*p, _parameterMenuId)),
          _parameterMenuHover, valueAttrs, style);
    }
  }

  void updateDraggedParam(drawing::Point point) {
    if (_dragParam <= 0)
      return;
    const clap_id id = static_cast<clap_id>(_dragParam);
    bool visible = id == kOutputLevelRow.id;
    for (const auto &row : kUiRows)
      visible = visible || row.id == id;
    if (!visible)
      return;
    const double controlX = s3g::gui_layout::processorControlX(16.0);
    const double trackWidth = s3g::gui_layout::processorTrackWidth(286.0);
    const double normalized =
        std::clamp((point.x - controlX) / trackWidth, 0.0, 1.0);
    queueGuiParamValue(*static_cast<Plugin *>(_plugin), id,
                       uiValueFromNormalized(id, normalized));
    setNeedsDisplay(true);
  }

  void updateDraggedSpatialPoint(drawing::Point point) {
    if (_dragSpatialView < 0)
      return;
    auto *p = static_cast<Plugin *>(_plugin);
    if (!p)
      return;
    const uint32_t view = static_cast<uint32_t>(_dragSpatialView);
    const drawing::Rect field = spatialPathFieldRect(view);
    const double scale = std::min(field.size.width, field.size.height) * 0.42;
    const clap_id base =
        kSpatialParamBase +
        static_cast<clap_id>(_selectedStep) * kSpatialParamStride;
    if (view == 0u) {
      const double x = std::clamp((drawing::midY(field) - point.y) /
                                      std::max<double>(1.0, scale),
                                  -1.0, 1.0);
      const double y = std::clamp((drawing::midX(field) - point.x) /
                                      std::max<double>(1.0, scale),
                                  -1.0, 1.0);
      editValue(base, x);
      editValue(base + 1u, y);
    } else {
      const double y = std::clamp((drawing::midX(field) - point.x) /
                                      std::max<double>(1.0, scale),
                                  -1.0, 1.0);
      const double z = std::clamp((drawing::midY(field) - point.y) /
                                      std::max<double>(1.0, scale),
                                  -1.0, 1.0);
      editValue(base + 1u, y);
      editValue(base + 2u, z);
    }
    markCustomState();
    setNeedsDisplay(true);
  }

  void updateDraggedNote(drawing::Point point) {
    if (_dragStep < 0)
      return;
    const drawing::Rect rect = stepNoteRect(static_cast<uint32_t>(_dragStep));
    const double normalized =
        std::clamp((point.y - rect.origin.y) / rect.size.height, 0.0, 1.0);
    const int32_t rawNote =
        static_cast<int32_t>(std::lround(36.0 - normalized * 72.0));
    auto *p = static_cast<Plugin *>(_plugin);
    const uint32_t scale =
        static_cast<uint32_t>(std::lround(paramValue(*p, kScaleParamId)));
    const double note = s3g::ambiAcidQuantizeSemitoneOffset(rawNote, scale);
    const clap_id id =
        kStepParamBase + static_cast<clap_id>(_dragStep) * kStepParamStride;
    editValue(id, note);
    markCustomPattern();
    setNeedsDisplay(true);
  }

  void mouseDown(drawing::Event *event) {
    auto *p = static_cast<Plugin *>(_plugin);
    if (!p)
      return;

    const drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    if (_parameterMenuId != CLAP_INVALID_ID) {
      const clap_id menuId = _parameterMenuId;
      const int hit =
          drawing::dropdownHitIndex(point, discreteUiMenuRect(menuId), 18.0,
                                    discreteUiMenuItemCount(menuId));
      _parameterMenuId = CLAP_INVALID_ID;
      _parameterMenuHover = -1;
      if (hit >= 0) {
        if (menuId == kFormatMenuId) {
          if (hit < 3) {
            singleEdit(kOutputModeParamId, 0.0);
            singleEdit(kOrderParamId, hit + 1.0);
          } else {
            singleEdit(kOutputModeParamId, 1.0);
          }
          markCustomState();
        } else {
          singleEdit(menuId,
                     discreteUiMenuValue(menuId, static_cast<uint32_t>(hit)));
          if (menuId != kOutputParamId)
            markCustomState();
        }
      }
      setNeedsDisplay(true);
      return;
    }
    if (_scaleMenuOpen) {
      const int hit = drawing::multiColumnDropdownHitIndex(
          point, musicalScaleMenuRect(), kScaleMenuItemHeight,
          s3g::kMusicalScaleCount, kScaleMenuColumns);
      _scaleMenuOpen = false;
      _scaleMenuHover = -1;
      if (hit >= 0) {
        const uint32_t scale =
            s3g::musicalScaleValueForMenuIndex(static_cast<uint32_t>(hit));
        singleEdit(kScaleParamId, scale);
        markCustomState();
      }
      setNeedsDisplay(true);
      return;
    }
    if (_presetMenuOpen) {
      const int hit = drawing::dropdownHitIndex(
          point, patternPresetMenuRect(), 18.0,
          static_cast<uint32_t>(s3g::kAmbiAcidPatternPresets.size()));
      _presetMenuOpen = false;
      _presetHover = -1;
      if (hit >= 0)
        applyPatternPreset(hit);
      setNeedsDisplay(true);
      return;
    }
    const auto titleBand = drawing::encoderTitleBand(kGuiWidth, kGuiHeight);
    if (drawing::contains(point, drawing::cocoaRect(titleBand.presetMenu))) {
      _presetMenuOpen = true;
      _presetHover = -1;
      _scaleMenuOpen = false;
      _scaleMenuHover = -1;
      _parameterMenuId = CLAP_INVALID_ID;
      _parameterMenuHover = -1;
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, drawing::cocoaRect(titleBand.loadButton))) {
      drawing::String name{};
      if (presetDialog(false, &name)) {
        _presetIndex = publishedPatternPresetIndex(*p);
        std::snprintf(_patternName, sizeof(_patternName), "%s",
                      _presetIndex >= 0
                          ? s3g::kAmbiAcidPatternPresets[static_cast<uint32_t>(
                                                             _presetIndex)]
                                .name
                          : "CUSTOM");
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                      !name.empty() ? name.c_str() : "CUSTOM");
      } else {
        beep();
      }
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, drawing::cocoaRect(titleBand.saveButton))) {
      drawing::String name{};
      if (presetDialog(true, &name)) {
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                      !name.empty() ? name.c_str() : "CUSTOM");
      } else {
        beep();
      }
      setNeedsDisplay(true);
      return;
    }
    if (drawing::contains(point, drawing::cocoaRect(titleBand.randomButton))) {
      randomizePattern();
      return;
    }
    for (uint32_t page = 0u; page < 4u; ++page) {
      if (drawing::contains(point, controlPageButtonRect(page))) {
        _controlPage = static_cast<int>(page);
        _dragParam = -1;
        _dragSpatialView = -1;
        setNeedsDisplay(true);
        return;
      }
    }
    if (drawing::contains(point, spatialPathButtonRect(0u))) {
      resetSpatialPath();
      return;
    }
    if (drawing::contains(point, spatialPathButtonRect(1u))) {
      randomizeSpatialPath();
      return;
    }
    for (uint32_t view = 0u; view < 2u; ++view) {
      const drawing::Rect field = spatialPathFieldRect(view);
      if (drawing::contains(point, field)) {
        int hitStep = -1;
        double best = 9.0;
        for (uint32_t step = 0u; step < s3g::kAmbiAcidStepCount; ++step) {
          const drawing::Point marker =
              spatialPointScreenPosition(*p, step, field, view);
          const double distance =
              std::hypot(marker.x - point.x, marker.y - point.y);
          if (distance < best) {
            best = distance;
            hitStep = static_cast<int>(step);
          }
        }
        if (hitStep >= 0)
          _selectedStep = hitStep;
        const clap_id base =
            kSpatialParamBase +
            static_cast<clap_id>(_selectedStep) * kSpatialParamStride;
        if ((event)->clickCount() >= 2) {
          const auto &defaultPoint =
              s3g::kAmbiAcidDefaultSpatialPath[static_cast<uint32_t>(
                  _selectedStep)];
          singleEdit(base, defaultPoint.x);
          singleEdit(base + 1u, defaultPoint.y);
          singleEdit(base + 2u, defaultPoint.z);
          markCustomState();
        } else {
          _dragSpatialView = static_cast<int>(view);
          beginEdit(base);
          beginEdit(base + (view == 0u ? 1u : 2u));
          updateDraggedSpatialPoint(point);
        }
        setNeedsDisplay(true);
        return;
      }
    }
    for (uint32_t step = 0u; step < s3g::kAmbiAcidStepCount; ++step) {
      if (drawing::contains(point, stepNoteRect(step))) {
        _selectedStep = step;
        const clap_id id = kStepParamBase + step * kStepParamStride;
        double defaultValue = 0.0;
        if (drawing::sliderDoubleClickDefault(event, &p->plugin, id,
                                              &defaultValue)) {
          singleEdit(id, defaultValue);
        } else {
          _dragStep = static_cast<int>(step);
          beginEdit(id);
          updateDraggedNote(point);
        }
        markCustomPattern();
        setNeedsDisplay(true);
        return;
      }
      for (uint32_t toggle = 0u; toggle < 3u; ++toggle) {
        const auto kind = static_cast<StepParamKind>(toggle + 1u);
        if (!drawing::contains(point, stepToggleRect(step, kind)))
          continue;
        _selectedStep = step;
        const clap_id id =
            kStepParamBase + step * kStepParamStride + toggle + 1u;
        singleEdit(id, paramValue(*p, id) >= 0.5 ? 0.0 : 1.0);
        markCustomPattern();
        setNeedsDisplay(true);
        return;
      }
    }
    for (const auto &row : kUiRows) {
      if (controlPageForRow(row) != static_cast<uint32_t>(_controlPage))
        continue;
      const drawing::Rect hit = drawing::makeRect(
          16.0 + s3g::gui_layout::kStandardMetrics.hitInset, row.y - 9.0,
          286.0 - s3g::gui_layout::kStandardMetrics.hitInset * 2.0,
          s3g::gui_layout::kStandardMetrics.hitHeight);
      if (!drawing::contains(point, hit))
        continue;
      if (row.id == kScaleParamId) {
        _scaleMenuOpen = true;
        _scaleMenuHover = -1;
        _parameterMenuId = CLAP_INVALID_ID;
        _parameterMenuHover = -1;
        _presetMenuOpen = false;
        _presetHover = -1;
        setNeedsDisplay(true);
        return;
      }
      if (discreteUiMenuParam(row.id)) {
        _parameterMenuId = row.id;
        _parameterMenuHover = -1;
        _scaleMenuOpen = false;
        _scaleMenuHover = -1;
        _presetMenuOpen = false;
        _presetHover = -1;
        setNeedsDisplay(true);
        return;
      }
      double defaultValue = 0.0;
      if (drawing::sliderDoubleClickDefault(event, &p->plugin, row.id,
                                            &defaultValue)) {
        singleEdit(row.id, defaultValue);
      } else {
        _dragParam = static_cast<int>(row.id);
        beginEdit(row.id);
        updateDraggedParam(point);
      }
      if (row.id != kOutputParamId)
        markCustomState();
      return;
    }
    {
      const drawing::Rect hit = drawing::makeRect(
          16.0 + s3g::gui_layout::kStandardMetrics.hitInset, kFormatRow.y - 9.0,
          286.0 - s3g::gui_layout::kStandardMetrics.hitInset * 2.0,
          s3g::gui_layout::kStandardMetrics.hitHeight);
      if (drawing::contains(point, hit)) {
        _parameterMenuId = kFormatMenuId;
        _parameterMenuHover = -1;
        _scaleMenuOpen = false;
        _scaleMenuHover = -1;
        _presetMenuOpen = false;
        _presetHover = -1;
        setNeedsDisplay(true);
        return;
      }
    }
    {
      const drawing::Rect hit = drawing::makeRect(
          16.0 + s3g::gui_layout::kStandardMetrics.hitInset,
          kOutputLevelRow.y - 9.0,
          286.0 - s3g::gui_layout::kStandardMetrics.hitInset * 2.0,
          s3g::gui_layout::kStandardMetrics.hitHeight);
      if (drawing::contains(point, hit)) {
        double defaultValue = 0.0;
        if (drawing::sliderDoubleClickDefault(
                event, &p->plugin, kOutputLevelRow.id, &defaultValue)) {
          singleEdit(kOutputLevelRow.id, defaultValue);
        } else {
          _dragParam = static_cast<int>(kOutputLevelRow.id);
          beginEdit(kOutputLevelRow.id);
          updateDraggedParam(point);
        }
        return;
      }
    }
  }

  void mouseDragged(drawing::Event *event) {
    const drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    if (_dragSpatialView >= 0)
      updateDraggedSpatialPoint(point);
    else if (_dragStep >= 0)
      updateDraggedNote(point);
    else if (_dragParam > 0)
      updateDraggedParam(point);
  }

  void mouseUp(drawing::Event *event) {
    (void)event;
    auto *p = static_cast<Plugin *>(_plugin);
    if (p && _dragStep >= 0) {
      endEdit(kStepParamBase +
              static_cast<clap_id>(_dragStep) * kStepParamStride);
    }
    if (p && _dragParam > 0) {
      endEdit(static_cast<clap_id>(_dragParam));
    }
    if (p && _dragSpatialView >= 0) {
      const clap_id base =
          kSpatialParamBase +
          static_cast<clap_id>(_selectedStep) * kSpatialParamStride;
      endEdit(base);
      endEdit(base + (_dragSpatialView == 0 ? 1u : 2u));
    }
    _dragStep = -1;
    _dragParam = -1;
    _dragSpatialView = -1;
  }

  void scrollWheel(drawing::Event *event) {
    auto *p = static_cast<Plugin *>(_plugin);
    if (!p || event->delta == 0.0)
      return;
    const clap_id id =
        kStepParamBase + static_cast<clap_id>(_selectedStep) * kStepParamStride;
    const int32_t current =
        static_cast<int32_t>(std::lround(paramValue(*p, id)));
    const int32_t direction = event->delta > 0.0 ? 1 : -1;
    const uint32_t scale =
        static_cast<uint32_t>(std::lround(paramValue(*p, kScaleParamId)));
    const bool octave = wheelShift;
    const int32_t next =
        octave ? s3g::ambiAcidQuantizeSemitoneOffset(current + direction * 12,
                                                     scale)
               : s3g::ambiAcidMoveScaleDegree(current, scale, direction);
    singleEdit(id, next);
    markCustomPattern();
    setNeedsDisplay(true);
  }

  void mouseMoved(drawing::Event *event) {
    if (!_presetMenuOpen && !_scaleMenuOpen &&
        _parameterMenuId == CLAP_INVALID_ID)
      return;
    const drawing::Point point =
        convertPoint_fromView((event)->locationInWindow(), nullptr);
    if (_scaleMenuOpen) {
      const int hover = drawing::multiColumnDropdownHitIndex(
          point, musicalScaleMenuRect(), kScaleMenuItemHeight,
          s3g::kMusicalScaleCount, kScaleMenuColumns);
      if (hover != _scaleMenuHover) {
        _scaleMenuHover = hover;
        setNeedsDisplay(true);
      }
      return;
    }
    if (_parameterMenuId != CLAP_INVALID_ID) {
      const int hover = drawing::dropdownHitIndex(
          point, discreteUiMenuRect(_parameterMenuId), 18.0,
          discreteUiMenuItemCount(_parameterMenuId));
      if (hover != _parameterMenuHover) {
        _parameterMenuHover = hover;
        setNeedsDisplay(true);
      }
      return;
    }
    const int hover = drawing::dropdownHitIndex(
        point, patternPresetMenuRect(), 18.0,
        static_cast<uint32_t>(s3g::kAmbiAcidPatternPresets.size()));
    if (hover != _presetHover) {
      _presetHover = hover;
      setNeedsDisplay(true);
    }
  }
};
} // namespace score_encoder_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new score_encoder_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
