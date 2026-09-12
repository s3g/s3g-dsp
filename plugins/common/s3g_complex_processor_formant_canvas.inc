// Literal Formant Matrix Cocoa canvas port: all five pages and signed scenes.
namespace complex_processor_canvas {
namespace drawing = s3g::portable_gui::complex_drawing;
namespace foundation = s3g::portable_gui::foundation;
// Native editor for s3g Processor Formant Matrix 2. This file is included by
// the owning Objective-C++ translation unit after its DSP/control helpers have
// been declared and before the CLAP GUI extension glue is reopened.

constexpr s3g::gui_layout::Canvas kFormantCanvas{
    static_cast<double>(kGuiWidth), static_cast<double>(kGuiHeight)};

enum FormantPage : uint32_t {
  kFormantPageRoute = 0u,
  kFormantPageBank,
  kFormantPageCarrier,
  kFormantPagePhrase,
  kFormantPageFx,
  kFormantPageCount,
};

constexpr const char *kFormantPageNames[kFormantPageCount]{
    "ROUTE", "BANK", "SOURCES", "PHRASE", "FX"};

constexpr s3g::gui_layout::Panel kFormantRoutePanel{
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Routing,
    {12.0, 74.0, 884.0, 882.0},
    0.0,
    0.0,
    0u};
constexpr s3g::gui_layout::Panel kFormantOutputPanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Output,
    {908.0, 74.0, 436.0, 54.0},
    31.0,
    26.0,
    1u};
constexpr s3g::gui_layout::Panel kFormantCrosspointPanel{
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::SelectedObject,
    {908.0, 140.0, 436.0, 132.0},
    37.0,
    26.0,
    1u};
constexpr s3g::gui_layout::Panel kFormantMatrixControlPanel{
    s3g::gui_layout::PluginClass::MixerMatrixLane,
    s3g::gui_layout::PanelRole::Routing,
    {908.0, 284.0, 436.0, 168.0},
    32.0,
    26.0,
    5u};
constexpr s3g::gui_layout::Panel kFormantSignalPanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Source,
    {908.0, 464.0, 436.0, 194.0},
    31.0,
    25.0,
    3u};
constexpr s3g::gui_layout::Panel kFormantEnvelopePanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Envelope,
    {908.0, 670.0, 436.0, 286.0},
    31.0,
    29.5,
    11u};

constexpr s3g::gui_layout::Panel kFormantGenericLeftPanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Engine,
    {12.0, 74.0, 660.0, 882.0},
    39.0,
    30.0,
    16u};
constexpr s3g::gui_layout::Panel kFormantGenericRightPanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::ToneShape,
    {684.0, 74.0, 660.0, 882.0},
    39.0,
    30.0,
    15u};
constexpr s3g::gui_layout::Panel kFormantPhraseInputPanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::EventTiming,
    {12.0, 74.0, 1332.0, 150.0},
    0.0,
    0.0,
    0u};
constexpr s3g::gui_layout::Panel kFormantPhraseLeftPanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::EventTiming,
    {12.0, 236.0, 660.0, 720.0},
    37.0,
    29.0,
    24u};
constexpr s3g::gui_layout::Panel kFormantPhraseRightPanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Source,
    {684.0, 236.0, 660.0, 720.0},
    37.0,
    29.0,
    24u};

constexpr double kFormantMatrixFirstX = 146.0;
constexpr double kFormantMatrixFirstY = 178.0;
constexpr double kFormantMatrixExtent = 525.0;
constexpr double kFormantMenuItemHeight = 18.0;
constexpr uint32_t kFormantScaleMenuColumns = 4u;

constexpr clap_id kFormantRouteMatrixParams[]{
    kMatrixModeParamId, kMatrixMorphParamId, kCustomMatrixMorphParamId,
    kCouplingParamId,   kBandLayoutParamId,
};
constexpr clap_id kFormantRouteSignalParams[]{
    kBankModeParamId,
    kBankAmountParamId,
    kArticulationThruParamId,
};
constexpr clap_id kFormantRouteEnvelopeParams[]{
    kIntelligibilityParamId,  kMouthFocusParamId,  kBankAttackParamId,
    kBankReleaseParamId,      kOpenLevelParamId,   kVoicingModeParamId,
    kVoicingThresholdParamId, kVoicedLevelParamId, kUnvoicedLevelParamId,
};

constexpr clap_id kFormantBankParams[]{
    // Analysis and filter-bank behavior live here; ROUTE owns topology,
    // global mix, and performance envelopes.
    kTransferModeParamId,
    kAnalysisSlopeParamId,
    kAnalysisWidthParamId,
    kVoiceFocusParamId,
    kAnalysisLevelerParamId,
    kAnalysisLowEqParamId,
    kAnalysisMidEqParamId,
    kAnalysisAirEqParamId,
    kAnalysisCompressionParamId,
    kAnalysisNoiseRejectParamId,
    kAnalysisSpectralBalanceParamId,
    kAnalysisBlendParamId,
    kBankResonanceParamId,
    kBankDriveParamId,
    kBandShiftParamId,
    kBandStretchParamId,
    kBandTiltParamId,
    kSibilanceParamId,
    kConsonantColorParamId,
    kConsonantSpeedParamId,
    kHfDetailModeParamId,
    kHfDetailLevelParamId,
    kHfDetailCutoffParamId,
    kStereoModeParamId,
    kBankStereoSpreadParamId,
    kBankFreezeParamId,
    kFreezeTriggerParamId,
    kBankBlurParamId,
    kBankGestureFollowParamId,
    kVoicedTransitionParamId,
    kUnvoicedTransitionParamId,
};

constexpr clap_id kFormantCarrierParams[]{
    kModulatorSourceParamId,    kMicGainParamId,
    kCarrierPitchSourceParamId, kPitchScaleRootParamId,
    kPitchScaleParamId,         kPitchHoldParamId,
    kCarrierShapeParamId,       kCarrierHarmonicsParamId,
    kCarrierDensityParamId,     kCarrierColorParamId,
    kCarrierNoiseParamId,       kCarrierPulseWidthParamId,
    kCarrierLfoShapeParamId,    kCarrierLfoRateParamId,
    kCarrierLfoDepthParamId,    kCarrierPwmDepthParamId,
    kCarrierLfoSyncParamId,     kCarrierLfoDivisionParamId,
};

constexpr clap_id kFormantPhraseParams[]{
    kGestureSequenceParamId, kGestureRateParamId,  kGestureDepthParamId,
    kGestureLoopParamId,     kGestureSyncParamId,  kGestureDivisionParamId,
    kAuditionParamId,        kDeliveryParamId,     kVowelParamId,
    kOnsetParamId,           kDurationParamId,     kArticulationParamId,
    kConsonantParamId,       kIntensityParamId,    kWaveguideBlendParamId,
    kCoarticulationParamId,  kHybridBlendParamId,  kOnsetGuardParamId,
    kTractParamId,           kBreathParamId,       kOpenQuotientParamId,
    kHarshnessParamId,       kFalseFoldParamId,    kThroatParamId,
    kRoughnessParamId,       kBrightnessParamId,   kChestParamId,
    kNasalParamId,           kPolyphonyParamId,    kDoubleAmountParamId,
    kDoubleDetuneParamId,    kDoubleTimingParamId, kDoubleDirtParamId,
    kDoubleWidthParamId,     kAttackParamId,       kReleaseParamId,
    kRetriggerParamId,
};

constexpr clap_id kFormantFxParams[]{
    // Left: post-bank character. Right: the complete tape path. Output is
    // primary-page-only so it stays reachable without appearing twice.
    kCompressionParamId,  kParallelCrushParamId, kDeEssParamId,
    kWidthParamId,        kOctaveDownParamId,    kOctaveUpParamId,
    kFuzzDriveParamId,    kFuzzMixParamId,       kFuzzToneParamId,
    kEchoHeadsParamId,    kEchoClockParamId,     kEchoTimeParamId,
    kEchoFeedbackParamId, kEchoWearParamId,      kEchoFlutterParamId,
    kEchoToneParamId,     kEchoSpreadParamId,    kEchoMixParamId,
};

template <size_t N>
constexpr bool formantUniquePageParams(const clap_id (&params)[N]) {
  for (size_t first = 0u; first < N; ++first) {
    for (size_t second = first + 1u; second < N; ++second) {
      if (params[first] == params[second])
        return false;
    }
  }
  return true;
}

template <size_t N, size_t M>
constexpr bool formantDisjointPageParams(const clap_id (&left)[N],
                                         const clap_id (&right)[M]) {
  for (clap_id leftId : left) {
    for (clap_id rightId : right) {
      if (leftId == rightId)
        return false;
    }
  }
  return true;
}

template <size_t N>
constexpr bool formantPageContains(const clap_id (&params)[N], clap_id id) {
  for (clap_id candidate : params) {
    if (candidate == id)
      return true;
  }
  return false;
}

#define S3G_ASSERT_FORMANT_DISJOINT(left, right)                               \
  static_assert(formantDisjointPageParams(left, right),                        \
                "Formant Matrix parameter appears on multiple pages")

static_assert(formantUniquePageParams(kFormantRouteMatrixParams));
static_assert(formantUniquePageParams(kFormantRouteSignalParams));
static_assert(formantUniquePageParams(kFormantRouteEnvelopeParams));
static_assert(formantUniquePageParams(kFormantBankParams));
static_assert(formantUniquePageParams(kFormantCarrierParams));
static_assert(formantUniquePageParams(kFormantPhraseParams));
static_assert(formantUniquePageParams(kFormantFxParams));
S3G_ASSERT_FORMANT_DISJOINT(kFormantRouteMatrixParams,
                            kFormantRouteSignalParams);
S3G_ASSERT_FORMANT_DISJOINT(kFormantRouteMatrixParams,
                            kFormantRouteEnvelopeParams);
S3G_ASSERT_FORMANT_DISJOINT(kFormantRouteSignalParams,
                            kFormantRouteEnvelopeParams);
S3G_ASSERT_FORMANT_DISJOINT(kFormantRouteMatrixParams, kFormantBankParams);
S3G_ASSERT_FORMANT_DISJOINT(kFormantRouteMatrixParams, kFormantCarrierParams);
S3G_ASSERT_FORMANT_DISJOINT(kFormantRouteMatrixParams, kFormantPhraseParams);
S3G_ASSERT_FORMANT_DISJOINT(kFormantRouteMatrixParams, kFormantFxParams);
S3G_ASSERT_FORMANT_DISJOINT(kFormantRouteSignalParams, kFormantBankParams);
S3G_ASSERT_FORMANT_DISJOINT(kFormantRouteSignalParams, kFormantCarrierParams);
S3G_ASSERT_FORMANT_DISJOINT(kFormantRouteSignalParams, kFormantPhraseParams);
S3G_ASSERT_FORMANT_DISJOINT(kFormantRouteSignalParams, kFormantFxParams);
S3G_ASSERT_FORMANT_DISJOINT(kFormantRouteEnvelopeParams, kFormantBankParams);
S3G_ASSERT_FORMANT_DISJOINT(kFormantRouteEnvelopeParams, kFormantCarrierParams);
S3G_ASSERT_FORMANT_DISJOINT(kFormantRouteEnvelopeParams, kFormantPhraseParams);
S3G_ASSERT_FORMANT_DISJOINT(kFormantRouteEnvelopeParams, kFormantFxParams);
S3G_ASSERT_FORMANT_DISJOINT(kFormantBankParams, kFormantCarrierParams);
S3G_ASSERT_FORMANT_DISJOINT(kFormantBankParams, kFormantPhraseParams);
S3G_ASSERT_FORMANT_DISJOINT(kFormantBankParams, kFormantFxParams);
S3G_ASSERT_FORMANT_DISJOINT(kFormantCarrierParams, kFormantPhraseParams);
S3G_ASSERT_FORMANT_DISJOINT(kFormantCarrierParams, kFormantFxParams);
S3G_ASSERT_FORMANT_DISJOINT(kFormantPhraseParams, kFormantFxParams);
static_assert(!formantPageContains(kFormantRouteMatrixParams, kOutputParamId));
static_assert(!formantPageContains(kFormantRouteSignalParams, kOutputParamId));
static_assert(!formantPageContains(kFormantRouteEnvelopeParams,
                                   kOutputParamId));
static_assert(!formantPageContains(kFormantBankParams, kOutputParamId));
static_assert(!formantPageContains(kFormantCarrierParams, kOutputParamId));
static_assert(!formantPageContains(kFormantPhraseParams, kOutputParamId));
static_assert(!formantPageContains(kFormantFxParams, kOutputParamId));

#undef S3G_ASSERT_FORMANT_DISJOINT

struct FormantParamSpan {
  const clap_id *data = nullptr;
  uint32_t count = 0u;
};

FormantParamSpan formantParamsForPage(uint32_t page) {
  switch (page) {
  case kFormantPageBank:
    return {kFormantBankParams,
            static_cast<uint32_t>(std::size(kFormantBankParams))};
  case kFormantPageCarrier:
    return {kFormantCarrierParams,
            static_cast<uint32_t>(std::size(kFormantCarrierParams))};
  case kFormantPagePhrase:
    return {kFormantPhraseParams,
            static_cast<uint32_t>(std::size(kFormantPhraseParams))};
  case kFormantPageFx:
    return {kFormantFxParams,
            static_cast<uint32_t>(std::size(kFormantFxParams))};
  default:
    return {};
  }
}

drawing::Rect formantPageButtonRect(uint32_t page) {
  return drawing::makeRect(20.0 + static_cast<double>(page) * 92.0, 44.0, 86.0,
                           20.0);
}

drawing::Rect formantSceneButtonRect(bool sceneB) {
  return drawing::makeRect(sceneB ? 704.0 : 666.0, 44.0, 32.0, 20.0);
}

drawing::Rect formantSceneOperationRect(uint32_t operation) {
  constexpr double widths[]{82.0, 82.0, 72.0, 112.0};
  double x = 750.0;
  for (uint32_t index = 0u; index < operation; ++index) {
    x += widths[index] + 8.0;
  }
  return drawing::makeRect(x, 44.0, widths[operation], 20.0);
}

double formantMatrixPitch(uint32_t activeBands) {
  return kFormantMatrixExtent /
         static_cast<double>(std::max<uint32_t>(1u, activeBands - 1u));
}

drawing::Rect formantMatrixCellRect(uint32_t activeBands, uint32_t destination,
                                    uint32_t source) {
  const double pitch = formantMatrixPitch(activeBands);
  return drawing::makeRect(
      kFormantMatrixFirstX + static_cast<double>(source) * pitch - 10.0,
      kFormantMatrixFirstY + static_cast<double>(destination) * pitch - 10.0,
      20.0, 20.0);
}

drawing::Rect formantBandTrimHitRect(uint32_t activeBands, uint32_t band) {
  const double pitch = formantMatrixPitch(activeBands);
  return drawing::makeRect(kFormantMatrixFirstX +
                               static_cast<double>(band) * pitch - 10.0,
                           738.0, 20.0, 108.0);
}

drawing::Rect formantCrosspointActionRect(uint32_t action) {
  return drawing::makeRect(924.0 + static_cast<double>(action) * 104.0, 232.0,
                           94.0, 20.0);
}

drawing::Rect formantCompileRect() {
  return drawing::makeRect(1234.0, 106.0, 92.0, 24.0);
}

drawing::Rect formantScaleMenuRect() {
  const uint32_t rows = drawing::multiColumnMenuRows(
      s3g::kAcapellaResonatorPitchScaleCount, kFormantScaleMenuColumns);
  return drawing::makeRect(218.0, 238.0, 920.0,
                           kFormantMenuItemHeight * static_cast<double>(rows));
}

drawing::String const *formantScaleMenuItems() {
  static const std::array<drawing::String,
                          s3g::kAcapellaResonatorPitchScaleCount>
      items = [] {
        std::array<drawing::String, s3g::kAcapellaResonatorPitchScaleCount>
            result{};
        result[0] = "CONTINUOUS";
        for (uint32_t menuIndex = 0u; menuIndex < s3g::kMusicalScaleCount;
             ++menuIndex) {
          const uint32_t sharedScale =
              s3g::musicalScaleValueForMenuIndex(menuIndex);
          result[menuIndex + 1u] = drawing::stringFromUtf8(
              s3g::musicalScaleDefinition(sharedScale).name);
        }
        return result;
      }();
  return items.data();
}

int formantScaleMenuSelectedIndex(double value) {
  const uint32_t scaleValue =
      std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                         s3g::kAcapellaResonatorPitchScaleCount - 1u);
  if (scaleValue == 0u)
    return 0;
  const auto scale = static_cast<s3g::AcapellaResonatorPitchScale>(scaleValue);
  return static_cast<int>(s3g::musicalScaleMenuIndexForValue(
                              s3g::acapellaResonatorSharedScale(scale)) +
                          1u);
}

double formantScaleValueForMenuIndex(uint32_t menuIndex) {
  if (menuIndex == 0u)
    return 0.0;
  const uint32_t sharedScale = s3g::musicalScaleValueForMenuIndex(
      std::min<uint32_t>(menuIndex - 1u, s3g::kMusicalScaleCount - 1u));
  return static_cast<double>(
      s3g::acapellaResonatorPitchScaleValue(sharedScale));
}

drawing::String formantShortLabel(clap_id id) {
  switch (id) {
  case kBankAmountParamId:
    return "BANK";
  case kBankModeParamId:
    return "MODE";
  case kBandLayoutParamId:
    return "LAYOUT";
  case kAnalysisSlopeParamId:
    return "RESPONSE";
  case kAnalysisWidthParamId:
    return "AN WIDTH";
  case kTransferModeParamId:
    return "TRANSFER";
  case kVoiceFocusParamId:
    return "VOICE FOCUS";
  case kAnalysisLevelerParamId:
    return "LEVELER";
  case kAnalysisLowEqParamId:
    return "LOW EQ";
  case kAnalysisMidEqParamId:
    return "MID EQ";
  case kAnalysisAirEqParamId:
    return "AIR EQ";
  case kAnalysisCompressionParamId:
    return "AN COMP";
  case kAnalysisNoiseRejectParamId:
    return "NOISE REJ";
  case kAnalysisSpectralBalanceParamId:
    return "SPEC BAL";
  case kAnalysisBlendParamId:
    return "AN / PH";
  case kBankAttackParamId:
    return "ATTACK";
  case kBankReleaseParamId:
    return "RELEASE";
  case kBankResonanceParamId:
    return "RESON";
  case kBankDriveParamId:
    return "DRIVE";
  case kBandShiftParamId:
    return "SHIFT";
  case kBandStretchParamId:
    return "STRETCH";
  case kBandTiltParamId:
    return "TILT";
  case kSibilanceParamId:
    return "CONS LEVEL";
  case kConsonantColorParamId:
    return "CONS COLOR";
  case kConsonantSpeedParamId:
    return "CONS SPEED";
  case kHfDetailModeParamId:
    return "HF MODE";
  case kHfDetailLevelParamId:
    return "HF LEVEL";
  case kHfDetailCutoffParamId:
    return "HF CUTOFF";
  case kMatrixModeParamId:
    return "MAP";
  case kMatrixMorphParamId:
    return "DEPTH";
  case kCustomMatrixMorphParamId:
    return "A / B";
  case kBankStereoSpreadParamId:
    return "SPREAD";
  case kBankFreezeParamId:
    return "FREEZE";
  case kFreezeTriggerParamId:
    return "TRIGGER";
  case kBankBlurParamId:
    return "BLUR";
  case kBankGestureFollowParamId:
    return "FOLLOW";
  case kVoicingModeParamId:
    return "V / UV";
  case kVoicingThresholdParamId:
    return "THRESH";
  case kVoicedLevelParamId:
    return "VOICED";
  case kUnvoicedLevelParamId:
    return "UNVOICE";
  case kVoicedTransitionParamId:
    return "TO V";
  case kUnvoicedTransitionParamId:
    return "TO UV";
  case kOpenLevelParamId:
    return "OPEN";
  case kCouplingParamId:
    return "COUPLE";
  case kArticulationThruParamId:
    return "ART";
  case kStereoModeParamId:
    return "STEREO";
  case kModulatorSourceParamId:
    return "MOD SOURCE";
  case kMicGainParamId:
    return "MIC GAIN";
  case kCarrierPitchSourceParamId:
    return "PITCH SRC";
  case kPitchScaleRootParamId:
    return "ROOT";
  case kPitchScaleParamId:
    return "SCALE";
  case kPitchHoldParamId:
    return "PITCH HOLD";
  case kCarrierPulseWidthParamId:
    return "PULSE W";
  case kCarrierLfoShapeParamId:
    return "LFO SHAPE";
  case kCarrierLfoRateParamId:
    return "LFO RATE";
  case kCarrierLfoDepthParamId:
    return "FM";
  case kCarrierPwmDepthParamId:
    return "PWM";
  case kCarrierLfoSyncParamId:
    return "LFO SYNC";
  case kCarrierLfoDivisionParamId:
    return "DIV";
  case kOutputParamId:
    return "OUT";
  case kCompressionParamId:
    return "COMP";
  case kParallelCrushParamId:
    return "CRUSH";
  case kDeEssParamId:
    return "DE-ESS";
  case kWidthParamId:
    return "WIDTH";
  case kOctaveDownParamId:
    return "OCT DOWN";
  case kOctaveUpParamId:
    return "OCT UP";
  case kFuzzDriveParamId:
    return "FUZZ DRV";
  case kFuzzMixParamId:
    return "FUZZ MIX";
  case kFuzzToneParamId:
    return "FUZZ TONE";
  case kEchoHeadsParamId:
    return "HEADS";
  case kEchoClockParamId:
    return "CLOCK";
  case kEchoTimeParamId:
    return "TIME";
  case kEchoFeedbackParamId:
    return "FEEDBACK";
  case kEchoWearParamId:
    return "WEAR";
  case kEchoFlutterParamId:
    return "FLUTTER";
  case kEchoToneParamId:
    return "TONE";
  case kEchoSpreadParamId:
    return "SPREAD";
  case kEchoMixParamId:
    return "MIX";
  case kCarrierShapeParamId:
    return "SHAPE";
  case kCarrierHarmonicsParamId:
    return "HARMONICS";
  case kCarrierDensityParamId:
    return "DENSITY";
  case kCarrierColorParamId:
    return "COLOR";
  case kCarrierNoiseParamId:
    return "NOISE";
  case kIntelligibilityParamId:
    return "DEFINITION";
  case kMouthFocusParamId:
    return "MOUTH FOCUS";
  default: {
    const ParamDef *def = paramDef(id);
    if (!def)
      return "";
    drawing::String label =
        drawing::uppercaseString(drawing::stringFromUtf8(def->name));
    // The processor row reserves a compact label lane before the slider.
    // Bound every fallback label so a future long host name cannot overlap
    // the track or value readout.
    return label.length() > 10u ? label.substr(0, 10u) : label;
  }
  }
}

drawing::String formantPhraseString(const PhraseState &phrase) {
  return std::string(phrase.text.data(), phrase.length);
}

bool queueFormantParamValue(Plugin &plugin, clap_id id, double value) {
  // queueGuiParamValue publishes locally before its three-event host gesture.
  // This view has one serialized producer, so a capacity preflight guarantees
  // that both sides of the edit remain coherent; the consumer can only free
  // additional slots between this check and publication.
  if (plugin.guiParamEvents.available() < 3u)
    return false;
  queueGuiParamValue(plugin, id, value);
  return true;
}

const char *formantPhonemeLabel(s3g::AcapellaPhoneme phoneme) {
  constexpr const char *labels[s3g::kAcapellaPhonemeCount]{
      "-",  "IY", "IH", "EH", "AE", "AA", "AO", "UH", "UW", "AH", "AX", "ER",
      "P",  "B",  "T",  "D",  "K",  "G",  "F",  "V",  "TH", "DH", "S",  "Z",
      "SH", "ZH", "HH", "CH", "JH", "M",  "N",  "NG", "L",  "R",  "W",  "Y",
  };
  return labels[std::min<uint32_t>(static_cast<uint32_t>(phoneme),
                                   s3g::kAcapellaPhonemeCount - 1u)];
}

void drawFormantActionButton(drawing::Rect rect, drawing::String title,
                             bool active, const drawing::Style &style) {
  drawing::drawHeaderButton(rect, rect, title, active,
                            drawing::softLabelAttrs(), style);
}

bool formantRouteControlLocation(clap_id id,
                                 const s3g::gui_layout::Panel *&panel,
                                 uint32_t &row) {
  if (id == kOutputParamId) {
    panel = &kFormantOutputPanel;
    row = 0u;
    return true;
  }
  struct Group {
    const clap_id *ids;
    uint32_t count;
    const s3g::gui_layout::Panel *panel;
  };
  constexpr Group groups[]{
      {kFormantRouteMatrixParams,
       static_cast<uint32_t>(std::size(kFormantRouteMatrixParams)),
       &kFormantMatrixControlPanel},
      {kFormantRouteSignalParams,
       static_cast<uint32_t>(std::size(kFormantRouteSignalParams)),
       &kFormantSignalPanel},
      {kFormantRouteEnvelopeParams,
       static_cast<uint32_t>(std::size(kFormantRouteEnvelopeParams)),
       &kFormantEnvelopePanel},
  };
  for (const auto &group : groups) {
    for (uint32_t index = 0u; index < group.count; ++index) {
      if (group.ids[index] != id)
        continue;
      panel = group.panel;
      row = index;
      return true;
    }
  }
  return false;
}

bool formantGenericControlLocation(uint32_t page, clap_id id,
                                   const s3g::gui_layout::Panel *&panel,
                                   uint32_t &row) {
  const FormantParamSpan params = formantParamsForPage(page);
  if (!params.data || params.count == 0u)
    return false;
  const uint32_t leftCount = (params.count + 1u) / 2u;
  for (uint32_t index = 0u; index < params.count; ++index) {
    if (params.data[index] != id)
      continue;
    if (page == kFormantPagePhrase) {
      panel = index < leftCount ? &kFormantPhraseLeftPanel
                                : &kFormantPhraseRightPanel;
    } else {
      panel = index < leftCount ? &kFormantGenericLeftPanel
                                : &kFormantGenericRightPanel;
    }
    row = index < leftCount ? index : index - leftCount;
    return true;
  }
  return false;
}

drawing::Rect formantParamHitRect(const s3g::gui_layout::Panel &panel,
                                  uint32_t row, bool stepped) {
  if (stepped) {
    return drawing::makeRect(
        s3g::gui_layout::processorControlX(panel.frame.x),
        s3g::gui_layout::rowY(panel, row) - 1.0,
        s3g::gui_layout::processorMenuWidth(panel.frame.width), 15.0);
  }
  return drawing::cocoaRect(s3g::gui_layout::sliderHitRect(panel, row));
}

class Editor final : public s3g::portable_gui::routing::Canvas {
public:
  using Canvas = s3g::portable_gui::routing::Canvas;

  Plugin *_plugin;
  uint32_t _page;
  bool _editingSceneB;
  uint32_t _selectedSource;
  uint32_t _selectedDestination;
  clap_id _dragParam;
  bool _dragVertical;
  double _dragTrackStart;
  double _dragTrackLength;
  clap_id _openMenu;
  int _hoverMenuItem;
  drawing::Point _menuOrigin;
  double _menuWidth;
  uint32_t _randomSeed;
  char _titlePresetName[64];
#include "s3g_complex_processor_formant_editor.inc"
  explicit Editor(Plugin &plugin) : Canvas(kGuiWidth, kGuiHeight) {
    _plugin = &plugin;
    _page = kFormantPageRoute;
    _editingSceneB = false;
    _selectedSource = 0u;
    _selectedDestination = 0u;
    _dragParam = CLAP_INVALID_ID;
    _dragVertical = false;
    _dragTrackStart = 0.0;
    _dragTrackLength = 1.0;
    _openMenu = CLAP_INVALID_ID;
    _hoverMenuItem = -1;
    _menuOrigin = drawing::Point{};
    _menuWidth = 180.0;
    _randomSeed = 0x6d2b79f5u;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "INIT");
  }

  uint32_t activeMatrixBands() {
    return loadValue(*_plugin, kBandLayoutParamId) >= 0.5 ? 16u : kMatrixBands;
  }

  void clampMatrixSelection() {
    const uint32_t activeBands = activeMatrixBands();
    _selectedSource = std::min(_selectedSource, activeBands - 1u);
    _selectedDestination = std::min(_selectedDestination, activeBands - 1u);
  }

  void finishDrag() {
    if (_dragParam != CLAP_INVALID_ID && _plugin) {
      endGuiParamGesture(*_plugin, _dragParam);
    }
    _dragParam = CLAP_INVALID_ID;
  }

  void drawPanel_panel_style(drawing::String title,
                             const s3g::gui_layout::Panel &panel,
                             const drawing::Style &style) {
    drawing::drawPanelFrame(panel, style);
    drawing::drawPanelHeader(title, true, panel, drawing::softLabelAttrs(),
                             style);
  }

  void drawParam_row_panel_style(clap_id id, uint32_t row,
                                 const s3g::gui_layout::Panel &panel,
                                 const drawing::Style &style) {
    const ParamDef *def = paramDef(id);
    if (!def)
      return;
    const double value = loadValue(*_plugin, id);
    char text[64]{};
    paramsValueToText(&_plugin->plugin, id, value, text, sizeof(text));
    drawing::String display =
        drawing::uppercaseString(drawing::stringFromUtf8(text));
    const double y = s3g::gui_layout::rowY(panel, row);
    if (def->stepped) {
      drawing::drawProcessorMenu(
          formantShortLabel(id), display, y, panel.frame.x, panel.frame.width,
          drawing::softLabelAttrs(), drawing::softValueAttrs(), style);
    } else {
      const double norm = static_cast<double>(
          std::clamp((value - def->minimum) /
                         std::max(1.0e-9, def->maximum - def->minimum),
                     0.0, 1.0));
      drawing::drawProcessorSlider(formantShortLabel(id), display, norm, y,
                                   panel.frame.x, panel.frame.width,
                                   drawing::softLabelAttrs(),
                                   drawing::softValueAttrs(), style);
    }
  }

  void drawPageBand(const drawing::Style &style) {
    drawing::setFill(style.strip);
    drawing::rectFill(drawing::makeRect(12.0, 42.0, 1332.0, 24.0));
    for (uint32_t page = 0u; page < kFormantPageCount; ++page) {
      drawFormantActionButton(formantPageButtonRect(page),
                              drawing::stringFromUtf8(kFormantPageNames[page]),
                              _page == page, style);
    }
    if (_page != kFormantPageRoute)
      return;
    drawFormantActionButton(formantSceneButtonRect(false), "A", !_editingSceneB,
                            style);
    drawFormantActionButton(formantSceneButtonRect(true), "B", _editingSceneB,
                            style);
    drawFormantActionButton(formantSceneOperationRect(0u), "IDENTITY", false,
                            style);
    drawFormantActionButton(formantSceneOperationRect(1u), "RANDOM", false,
                            style);
    drawFormantActionButton(formantSceneOperationRect(2u), "CLEAR", false,
                            style);
    drawFormantActionButton(formantSceneOperationRect(3u),
                            _editingSceneB ? "COPY B > A" : "COPY A > B", false,
                            style);
  }

  void drawMeter_level_color_style_vertical(drawing::Rect rect, double level,
                                            VSTGUI::CColor color,
                                            const drawing::Style &style,
                                            bool vertical) {
    drawing::setFill(drawing::color(0x090909));
    drawing::rectFill(rect);
    drawing::setStroke(style.grid);
    drawing::frameRect(rect);
    level =
        std::clamp(level, static_cast<double>(0.0), static_cast<double>(1.0));
    drawing::Rect fill = drawing::inset(rect, 1.0, 1.0);
    drawing::setFill(color);
    if (vertical) {
      const double height = fill.size.height * level;
      fill.origin.y += fill.size.height - height;
      fill.size.height = height;
    } else {
      fill.size.width *= level;
    }
    if (fill.size.width > 0.0 && fill.size.height > 0.0)
      drawing::rectFill(fill);
  }

  void drawRoutePage(const drawing::Style &style) {
    drawing::Attrs labels = drawing::softLabelAttrs();
    drawing::Attrs values = drawing::softValueAttrs();
    clampMatrixSelection();
    const uint32_t activeBands = activeMatrixBands();
    const double matrixPitch = formantMatrixPitch(activeBands);
    const bool voicePitch =
        loadValue(*_plugin, kCarrierPitchSourceParamId) >= 0.5;
    drawPanel_panel_style(
        drawing::stringFormat(
            "ROUTING %c // %u BAND // MODULATOR ANALYSIS > %@ CARRIER",
            _editingSceneB ? 'B' : 'A', activeBands,
            voicePitch ? "VOICE-PITCH" : "MIDI"),
        kFormantRoutePanel, style);
    drawPanel_panel_style("OUTPUT", kFormantOutputPanel, style);
    drawPanel_panel_style(drawing::stringFormat("CROSSPOINT // A%02u > S%02u",
                                                _selectedSource + 1u,
                                                _selectedDestination + 1u),
                          kFormantCrosspointPanel, style);
    drawPanel_panel_style("MATRIX", kFormantMatrixControlPanel, style);
    drawPanel_panel_style("SIGNAL FLOW", kFormantSignalPanel, style);
    drawPanel_panel_style("ENVELOPES", kFormantEnvelopePanel, style);

    drawParam_row_panel_style(kOutputParamId, 0u, kFormantOutputPanel, style);
    for (uint32_t row = 0u; row < std::size(kFormantRouteMatrixParams); ++row) {
      drawParam_row_panel_style(kFormantRouteMatrixParams[row], row,
                                kFormantMatrixControlPanel, style);
    }
    for (uint32_t row = 0u; row < std::size(kFormantRouteSignalParams); ++row) {
      drawParam_row_panel_style(kFormantRouteSignalParams[row], row,
                                kFormantSignalPanel, style);
    }
    for (uint32_t row = 0u; row < std::size(kFormantRouteEnvelopeParams);
         ++row) {
      drawParam_row_panel_style(kFormantRouteEnvelopeParams[row], row,
                                kFormantEnvelopePanel, style);
    }

    const clap_id selectedId =
        matrixParamId(_editingSceneB, _selectedDestination, _selectedSource);
    const double selected = loadValue(*_plugin, selectedId);
    drawing::drawProcessorSlider(
        "GAIN", drawing::stringFormat("%+.3f", selected),
        static_cast<double>((selected + 1.0) * 0.5),
        s3g::gui_layout::rowY(kFormantCrosspointPanel, 0u),
        kFormantCrosspointPanel.frame.x, kFormantCrosspointPanel.frame.width,
        labels, values, style);
    drawFormantActionButton(formantCrosspointActionRect(0u),
                            selected < 0.0 ? "NEGATIVE" : "POSITIVE",
                            std::abs(selected) > 0.001, style);
    drawFormantActionButton(formantCrosspointActionRect(1u), "CLEAR", false,
                            style);

    const int modulatorSource = static_cast<int>(
        std::lround(loadValue(*_plugin, kModulatorSourceParamId)));
    drawing::String selectedAnalysisLabel =
        modulatorSource == 0
            ? "SELECTED ANALYSIS // MIC"
            : modulatorSource == 2 ? "SELECTED ANALYSIS // BLEND"
                                   : "SELECTED ANALYSIS // SPEECH";
    drawing::drawAtPoint_withAttributes(
        selectedAnalysisLabel, drawing::makePoint(28.0, 116.0), labels);
    drawing::drawAtPoint_withAttributes(
        "MODULATOR BAND", drawing::makePoint(366.0, 98.0), labels);
    drawing::drawAtPoint_withAttributes(
        (voicePitch ? "VOICE-PITCH CARRIER / MAPPED" : "MIDI CARRIER / MAPPED"),
        drawing::makePoint(698.0, 116.0), labels);
    drawing::setStroke(style.grid);
    const double lastX = kFormantMatrixFirstX +
                         static_cast<double>(activeBands - 1u) * matrixPitch;
    const double lastY = kFormantMatrixFirstY +
                         static_cast<double>(activeBands - 1u) * matrixPitch;
    for (uint32_t band = 0u; band < activeBands; ++band) {
      const double x =
          kFormantMatrixFirstX + static_cast<double>(band) * matrixPitch;
      const double y =
          kFormantMatrixFirstY + static_cast<double>(band) * matrixPitch;
      drawing::Path::strokeLineFromPoint_toPoint(
          drawing::makePoint(x, kFormantMatrixFirstY),
          drawing::makePoint(x, lastY));
      drawing::Path::strokeLineFromPoint_toPoint(
          drawing::makePoint(kFormantMatrixFirstX, y),
          drawing::makePoint(lastX, y));
      const double analysisLinear =
          _plugin->analysisBandMeters[band].load(std::memory_order_relaxed);
      const double synthesisLinear =
          _plugin->synthesisBandMeters[band].load(std::memory_order_relaxed);
      const double analysis =
          s3g::acapellaResonatorMeterDisplayLevel(analysisLinear);
      const double synthesis =
          s3g::acapellaResonatorMeterDisplayLevel(synthesisLinear);
      drawMeter_level_color_style_vertical(
          drawing::makeRect(x - 4.0, 122.0, 8.0, 38.0), analysis,
          drawing::color(0xc67b52), style, true);
      drawMeter_level_color_style_vertical(
          drawing::makeRect(700.0, y - 4.0, 62.0, 8.0), synthesis,
          drawing::color(0x70b4bb), style, false);
      drawing::String number = drawing::stringFormat("%u", band + 1u);
      drawing::drawAtPoint_withAttributes(
          number, drawing::makePoint(x - 6.0, 162.0), values);
      drawing::drawAtPoint_withAttributes(
          number, drawing::makePoint(112.0, y - 6.0), values);
    }

    for (uint32_t destination = 0u; destination < activeBands; ++destination) {
      const double synthesis = s3g::acapellaResonatorMeterDisplayLevel(
          _plugin->synthesisBandMeters[destination].load(
              std::memory_order_relaxed));
      for (uint32_t source = 0u; source < activeBands; ++source) {
        const double route = loadValue(
            *_plugin, matrixParamId(_editingSceneB, destination, source));
        const double analysis = s3g::acapellaResonatorMeterDisplayLevel(
            _plugin->analysisBandMeters[source].load(
                std::memory_order_relaxed));
        const drawing::Rect hit =
            formantMatrixCellRect(activeBands, destination, source);
        const drawing::Rect node = drawing::makeRect(
            drawing::midX(hit) - 5.0, drawing::midY(hit) - 5.0, 10.0, 10.0);
        const bool selectedCell =
            destination == _selectedDestination && source == _selectedSource;
        drawing::setFill(drawing::color(0x111111));
        drawing::rectFill(node);
        if (std::abs(route) > 0.001) {
          const double activity =
              std::sqrt(std::max<double>(0.0, analysis * synthesis));
          const double size = std::clamp<double>(
              2.0 + std::abs(route) * 6.0 + activity * 3.0, 2.0, 9.0);
          drawing::setFill((route >= 0.0 ? drawing::color(0xd47a4d)
                                         : drawing::color(0x62b7c1)));
          drawing::rectFill(drawing::makeRect(drawing::midX(node) - size * 0.5,
                                              drawing::midY(node) - size * 0.5,
                                              size, size));
        }
        drawing::setStroke((selectedCell ? drawing::color(0xe0e0e0)
                                         : drawing::color(0x4c4c4c)));
        drawing::frameRect(node);
      }
    }

    drawing::drawAtPoint_withAttributes(
        "OUTPUT TRIM", drawing::makePoint(94.0, 718.0), labels);
    for (uint32_t band = 0u; band < activeBands; ++band) {
      const double x =
          kFormantMatrixFirstX + static_cast<double>(band) * matrixPitch;
      const double trim = loadValue(*_plugin, bandTrimParamId(band));
      const double norm = static_cast<double>(trim * 0.5);
      const drawing::Rect track = drawing::makeRect(x - 3.0, 748.0, 6.0, 82.0);
      drawing::setFill(drawing::color(0x0a0a0a));
      drawing::rectFill(track);
      drawing::setStroke(style.grid);
      drawing::frameRect(track);
      const double handleY = track.origin.y + track.size.height * (1.0 - norm);
      drawing::setFill(drawing::color(0x70b4bb));
      drawing::rectFill(drawing::makeRect(x - 6.0, handleY - 2.0, 12.0, 4.0));
      drawing::drawAtPoint_withAttributes(
          drawing::stringFormat("%u", band + 1u),
          drawing::makePoint(x - 6.0, 836.0), values);
    }
    drawing::drawAtPoint_withAttributes(
        "CLICK: CREATE / SELECT · CLICK SELECTED: CLEAR · OPTION: NEGATIVE",
        drawing::makePoint(30.0, 918.0), values);

    const double micPeak = s3g::acapellaResonatorMeterDisplayLevel(
        _plugin->externalMicPeak.load(std::memory_order_relaxed));
    const double speechPeak = s3g::acapellaResonatorMeterDisplayLevel(
        _plugin->internalSpeechPeak.load(std::memory_order_relaxed));
    drawing::String micLabel =
        modulatorSource == 0 ? "MIC [SELECTED]"
                             : modulatorSource == 2 ? "MIC [BLEND]" : "MIC";
    drawing::String speechLabel =
        modulatorSource == 1 ? "INTERNAL SPEECH [SELECTED]"
                             : modulatorSource == 2 ? "INTERNAL SPEECH [BLEND]"
                                                    : "INTERNAL SPEECH";
    const double sourceMeterY =
        s3g::gui_layout::rowY(kFormantSignalPanel, 2u) + 39.0;
    drawing::drawAtPoint_withAttributes(
        micLabel, drawing::makePoint(924.0, sourceMeterY), labels);
    drawMeter_level_color_style_vertical(
        drawing::makeRect(1072.0, sourceMeterY + 2.0, 252.0, 8.0), micPeak,
        drawing::color(0x87ad91), style, false);
    drawing::drawAtPoint_withAttributes(
        speechLabel, drawing::makePoint(924.0, sourceMeterY + 18.0), labels);
    drawMeter_level_color_style_vertical(
        drawing::makeRect(1072.0, sourceMeterY + 20.0, 252.0, 8.0), speechPeak,
        drawing::color(0xc67b52), style, false);
    if (voicePitch) {
      const bool pitchActive =
          _plugin->pitchActive.load(std::memory_order_relaxed);
      const float pitchHz =
          _plugin->detectedPitchHz.load(std::memory_order_relaxed);
      const float confidence =
          _plugin->pitchConfidence.load(std::memory_order_relaxed);
      drawing::String pitchText =
          pitchActive
              ? drawing::stringFormat("VOICE PITCH  %.1f Hz  //  %.0f%%",
                                      pitchHz, confidence * 100.0f)
              : "VOICE PITCH  LISTENING";
      drawing::drawAtPoint_withAttributes(
          pitchText, drawing::makePoint(924.0, sourceMeterY + 36.0), values);
    }
  }

  void drawGenericPage(const drawing::Style &style) {
    const FormantParamSpan params = formantParamsForPage(_page);
    if (!params.data || params.count == 0u)
      return;
    const uint32_t leftCount = (params.count + 1u) / 2u;
    const s3g::gui_layout::Panel &left = _page == kFormantPagePhrase
                                             ? kFormantPhraseLeftPanel
                                             : kFormantGenericLeftPanel;
    const s3g::gui_layout::Panel &right = _page == kFormantPagePhrase
                                              ? kFormantPhraseRightPanel
                                              : kFormantGenericRightPanel;
    drawing::String leftTitle = _page == kFormantPageBank
                                    ? "ANALYSIS / FILTER BANK"
                                    : _page == kFormantPageCarrier
                                          ? "MODULATOR / PITCH / CARRIER"
                                          : _page == kFormantPagePhrase
                                                ? "PHRASE / PERFORMANCE"
                                                : "DYNAMICS / CHARACTER";
    const bool voicePitch =
        loadValue(*_plugin, kCarrierPitchSourceParamId) >= 0.5;
    drawing::String rightTitle =
        _page == kFormantPageBank
            ? "STEREO / HOLD / TRANSITIONS"
            : _page == kFormantPageCarrier
                  ? (voicePitch ? "CARRIER DETAIL / VOICE-PITCH LFO"
                                : "CARRIER DETAIL / MIDI LFO")
                  : _page == kFormantPagePhrase ? "VOICE / ENSEMBLE"
                                                : "MULTI-HEAD TAPE";
    drawPanel_panel_style(leftTitle, left, style);
    drawPanel_panel_style(rightTitle, right, style);
    for (uint32_t index = 0u; index < params.count; ++index) {
      drawParam_row_panel_style(params.data[index],
                                index < leftCount ? index : index - leftCount,
                                index < leftCount ? left : right, style);
    }
  }

  void drawPhraseHeader(const drawing::Style &style) {
    drawPanel_panel_style("PROCEDURAL SPEECH // TEXT TO GESTURE",
                          kFormantPhraseInputPanel, style);
    drawFormantActionButton(formantCompileRect(), "COMPILE", false, style);
    const PhraseState phrase = loadPhrase(*_plugin);
    const auto compiled = s3g::compileAcapellaText(phrase.text.data());
    drawing::Attrs labels = drawing::softLabelAttrs();
    drawing::Attrs values = drawing::softValueAttrs();
    drawing::drawAtPoint_withAttributes(
        drawing::stringFormat(
            "%u WORDS // %u EVENTS%@", compiled.program.wordCount,
            compiled.program.count,
            compiled.program.truncated ? " // TRUNCATED" : ""),
        drawing::makePoint(28.0, 142.0), values);
    drawing::drawAtPoint_withAttributes("TEXT: NOTE / WORD + REST // WRAPS",
                                        drawing::makePoint(1032.0, 142.0),
                                        labels);
    constexpr uint32_t visibleSteps = 24u;
    constexpr double cellPitch = 52.0;
    for (uint32_t index = 0u; index < visibleSteps; ++index) {
      const drawing::Rect cell = drawing::makeRect(
          28.0 + static_cast<double>(index) * cellPitch, 166.0, 46.0, 34.0);
      const bool populated = index < compiled.program.count;
      drawing::setFill(drawing::color(populated ? 0x24282a : 0x151719));
      drawing::rectFill(cell);
      drawing::setStroke(drawing::color(populated ? 0x555b5e : 0x292c2e));
      drawing::frameRect(cell);
      if (!populated)
        continue;
      const auto &step = compiled.program.steps[index];
      const bool rest = (step.flags & s3g::kAcapellaForcedRest) != 0u;
      drawing::String symbol =
          rest ? "REST"
               : drawing::stringFromUtf8(formantPhonemeLabel(step.phoneme));
      const drawing::Size size = drawing::sizeWithAttributes(symbol, labels);
      drawing::drawAtPoint_withAttributes(
          symbol,
          drawing::makePoint(drawing::midX(cell) - size.width * 0.5,
                             cell.origin.y + 9.0),
          labels);
    }
  }

  void drawOpenMenu(const drawing::Style &style) {
    const ParamDef *def = paramDef(_openMenu);
    if (!def || !def->stepped)
      return;
    if (_openMenu == kPitchScaleParamId) {
      drawing::drawMultiColumnDropdownMenu(
          formantScaleMenuRect(), kFormantMenuItemHeight,
          formantScaleMenuItems(), s3g::kAcapellaResonatorPitchScaleCount,
          kFormantScaleMenuColumns,
          formantScaleMenuSelectedIndex(
              loadValue(*_plugin, kPitchScaleParamId)),
          _hoverMenuItem, drawing::softValueAttrs(), style);
      return;
    }
    const uint32_t count = std::min<uint32_t>(
        32u,
        static_cast<uint32_t>(std::lround(def->maximum - def->minimum)) + 1u);
    std::array<drawing::String, 32u> items{};
    for (uint32_t index = 0u; index < count; ++index) {
      char text[64]{};
      paramsValueToText(&_plugin->plugin, def->id,
                        def->minimum + static_cast<double>(index), text,
                        sizeof(text));
      items[index] = drawing::uppercaseString(drawing::stringFromUtf8(text));
    }
    const drawing::Rect rect =
        drawing::makeRect(_menuOrigin.x, _menuOrigin.y, _menuWidth,
                          kFormantMenuItemHeight * static_cast<double>(count));
    const int selected = static_cast<int>(
        std::lround(loadValue(*_plugin, def->id) - def->minimum));
    drawing::drawDropdownMenu(rect, kFormantMenuItemHeight, items.data(), count,
                              selected, _hoverMenuItem,
                              drawing::softValueAttrs(), style);
  }

  void drawOriginal(drawing::Rect dirty) {
    (void)dirty;
    if (!_plugin)
      return;
    drawing::Style style;
    drawing::setFill(style.bg);
    drawing::rectFill(bounds());
    char preset[64]{};
    paramsValueToText(&_plugin->plugin, kPresetParamId,
                      loadValue(*_plugin, kPresetParamId), preset,
                      sizeof(preset));
    const auto titleBand = s3g::gui_layout::matrixTitleBand(kFormantCanvas);
    drawing::drawProcessorTitleBand(
        "s3g PROCESSOR FORMANT MATRIX",
        drawing::uppercaseString(drawing::stringFromUtf8(preset)),
        drawing::peakDbText(
            _plugin->outputPeak.load(std::memory_order_relaxed)),
        titleBand, drawing::softTitleAttrs(), drawing::softLabelAttrs(),
        drawing::softValueAttrs(), style);
    drawPageBand(style);
    if (_page == kFormantPageRoute)
      drawRoutePage(style);
    else {
      if (_page == kFormantPagePhrase)
        drawPhraseHeader(style);
      drawGenericPage(style);
    }
    if (_openMenu != CLAP_INVALID_ID)
      drawOpenMenu(style);
  }

  uint32_t openMenuItemCount() {
    const ParamDef *def = paramDef(_openMenu);
    if (def && def->id == kPitchScaleParamId)
      return s3g::kAcapellaResonatorPitchScaleCount;
    return def && def->stepped
               ? std::min<uint32_t>(32u, static_cast<uint32_t>(std::lround(
                                             def->maximum - def->minimum)) +
                                             1u)
               : 0u;
  }

  drawing::Rect openMenuRect() {
    if (_openMenu == kPitchScaleParamId)
      return formantScaleMenuRect();
    return drawing::makeRect(_menuOrigin.x, _menuOrigin.y, _menuWidth,
                             kFormantMenuItemHeight *
                                 static_cast<double>(openMenuItemCount()));
  }

  void openMenuForParam_box(clap_id id, drawing::Rect box) {
    const ParamDef *def = paramDef(id);
    if (!def || !def->stepped)
      return;
    if (phraseIsEditing()) {
      focus();
    }
    const double height =
        kFormantMenuItemHeight *
        static_cast<double>(std::min<uint32_t>(
            32u,
            static_cast<uint32_t>(std::lround(def->maximum - def->minimum)) +
                1u));
    const double below = drawing::maxY(box) + 3.0;
    _openMenu = id;
    _hoverMenuItem = -1;
    if (id == kPitchScaleParamId) {
      _menuOrigin = formantScaleMenuRect().origin;
      _menuWidth = formantScaleMenuRect().size.width;
      syncPhraseFieldVisibility();
      setNeedsDisplay(true);
      return;
    }
    _menuOrigin = drawing::makePoint(
        box.origin.x,
        below + height <= static_cast<double>(kGuiHeight) - 8.0
            ? below
            : std::max<double>(34.0, box.origin.y - height - 3.0));
    _menuWidth = box.size.width;
    syncPhraseFieldVisibility();
    setNeedsDisplay(true);
  }

  void updateMenuHover(drawing::Point point) {
    if (_openMenu == CLAP_INVALID_ID)
      return;
    const int hover =
        _openMenu == kPitchScaleParamId
            ? drawing::multiColumnDropdownHitIndex(
                  point, formantScaleMenuRect(), kFormantMenuItemHeight,
                  s3g::kAcapellaResonatorPitchScaleCount,
                  kFormantScaleMenuColumns)
            : drawing::dropdownHitIndex(point, openMenuRect(),
                                        kFormantMenuItemHeight,
                                        openMenuItemCount());
    if (hover == _hoverMenuItem)
      return;
    _hoverMenuItem = hover;
    setNeedsDisplay(true);
  }

  void updateDrag(drawing::Point point) {
    if (_dragParam == CLAP_INVALID_ID || !_plugin)
      return;
    const ParamDef *def = paramDef(_dragParam);
    if (!def || def->stepped)
      return;
    double norm = _dragVertical
                      ? 1.0 - (point.y - _dragTrackStart) / _dragTrackLength
                      : (point.x - _dragTrackStart) / _dragTrackLength;
    norm = std::clamp(norm, static_cast<double>(0.0), static_cast<double>(1.0));
    if (_plugin->guiParamEvents.available() <= 1u)
      return;
    if (!updateGuiParamGesture(*_plugin, _dragParam,
                               def->minimum +
                                   static_cast<double>(norm) *
                                       (def->maximum - def->minimum)))
      return;
    setNeedsDisplay(true);
  }

  void beginDrag_trackStart_trackLength_vertical_event_point(
      clap_id id, double start, double length, bool vertical,
      drawing::Event *event, drawing::Point point) {
    finishDrag();
    double reset = 0.0;
    if (drawing::sliderDoubleClickDefault(event, &_plugin->plugin, id,
                                          &reset)) {
      if (!queueFormantParamValue(*_plugin, id, reset))
        beep();
      setNeedsDisplay(true);
      return;
    }
    _dragParam = id;
    _dragVertical = vertical;
    _dragTrackStart = start;
    _dragTrackLength = std::max<double>(1.0, length);
    // Reserve Value and End slots before publishing Begin. Updates retain one
    // queue slot for the matching End gesture if a sleeping host stops
    // draining.
    if (_plugin->guiParamEvents.available() < 3u ||
        !beginGuiParamGesture(*_plugin, id)) {
      _dragParam = CLAP_INVALID_ID;
      beep();
      return;
    }
    updateDrag(point);
  }

  bool makeCurrentMatrixCustom() {
    if (loadValue(*_plugin, kMatrixModeParamId) < 4.5) {
      return queueFormantParamValue(*_plugin, kMatrixModeParamId, 5.0);
    }
    return true;
  }

  bool prepareMatrixCellEdit() {
    const uint32_t transactionCount =
        loadValue(*_plugin, kMatrixModeParamId) < 4.5 ? 2u : 1u;
    if (_plugin->guiParamEvents.available() < transactionCount * 3u ||
        !makeCurrentMatrixCustom()) {
      beep();
      return false;
    }
    return true;
  }

  void applySceneOperation(uint32_t operation) {
    std::array<float, kMatrixCells> values{};
    const uint32_t activeBands = activeMatrixBands();
    bool targetSceneB = _editingSceneB;
    if (operation == 3u)
      targetSceneB = !_editingSceneB;

    // A Wide 16 operation edits only its visible 16 x 16 submatrix. Preserve
    // the six hidden Speech 22 rows and columns verbatim so changing layouts is
    // always reversible and never destroys a larger routing scene.
    for (uint32_t destination = 0u; destination < kMatrixBands; ++destination) {
      for (uint32_t source = 0u; source < kMatrixBands; ++source) {
        const uint32_t cell = destination * kMatrixBands + source;
        values[cell] = static_cast<float>(loadValue(
            *_plugin, matrixParamId(targetSceneB, destination, source)));
      }
    }
    if (operation == 3u) {
      for (uint32_t destination = 0u; destination < activeBands;
           ++destination) {
        for (uint32_t source = 0u; source < activeBands; ++source) {
          const uint32_t cell = destination * kMatrixBands + source;
          values[cell] = static_cast<float>(loadValue(
              *_plugin, matrixParamId(_editingSceneB, destination, source)));
        }
      }
    } else if (operation == 0u) {
      for (uint32_t destination = 0u; destination < activeBands;
           ++destination) {
        for (uint32_t source = 0u; source < activeBands; ++source) {
          values[destination * kMatrixBands + source] = 0.0f;
        }
      }
      for (uint32_t band = 0u; band < activeBands; ++band) {
        values[band * kMatrixBands + band] = 1.0f;
      }
    } else if (operation == 1u) {
      for (uint32_t destination = 0u; destination < activeBands;
           ++destination) {
        for (uint32_t source = 0u; source < activeBands; ++source) {
          _randomSeed = _randomSeed * 1664525u + 1013904223u;
          const uint32_t cell = destination * kMatrixBands + source;
          const float unit =
              static_cast<float>((_randomSeed >> 8u) & 0xffffu) / 65535.0f;
          const bool keep =
              source == destination || ((_randomSeed >> 2u) % 7u) == 0u;
          if (keep) {
            const float gain = 0.15f + unit * 0.70f;
            values[cell] = ((_randomSeed >> 1u) & 1u) != 0u ? -gain : gain;
          } else {
            values[cell] = 0.0f;
          }
        }
      }
    } else if (operation == 2u) {
      for (uint32_t destination = 0u; destination < activeBands;
           ++destination) {
        for (uint32_t source = 0u; source < activeBands; ++source) {
          values[destination * kMatrixBands + source] = 0.0f;
        }
      }
    }
    if (queueGuiMatrixValues(*_plugin, targetSceneB, values)) {
      if (operation == 3u)
        _editingSceneB = targetSceneB;
      std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CUSTOM");
    } else {
      beep();
    }
    setNeedsDisplay(true);
  }

  bool handleVisibleParamAtPoint_event(drawing::Point point,
                                       drawing::Event *event) {
    if (_page == kFormantPageRoute) {
      constexpr clap_id routeParams[]{
          kOutputParamId,           kMatrixModeParamId,
          kMatrixMorphParamId,      kCustomMatrixMorphParamId,
          kCouplingParamId,         kBandLayoutParamId,
          kBankModeParamId,         kBankAmountParamId,
          kArticulationThruParamId, kIntelligibilityParamId,
          kMouthFocusParamId,       kBankAttackParamId,
          kBankReleaseParamId,      kOpenLevelParamId,
          kVoicingModeParamId,      kVoicingThresholdParamId,
          kVoicedLevelParamId,      kUnvoicedLevelParamId,
      };
      for (clap_id id : routeParams) {
        const s3g::gui_layout::Panel *panel = nullptr;
        uint32_t row = 0u;
        if (!formantRouteControlLocation(id, panel, row))
          continue;
        const ParamDef *def = paramDef(id);
        const drawing::Rect hit =
            formantParamHitRect(*panel, row, def->stepped);
        if (!drawing::contains(point, hit))
          continue;
        if (def->stepped)
          openMenuForParam_box(id, hit);
        else
          beginDrag_trackStart_trackLength_vertical_event_point(
              id, s3g::gui_layout::processorControlX(panel->frame.x),
              s3g::gui_layout::processorTrackWidth(panel->frame.width), false,
              event, point);
        return true;
      }
      return false;
    }
    const FormantParamSpan params = formantParamsForPage(_page);
    for (uint32_t index = 0u; index < params.count; ++index) {
      const clap_id id = params.data[index];
      const s3g::gui_layout::Panel *panel = nullptr;
      uint32_t row = 0u;
      if (!formantGenericControlLocation(_page, id, panel, row))
        continue;
      const ParamDef *def = paramDef(id);
      const drawing::Rect hit = formantParamHitRect(*panel, row, def->stepped);
      if (!drawing::contains(point, hit))
        continue;
      if (def->stepped)
        openMenuForParam_box(id, hit);
      else
        beginDrag_trackStart_trackLength_vertical_event_point(
            id, s3g::gui_layout::processorControlX(panel->frame.x),
            s3g::gui_layout::processorTrackWidth(panel->frame.width), false,
            event, point);
      return true;
    }
    return false;
  }

  void mouseDown(drawing::Event *event) {
    if (!_plugin)
      return;
    if (_page == kFormantPageRoute && !phraseIsEditing()) {
      focus();
    }
    const drawing::Point point =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    if (_openMenu != CLAP_INVALID_ID) {
      const ParamDef *def = paramDef(_openMenu);
      const int hit =
          _openMenu == kPitchScaleParamId
              ? drawing::multiColumnDropdownHitIndex(
                    point, formantScaleMenuRect(), kFormantMenuItemHeight,
                    s3g::kAcapellaResonatorPitchScaleCount,
                    kFormantScaleMenuColumns)
              : drawing::dropdownHitIndex(point, openMenuRect(),
                                          kFormantMenuItemHeight,
                                          openMenuItemCount());
      if (hit >= 0 && def) {
        if (!queueFormantParamValue(
                *_plugin, def->id,
                def->id == kPitchScaleParamId
                    ? formantScaleValueForMenuIndex(static_cast<uint32_t>(hit))
                    : def->minimum + static_cast<double>(hit)))
          beep();
      }
      _openMenu = CLAP_INVALID_ID;
      _hoverMenuItem = -1;
      syncPhraseFieldVisibility();
      setNeedsDisplay(true);
      return;
    }

    const auto titleBand = s3g::gui_layout::matrixTitleBand(kFormantCanvas);
    if (drawing::contains(point, drawing::cocoaRect(titleBand.presetMenu))) {
      openMenuForParam_box(kPresetParamId,
                           drawing::cocoaRect(titleBand.presetMenu));
      return;
    }
    if (handleTitleClick(point, titleBand)) {
      setNeedsDisplay(true);
      return;
    }
    for (uint32_t page = 0u; page < kFormantPageCount; ++page) {
      if (!drawing::contains(point, formantPageButtonRect(page)))
        continue;
      if (_page == kFormantPagePhrase && page != _page && phraseIsEditing()) {
        focus();
      }
      finishDrag();
      _page = page;
      syncPhraseFieldVisibility();
      setNeedsDisplay(true);
      return;
    }

    if (_page == kFormantPageRoute) {
      clampMatrixSelection();
      const uint32_t activeBands = activeMatrixBands();
      if (drawing::contains(point, formantSceneButtonRect(false)) ||
          drawing::contains(point, formantSceneButtonRect(true))) {
        _editingSceneB = drawing::contains(point, formantSceneButtonRect(true));
        setNeedsDisplay(true);
        return;
      }
      for (uint32_t operation = 0u; operation < 4u; ++operation) {
        if (!drawing::contains(point, formantSceneOperationRect(operation)))
          continue;
        applySceneOperation(operation);
        return;
      }
      for (uint32_t destination = 0u; destination < activeBands;
           ++destination) {
        for (uint32_t source = 0u; source < activeBands; ++source) {
          if (!drawing::contains(point, formantMatrixCellRect(
                                            activeBands, destination, source)))
            continue;
          const clap_id id = matrixParamId(_editingSceneB, destination, source);
          const double current = loadValue(*_plugin, id);
          const bool selected =
              destination == _selectedDestination && source == _selectedSource;
          _selectedDestination = destination;
          _selectedSource = source;
          if (std::abs(current) <= 0.001 || selected) {
            double next = selected && std::abs(current) > 0.001
                              ? 0.0
                              : (destination == source ? 1.0 : 0.25);
            if (altDown) {
              next = -std::abs(next);
            }
            if (!prepareMatrixCellEdit())
              return;
            if (!queueFormantParamValue(*_plugin, id, next))
              beep();
          }
          setNeedsDisplay(true);
          return;
        }
      }
      for (uint32_t band = 0u; band < activeBands; ++band) {
        if (!drawing::contains(point,
                               formantBandTrimHitRect(activeBands, band)))
          continue;
        beginDrag_trackStart_trackLength_vertical_event_point(
            bandTrimParamId(band), 748.0, 82.0, true, event, point);
        return;
      }
      const clap_id selectedId =
          matrixParamId(_editingSceneB, _selectedDestination, _selectedSource);
      if (drawing::contains(
              point, formantParamHitRect(kFormantCrosspointPanel, 0u, false))) {
        if (!prepareMatrixCellEdit())
          return;
        beginDrag_trackStart_trackLength_vertical_event_point(
            selectedId,
            s3g::gui_layout::processorControlX(kFormantCrosspointPanel.frame.x),
            s3g::gui_layout::processorTrackWidth(
                kFormantCrosspointPanel.frame.width),
            false, event, point);
        return;
      }
      if (drawing::contains(point, formantCrosspointActionRect(0u))) {
        if (!prepareMatrixCellEdit())
          return;
        const double current = loadValue(*_plugin, selectedId);
        if (!queueFormantParamValue(*_plugin, selectedId,
                                    std::abs(current) < 0.001 ? 0.25
                                                              : -current))
          beep();
        setNeedsDisplay(true);
        return;
      }
      if (drawing::contains(point, formantCrosspointActionRect(1u))) {
        if (!prepareMatrixCellEdit())
          return;
        if (!queueFormantParamValue(*_plugin, selectedId, 0.0))
          beep();
        setNeedsDisplay(true);
        return;
      }
    }

    if (_page == kFormantPagePhrase &&
        drawing::contains(point, formantCompileRect())) {
      if (phraseIsEditing()) {
        // Ending the field edit invokes controlTextDidEndEditing:, which owns
        // this commit. Avoid publishing the unchanged phrase again later.
        focus();
      } else {
        commitPhrase(nullptr);
      }
      return;
    }
    handleVisibleParamAtPoint_event(point, event);
  }

  void mouseDragged(drawing::Event *event) {
    const drawing::Point point =
        convertPoint_fromView(event->locationInWindow(), nullptr);
    updateMenuHover(point);
    updateDrag(point);
  }

  void mouseMoved(drawing::Event *event) {
    updateMenuHover(convertPoint_fromView(event->locationInWindow(), nullptr));
  }

  void mouseUp(drawing::Event *event) {
    (void)event;
    finishDrag();
  }

  void commitPhrase(void *sender) {
    (void)sender;
    if (!_plugin)
      return;
    if (_plugin->guiParamEvents.available() < 3u) {
      beep();
      return;
    }
    const bool published = publishTextPhrase(*_plugin, phraseText().c_str());
    if (published) {
      if (!queueFormantParamValue(*_plugin, kGestureSequenceParamId, 5.0))
        beep();
    } else {
      beep();
    }
    setNeedsDisplay(true);
  }
};
} // namespace complex_processor_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new complex_processor_canvas::Editor(p);
}
#include "s3g_clap_canvas_gui.inc"
