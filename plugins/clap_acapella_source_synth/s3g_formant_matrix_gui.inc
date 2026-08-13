// Native editor for s3g Processor Formant Matrix. This file is included by
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
    {908.0, 284.0, 436.0, 220.0},
    32.0,
    26.0,
    6u};
constexpr s3g::gui_layout::Panel kFormantSignalPanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Source,
    {908.0, 516.0, 436.0, 216.0},
    31.0,
    25.0,
    5u};
constexpr s3g::gui_layout::Panel kFormantEnvelopePanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Envelope,
    {908.0, 744.0, 436.0, 212.0},
    31.0,
    21.0,
    8u};

constexpr s3g::gui_layout::Panel kFormantGenericLeftPanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Engine,
    {12.0, 74.0, 660.0, 882.0},
    39.0,
    30.0,
    28u};
constexpr s3g::gui_layout::Panel kFormantGenericRightPanel{
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::ToneShape,
    {684.0, 74.0, 660.0, 882.0},
    39.0,
    30.0,
    28u};
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

constexpr CGFloat kFormantMatrixFirstX = 146.0;
constexpr CGFloat kFormantMatrixFirstY = 178.0;
constexpr CGFloat kFormantMatrixPitch = 25.0;
constexpr CGFloat kFormantMenuItemHeight = 18.0;

constexpr clap_id kFormantRouteMatrixParams[]{
    kMatrixModeParamId, kMatrixMorphParamId, kCustomMatrixMorphParamId,
    kCouplingParamId,   kBandLayoutParamId,  kBankModeParamId,
};
constexpr clap_id kFormantRouteSignalParams[]{
    kModulatorSourceParamId, kMicGainParamId, kCarrierPitchSourceParamId,
    kBankAmountParamId,      kArticulationThruParamId,
};
constexpr clap_id kFormantRouteEnvelopeParams[]{
    kAnalysisBlendParamId, kBankAttackParamId,    kBankReleaseParamId,
    kOpenLevelParamId,     kVoicingModeParamId,   kVoicingThresholdParamId,
    kVoicedLevelParamId,   kUnvoicedLevelParamId,
};

constexpr clap_id kFormantBankParams[]{
    kBankAmountParamId,    kBankModeParamId,         kBandLayoutParamId,
    kAnalysisSlopeParamId, kAnalysisBlendParamId,    kBankAttackParamId,
    kBankReleaseParamId,
    kBankResonanceParamId, kBankDriveParamId,        kBandShiftParamId,
    kBandStretchParamId,   kBandTiltParamId,         kSibilanceParamId,
    kOpenLevelParamId,     kCouplingParamId,         kArticulationThruParamId,
    kStereoModeParamId,    kBankStereoSpreadParamId, kBankFreezeParamId,
    kFreezeTriggerParamId, kBankBlurParamId,         kBankGestureFollowParamId,
    kVoicingModeParamId,   kVoicingThresholdParamId, kVoicedLevelParamId,
    kUnvoicedLevelParamId, kVoicedTransitionParamId, kUnvoicedTransitionParamId,
    kMatrixModeParamId,    kMatrixMorphParamId,
};

constexpr clap_id kFormantCarrierParams[]{
    kModulatorSourceParamId,
    kMicGainParamId,
    kCarrierPitchSourceParamId,
    kPitchScaleRootParamId,
    kPitchScaleParamId,
    kPitchHoldParamId,
    kCarrierShapeParamId,
    kCarrierHarmonicsParamId,
    kCarrierColorParamId,
    kCarrierNoiseParamId,
    kCarrierPulseWidthParamId,
    kCarrierLfoShapeParamId,
    kCarrierLfoRateParamId,
    kCarrierLfoDepthParamId,
    kCarrierPwmDepthParamId,
    kCarrierLfoSyncParamId,
    kCarrierLfoDivisionParamId,
    kOctaveDownParamId,
    kOctaveUpParamId,
    kFuzzDriveParamId,
    kFuzzMixParamId,
    kFuzzToneParamId,
};

constexpr clap_id kFormantPhraseParams[]{
    kGestureSequenceParamId, kGestureRateParamId,    kGestureDepthParamId,
    kGestureLoopParamId,     kGestureSyncParamId,    kGestureDivisionParamId,
    kIntelligibilityParamId, kAuditionParamId,       kDeliveryParamId,
    kVowelParamId,           kOnsetParamId,          kDurationParamId,
    kArticulationParamId,    kConsonantParamId,      kIntensityParamId,
    kWaveguideBlendParamId,  kCoarticulationParamId, kHybridBlendParamId,
    kOnsetGuardParamId,      kTractParamId,          kBreathParamId,
    kOpenQuotientParamId,    kHarshnessParamId,      kFalseFoldParamId,
    kThroatParamId,          kRoughnessParamId,      kBrightnessParamId,
    kChestParamId,           kNasalParamId,          kPolyphonyParamId,
    kDoubleAmountParamId,    kDoubleDetuneParamId,   kDoubleTimingParamId,
    kDoubleDirtParamId,      kDoubleWidthParamId,    kVibratoRateParamId,
    kVibratoDepthParamId,    kPitchDriftParamId,     kGlideParamId,
    kScoopParamId,           kDeclinationParamId,    kAttackParamId,
    kReleaseParamId,         kRetriggerParamId,
};

constexpr clap_id kFormantFxParams[]{
    kOutputParamId,     kCompressionParamId, kParallelCrushParamId,
    kDeEssParamId,      kWidthParamId,       kEchoHeadsParamId,
    kEchoClockParamId,  kEchoTimeParamId,    kEchoFeedbackParamId,
    kEchoWearParamId,   kEchoFlutterParamId, kEchoToneParamId,
    kEchoSpreadParamId, kEchoMixParamId,     kOctaveDownParamId,
    kOctaveUpParamId,   kFuzzDriveParamId,   kFuzzMixParamId,
    kFuzzToneParamId,
};

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

NSRect formantPageButtonRect(uint32_t page) {
  return NSMakeRect(20.0 + static_cast<CGFloat>(page) * 92.0, 44.0, 86.0, 20.0);
}

NSRect formantSceneButtonRect(bool sceneB) {
  return NSMakeRect(sceneB ? 704.0 : 666.0, 44.0, 32.0, 20.0);
}

NSRect formantSceneOperationRect(uint32_t operation) {
  constexpr CGFloat widths[]{82.0, 82.0, 72.0, 112.0};
  CGFloat x = 750.0;
  for (uint32_t index = 0u; index < operation; ++index) {
    x += widths[index] + 8.0;
  }
  return NSMakeRect(x, 44.0, widths[operation], 20.0);
}

NSRect formantMatrixCellRect(uint32_t destination, uint32_t source) {
  return NSMakeRect(
      kFormantMatrixFirstX +
          static_cast<CGFloat>(source) * kFormantMatrixPitch - 10.0,
      kFormantMatrixFirstY +
          static_cast<CGFloat>(destination) * kFormantMatrixPitch - 10.0,
      20.0, 20.0);
}

NSRect formantBandTrimHitRect(uint32_t band) {
  return NSMakeRect(kFormantMatrixFirstX +
                        static_cast<CGFloat>(band) * kFormantMatrixPitch - 10.0,
                    738.0, 20.0, 108.0);
}

NSRect formantCrosspointActionRect(uint32_t action) {
  return NSMakeRect(924.0 + static_cast<CGFloat>(action) * 104.0, 232.0, 94.0,
                    20.0);
}

NSRect formantCompileRect() { return NSMakeRect(1234.0, 106.0, 92.0, 24.0); }

NSString *formantShortLabel(clap_id id) {
  switch (id) {
  case kBankAmountParamId:
    return @"BANK MIX";
  case kBankModeParamId:
    return @"MODE";
  case kBandLayoutParamId:
    return @"LAYOUT";
  case kAnalysisSlopeParamId:
    return @"AN SLOPE";
  case kAnalysisBlendParamId:
    return @"AN / PH";
  case kBankAttackParamId:
    return @"ATTACK";
  case kBankReleaseParamId:
    return @"RELEASE";
  case kBankResonanceParamId:
    return @"RESON";
  case kBankDriveParamId:
    return @"DRIVE";
  case kBandShiftParamId:
    return @"SHIFT";
  case kBandStretchParamId:
    return @"STRETCH";
  case kBandTiltParamId:
    return @"TILT";
  case kSibilanceParamId:
    return @"SIBIL";
  case kMatrixModeParamId:
    return @"MAP";
  case kMatrixMorphParamId:
    return @"DEPTH";
  case kCustomMatrixMorphParamId:
    return @"A / B";
  case kBankStereoSpreadParamId:
    return @"SPREAD";
  case kBankFreezeParamId:
    return @"FREEZE";
  case kFreezeTriggerParamId:
    return @"TRIGGER";
  case kBankBlurParamId:
    return @"BLUR";
  case kBankGestureFollowParamId:
    return @"FOLLOW";
  case kVoicingModeParamId:
    return @"V / UV";
  case kVoicingThresholdParamId:
    return @"THRESH";
  case kVoicedLevelParamId:
    return @"VOICED";
  case kUnvoicedLevelParamId:
    return @"UNVOICE";
  case kVoicedTransitionParamId:
    return @"TO V";
  case kUnvoicedTransitionParamId:
    return @"TO UV";
  case kOpenLevelParamId:
    return @"OPEN";
  case kCouplingParamId:
    return @"COUPLE";
  case kArticulationThruParamId:
    return @"ART THRU";
  case kStereoModeParamId:
    return @"STEREO";
  case kModulatorSourceParamId:
    return @"MOD SOURCE";
  case kMicGainParamId:
    return @"MIC GAIN";
  case kCarrierPitchSourceParamId:
    return @"PITCH SRC";
  case kPitchScaleRootParamId:
    return @"ROOT";
  case kPitchScaleParamId:
    return @"SCALE";
  case kPitchHoldParamId:
    return @"PITCH HOLD";
  case kCarrierPulseWidthParamId:
    return @"PULSE W";
  case kCarrierLfoShapeParamId:
    return @"LFO SHAPE";
  case kCarrierLfoRateParamId:
    return @"LFO RATE";
  case kCarrierLfoDepthParamId:
    return @"FM";
  case kCarrierPwmDepthParamId:
    return @"PWM";
  case kCarrierLfoSyncParamId:
    return @"LFO SYNC";
  case kCarrierLfoDivisionParamId:
    return @"DIV";
  case kOutputParamId:
    return @"OUT";
  default: {
    const ParamDef *def = paramDef(id);
    return def ? [[NSString stringWithUTF8String:def->name] uppercaseString]
               : @"";
  }
  }
}

NSString *formantPhraseString(const PhraseState &phrase) {
  NSString *result = [[NSString alloc]
      initWithBytes:phrase.text.data()
             length:phrase.length
           encoding:NSUTF8StringEncoding];
  if (!result) {
    // Phrase storage is byte-bounded for real-time safety. A legacy or corrupt
    // payload may end with an incomplete UTF-8 sequence, so keep the native
    // field non-nil and editable using a deterministic byte-preserving fallback.
    result = [[NSString alloc] initWithBytes:phrase.text.data()
                                     length:phrase.length
                                   encoding:NSISOLatin1StringEncoding];
  }
  return [result autorelease];
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

void drawFormantActionButton(NSRect rect, NSString *title, bool active,
                             const s3g::clap_gui::Style &style) {
  s3g::clap_gui::drawHeaderButton(rect, rect, title, active,
                                  s3g::clap_gui::softLabelAttrs(), style);
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

NSRect formantParamHitRect(const s3g::gui_layout::Panel &panel, uint32_t row,
                           bool stepped) {
  if (stepped) {
    return NSMakeRect(s3g::gui_layout::processorControlX(panel.frame.x),
                      s3g::gui_layout::rowY(panel, row) - 1.0,
                      s3g::gui_layout::processorMenuWidth(panel.frame.width),
                      15.0);
  }
  return s3g::clap_gui::cocoaRect(s3g::gui_layout::sliderHitRect(panel, row));
}

@interface S3GFormantMatrixView : NSView <NSTextFieldDelegate> {
@private
  Plugin *_plugin;
  NSTextField *_phraseField;
  NSTimer *_timer;
  NSTrackingArea *_trackingArea;
  uint32_t _page;
  BOOL _editingSceneB;
  uint32_t _selectedSource;
  uint32_t _selectedDestination;
  clap_id _dragParam;
  BOOL _dragVertical;
  CGFloat _dragTrackStart;
  CGFloat _dragTrackLength;
  clap_id _openMenu;
  int _hoverMenuItem;
  NSPoint _menuOrigin;
  CGFloat _menuWidth;
  uint32_t _randomSeed;
  char _titlePresetName[64];
}
- (instancetype)initWithPlugin:(Plugin *)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
@end

@implementation S3GFormantMatrixView

- (instancetype)initWithPlugin:(Plugin *)plugin {
  self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
  if (self) {
    _plugin = plugin;
    _phraseField = nil;
    _timer = nil;
    _trackingArea = nil;
    _page = kFormantPageRoute;
    _editingSceneB = NO;
    _selectedSource = 0u;
    _selectedDestination = 0u;
    _dragParam = CLAP_INVALID_ID;
    _dragVertical = NO;
    _dragTrackStart = 0.0;
    _dragTrackLength = 1.0;
    _openMenu = CLAP_INVALID_ID;
    _hoverMenuItem = -1;
    _menuOrigin = NSZeroPoint;
    _menuWidth = 180.0;
    _randomSeed = 0x6d2b79f5u;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "INIT");

    _phraseField = [[NSTextField alloc]
        initWithFrame:NSMakeRect(28.0, 106.0, 1188.0, 24.0)];
    s3g::clap_gui::styleNumberTextField(_phraseField, 12.0,
                                        NSTextAlignmentLeft);
    [_phraseField setPlaceholderString:@"type a phrase   | = rest"];
    [_phraseField setDelegate:self];
    [_phraseField setTarget:self];
    [_phraseField setAction:@selector(commitPhrase:)];
    const PhraseState phrase = plugin ? loadPhrase(*plugin) : PhraseState{};
    [_phraseField setStringValue:formantPhraseString(phrase)];
    [self addSubview:_phraseField];
    [_phraseField setHidden:YES];
  }
  return self;
}

- (BOOL)isFlipped {
  return YES;
}
- (BOOL)acceptsFirstResponder {
  return YES;
}

- (void)dealloc {
  if (_dragParam != CLAP_INVALID_ID && _plugin) {
    endGuiParamGesture(*_plugin, _dragParam);
  }
  [self stopRefreshTimer];
  if (_trackingArea) {
    [self removeTrackingArea:_trackingArea];
    [_trackingArea release];
    _trackingArea = nil;
  }
  [_phraseField setDelegate:nil];
  [_phraseField setTarget:nil];
  [_phraseField release];
  [super dealloc];
}

- (void)updateTrackingAreas {
  [super updateTrackingAreas];
  if (_trackingArea) {
    [self removeTrackingArea:_trackingArea];
    [_trackingArea release];
  }
  _trackingArea = [[NSTrackingArea alloc]
      initWithRect:NSZeroRect
           options:NSTrackingMouseMoved | NSTrackingActiveInKeyWindow |
                   NSTrackingInVisibleRect
             owner:self
          userInfo:nil];
  [self addTrackingArea:_trackingArea];
}

- (BOOL)phraseIsEditing {
  NSResponder *first = [[self window] firstResponder];
  return first == _phraseField || first == [_phraseField currentEditor];
}

- (void)syncPhraseFieldVisibility {
  [_phraseField
      setHidden:!(_page == kFormantPagePhrase && _openMenu == CLAP_INVALID_ID)];
}

- (void)finishDrag {
  if (_dragParam != CLAP_INVALID_ID && _plugin) {
    endGuiParamGesture(*_plugin, _dragParam);
  }
  _dragParam = CLAP_INVALID_ID;
}

- (void)drawPanel:(NSString *)title
            panel:(const s3g::gui_layout::Panel &)panel
            style:(const s3g::clap_gui::Style &)style {
  s3g::clap_gui::drawPanelFrame(panel, style);
  s3g::clap_gui::drawPanelHeader(title, true, panel,
                                 s3g::clap_gui::softLabelAttrs(), style);
}

- (void)drawParam:(clap_id)id
              row:(uint32_t)row
            panel:(const s3g::gui_layout::Panel &)panel
            style:(const s3g::clap_gui::Style &)style {
  const ParamDef *def = paramDef(id);
  if (!def)
    return;
  const double value = loadValue(*_plugin, id);
  char text[64]{};
  paramsValueToText(&_plugin->plugin, id, value, text, sizeof(text));
  NSString *display = [[NSString stringWithUTF8String:text] uppercaseString];
  const CGFloat y = s3g::gui_layout::rowY(panel, row);
  if (def->stepped) {
    s3g::clap_gui::drawProcessorMenu(formantShortLabel(id), display, y,
                                     panel.frame.x, panel.frame.width,
                                     s3g::clap_gui::softLabelAttrs(),
                                     s3g::clap_gui::softValueAttrs(), style);
  } else {
    const CGFloat norm = static_cast<CGFloat>(std::clamp(
        (value - def->minimum) / std::max(1.0e-9, def->maximum - def->minimum),
        0.0, 1.0));
    s3g::clap_gui::drawProcessorSlider(formantShortLabel(id), display, norm, y,
                                       panel.frame.x, panel.frame.width,
                                       s3g::clap_gui::softLabelAttrs(),
                                       s3g::clap_gui::softValueAttrs(), style);
  }
}

- (void)drawPageBand:(const s3g::clap_gui::Style &)style {
  [style.strip setFill];
  NSRectFill(NSMakeRect(12.0, 42.0, 1332.0, 24.0));
  for (uint32_t page = 0u; page < kFormantPageCount; ++page) {
    drawFormantActionButton(
        formantPageButtonRect(page),
        [NSString stringWithUTF8String:kFormantPageNames[page]], _page == page,
        style);
  }
  if (_page != kFormantPageRoute)
    return;
  drawFormantActionButton(formantSceneButtonRect(false), @"A", !_editingSceneB,
                          style);
  drawFormantActionButton(formantSceneButtonRect(true), @"B", _editingSceneB,
                          style);
  drawFormantActionButton(formantSceneOperationRect(0u), @"IDENTITY", false,
                          style);
  drawFormantActionButton(formantSceneOperationRect(1u), @"RANDOM", false,
                          style);
  drawFormantActionButton(formantSceneOperationRect(2u), @"CLEAR", false,
                          style);
  drawFormantActionButton(formantSceneOperationRect(3u),
                          _editingSceneB ? @"COPY B > A" : @"COPY A > B", false,
                          style);
}

- (void)drawMeter:(NSRect)rect
            level:(CGFloat)level
            color:(NSColor *)color
            style:(const s3g::clap_gui::Style &)style
         vertical:(BOOL)vertical {
  [s3g::clap_gui::color(0x090909) setFill];
  NSRectFill(rect);
  [style.grid setStroke];
  NSFrameRect(rect);
  level =
      std::clamp(level, static_cast<CGFloat>(0.0), static_cast<CGFloat>(1.0));
  NSRect fill = NSInsetRect(rect, 1.0, 1.0);
  [color setFill];
  if (vertical) {
    const CGFloat height = fill.size.height * level;
    fill.origin.y += fill.size.height - height;
    fill.size.height = height;
  } else {
    fill.size.width *= level;
  }
  if (fill.size.width > 0.0 && fill.size.height > 0.0)
    NSRectFill(fill);
}

- (void)drawRoutePage:(const s3g::clap_gui::Style &)style {
  NSDictionary *labels = s3g::clap_gui::softLabelAttrs();
  NSDictionary *values = s3g::clap_gui::softValueAttrs();
  const bool voicePitch =
      loadValue(*_plugin, kCarrierPitchSourceParamId) >= 0.5;
  [self drawPanel:[NSString
                      stringWithFormat:@"ROUTING %c // MODULATOR ANALYSIS > %@ CARRIER",
                                       _editingSceneB ? 'B' : 'A',
                                       voicePitch ? @"VOICE-PITCH" : @"MIDI"]
            panel:kFormantRoutePanel
            style:style];
  [self drawPanel:@"OUTPUT" panel:kFormantOutputPanel style:style];
  [self drawPanel:[NSString stringWithFormat:@"CROSSPOINT // A%02u > S%02u",
                                             _selectedSource + 1u,
                                             _selectedDestination + 1u]
            panel:kFormantCrosspointPanel
            style:style];
  [self drawPanel:@"MATRIX" panel:kFormantMatrixControlPanel style:style];
  [self drawPanel:@"SIGNAL FLOW" panel:kFormantSignalPanel style:style];
  [self drawPanel:@"ENVELOPES" panel:kFormantEnvelopePanel style:style];

  [self drawParam:kOutputParamId row:0u panel:kFormantOutputPanel style:style];
  for (uint32_t row = 0u; row < std::size(kFormantRouteMatrixParams); ++row) {
    [self drawParam:kFormantRouteMatrixParams[row]
                row:row
              panel:kFormantMatrixControlPanel
              style:style];
  }
  for (uint32_t row = 0u; row < std::size(kFormantRouteSignalParams); ++row) {
    [self drawParam:kFormantRouteSignalParams[row]
                row:row
              panel:kFormantSignalPanel
              style:style];
  }
  for (uint32_t row = 0u; row < std::size(kFormantRouteEnvelopeParams); ++row) {
    [self drawParam:kFormantRouteEnvelopeParams[row]
                row:row
              panel:kFormantEnvelopePanel
              style:style];
  }

  const clap_id selectedId =
      matrixParamId(_editingSceneB, _selectedDestination, _selectedSource);
  const double selected = loadValue(*_plugin, selectedId);
  s3g::clap_gui::drawProcessorSlider(
      @"GAIN", [NSString stringWithFormat:@"%+.3f", selected],
      static_cast<CGFloat>((selected + 1.0) * 0.5),
      s3g::gui_layout::rowY(kFormantCrosspointPanel, 0u),
      kFormantCrosspointPanel.frame.x, kFormantCrosspointPanel.frame.width,
      labels, values, style);
  drawFormantActionButton(formantCrosspointActionRect(0u),
                          selected < 0.0 ? @"NEGATIVE" : @"POSITIVE",
                          std::abs(selected) > 0.001, style);
  drawFormantActionButton(formantCrosspointActionRect(1u), @"CLEAR", false,
                          style);

  const bool wide16 = loadValue(*_plugin, kBandLayoutParamId) >= 0.5;
  const int modulatorSource = static_cast<int>(std::lround(
      loadValue(*_plugin, kModulatorSourceParamId)));
  NSString *selectedAnalysisLabel =
      modulatorSource == 0
          ? @"SELECTED ANALYSIS // MIC"
          : modulatorSource == 2 ? @"SELECTED ANALYSIS // BLEND"
                                 : @"SELECTED ANALYSIS // SPEECH";
  [selectedAnalysisLabel drawAtPoint:NSMakePoint(28.0, 116.0)
                      withAttributes:labels];
  [@"MODULATOR BAND" drawAtPoint:NSMakePoint(366.0, 98.0)
                   withAttributes:labels];
  [(voicePitch ? @"VOICE-PITCH CARRIER / MAPPED"
               : @"MIDI CARRIER / MAPPED")
      drawAtPoint:NSMakePoint(698.0, 116.0)
      withAttributes:labels];
  [style.grid setStroke];
  const CGFloat lastX =
      kFormantMatrixFirstX +
      static_cast<CGFloat>(kMatrixBands - 1u) * kFormantMatrixPitch;
  const CGFloat lastY =
      kFormantMatrixFirstY +
      static_cast<CGFloat>(kMatrixBands - 1u) * kFormantMatrixPitch;
  for (uint32_t band = 0u; band < kMatrixBands; ++band) {
    const CGFloat x =
        kFormantMatrixFirstX + static_cast<CGFloat>(band) * kFormantMatrixPitch;
    const CGFloat y =
        kFormantMatrixFirstY + static_cast<CGFloat>(band) * kFormantMatrixPitch;
    [NSBezierPath strokeLineFromPoint:NSMakePoint(x, kFormantMatrixFirstY)
                              toPoint:NSMakePoint(x, lastY)];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(kFormantMatrixFirstX, y)
                              toPoint:NSMakePoint(lastX, y)];
    const bool activeBand = !wide16 || band < 16u;
    const CGFloat analysisLinear =
        activeBand
            ? _plugin->analysisBandMeters[band].load(std::memory_order_relaxed)
            : 0.0;
    const CGFloat synthesisLinear =
        activeBand
            ? _plugin->synthesisBandMeters[band].load(std::memory_order_relaxed)
            : 0.0;
    const CGFloat analysis =
        s3g::acapellaResonatorMeterDisplayLevel(analysisLinear);
    const CGFloat synthesis =
        s3g::acapellaResonatorMeterDisplayLevel(synthesisLinear);
    [self drawMeter:NSMakeRect(x - 4.0, 122.0, 8.0, 38.0)
              level:analysis
              color:s3g::clap_gui::color(0xc67b52)
              style:style
           vertical:YES];
    [self drawMeter:NSMakeRect(700.0, y - 4.0, 62.0, 8.0)
              level:synthesis
              color:s3g::clap_gui::color(0x70b4bb)
              style:style
           vertical:NO];
    NSString *number = [NSString stringWithFormat:@"%u", band + 1u];
    [number drawAtPoint:NSMakePoint(x - 6.0, 162.0) withAttributes:values];
    [number drawAtPoint:NSMakePoint(112.0, y - 6.0) withAttributes:values];
  }

  for (uint32_t destination = 0u; destination < kMatrixBands; ++destination) {
    const CGFloat synthesis = s3g::acapellaResonatorMeterDisplayLevel(
        _plugin->synthesisBandMeters[destination].load(
            std::memory_order_relaxed));
    for (uint32_t source = 0u; source < kMatrixBands; ++source) {
      const double route = loadValue(
          *_plugin, matrixParamId(_editingSceneB, destination, source));
      const CGFloat analysis = s3g::acapellaResonatorMeterDisplayLevel(
          _plugin->analysisBandMeters[source].load(
              std::memory_order_relaxed));
      const NSRect hit = formantMatrixCellRect(destination, source);
      const NSRect node =
          NSMakeRect(NSMidX(hit) - 5.0, NSMidY(hit) - 5.0, 10.0, 10.0);
      const bool selectedCell =
          destination == _selectedDestination && source == _selectedSource;
      [s3g::clap_gui::color(0x111111) setFill];
      NSRectFill(node);
      if (std::abs(route) > 0.001) {
        const CGFloat activity =
            std::sqrt(std::max<CGFloat>(0.0, analysis * synthesis));
        const CGFloat size = std::clamp<CGFloat>(
            2.0 + std::abs(route) * 6.0 + activity * 3.0, 2.0, 9.0);
        [(route >= 0.0 ? s3g::clap_gui::color(0xd47a4d)
                       : s3g::clap_gui::color(0x62b7c1)) setFill];
        NSRectFill(NSMakeRect(NSMidX(node) - size * 0.5,
                              NSMidY(node) - size * 0.5, size, size));
      }
      [(selectedCell ? s3g::clap_gui::color(0xe0e0e0)
                     : s3g::clap_gui::color(0x4c4c4c)) setStroke];
      NSFrameRect(node);
    }
  }

  [@"OUTPUT TRIM" drawAtPoint:NSMakePoint(94.0, 718.0) withAttributes:labels];
  for (uint32_t band = 0u; band < kMatrixBands; ++band) {
    const CGFloat x =
        kFormantMatrixFirstX + static_cast<CGFloat>(band) * kFormantMatrixPitch;
    const double trim = loadValue(*_plugin, bandTrimParamId(band));
    const CGFloat norm = static_cast<CGFloat>(trim * 0.5);
    const NSRect track = NSMakeRect(x - 3.0, 748.0, 6.0, 82.0);
    [s3g::clap_gui::color(0x0a0a0a) setFill];
    NSRectFill(track);
    [style.grid setStroke];
    NSFrameRect(track);
    const CGFloat handleY = track.origin.y + track.size.height * (1.0 - norm);
    [s3g::clap_gui::color(0x70b4bb) setFill];
    NSRectFill(NSMakeRect(x - 6.0, handleY - 2.0, 12.0, 4.0));
    [[NSString stringWithFormat:@"%u", band + 1u]
           drawAtPoint:NSMakePoint(x - 6.0, 836.0)
        withAttributes:values];
  }
  [@"CLICK: CREATE / SELECT · CLICK SELECTED: CLEAR · OPTION: NEGATIVE"
         drawAtPoint:NSMakePoint(30.0, 918.0)
      withAttributes:values];

  const CGFloat micPeak = s3g::acapellaResonatorMeterDisplayLevel(
      _plugin->externalMicPeak.load(std::memory_order_relaxed));
  const CGFloat speechPeak = s3g::acapellaResonatorMeterDisplayLevel(
      _plugin->internalSpeechPeak.load(std::memory_order_relaxed));
  NSString *micLabel = modulatorSource == 0
                           ? @"MIC [SELECTED]"
                           : modulatorSource == 2 ? @"MIC [BLEND]" : @"MIC";
  NSString *speechLabel =
      modulatorSource == 1
          ? @"INTERNAL SPEECH [SELECTED]"
          : modulatorSource == 2 ? @"INTERNAL SPEECH [BLEND]"
                                 : @"INTERNAL SPEECH";
  [micLabel drawAtPoint:NSMakePoint(924.0, 674.0) withAttributes:labels];
  [self drawMeter:NSMakeRect(1072.0, 676.0, 252.0, 8.0)
            level:micPeak
            color:s3g::clap_gui::color(0x87ad91)
            style:style
         vertical:NO];
  [speechLabel drawAtPoint:NSMakePoint(924.0, 692.0)
            withAttributes:labels];
  [self drawMeter:NSMakeRect(1072.0, 694.0, 252.0, 8.0)
            level:speechPeak
            color:s3g::clap_gui::color(0xc67b52)
            style:style
         vertical:NO];
  if (voicePitch) {
    const bool pitchActive =
        _plugin->pitchActive.load(std::memory_order_relaxed);
    const float pitchHz =
        _plugin->detectedPitchHz.load(std::memory_order_relaxed);
    const float confidence =
        _plugin->pitchConfidence.load(std::memory_order_relaxed);
    NSString *pitchText = pitchActive
        ? [NSString stringWithFormat:@"VOICE PITCH  %.1f Hz  //  %.0f%%",
                                      pitchHz, confidence * 100.0f]
        : @"VOICE PITCH  LISTENING";
    [pitchText drawAtPoint:NSMakePoint(924.0, 710.0)
            withAttributes:values];
  }
}

- (void)drawGenericPage:(const s3g::clap_gui::Style &)style {
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
  NSString *leftTitle = _page == kFormantPageBank
                            ? @"FILTER BANK"
                            : _page == kFormantPageCarrier
                                  ? @"MODULATOR / CARRIER"
                                  : _page == kFormantPagePhrase
                                        ? @"PHRASE / PERFORMANCE"
                                        : @"DYNAMICS / OUTPUT";
  const bool voicePitch =
      loadValue(*_plugin, kCarrierPitchSourceParamId) >= 0.5;
  NSString *rightTitle = _page == kFormantPageBank
                             ? @"ENVELOPES / SPACE"
                             : _page == kFormantPageCarrier
                                   ? (voicePitch ? @"VOICE-PITCH CARRIER / LFO"
                                                 : @"MIDI CARRIER / LFO")
                                   : _page == kFormantPagePhrase
                                         ? @"VOICE / ENSEMBLE"
                                         : @"MULTI-HEAD TAPE";
  [self drawPanel:leftTitle panel:left style:style];
  [self drawPanel:rightTitle panel:right style:style];
  for (uint32_t index = 0u; index < params.count; ++index) {
    [self drawParam:params.data[index]
                row:index < leftCount ? index : index - leftCount
              panel:index < leftCount ? left : right
              style:style];
  }
}

- (void)drawPhraseHeader:(const s3g::clap_gui::Style &)style {
  [self drawPanel:@"PROCEDURAL SPEECH // TEXT TO GESTURE"
            panel:kFormantPhraseInputPanel
            style:style];
  drawFormantActionButton(formantCompileRect(), @"COMPILE", false, style);
  const PhraseState phrase = loadPhrase(*_plugin);
  const auto compiled = s3g::compileAcapellaText(phrase.text.data());
  NSDictionary *labels = s3g::clap_gui::softLabelAttrs();
  NSDictionary *values = s3g::clap_gui::softValueAttrs();
  [[NSString
      stringWithFormat:@"%u WORDS // %u EVENTS%@", compiled.program.wordCount,
                       compiled.program.count,
                       compiled.program.truncated ? @" // TRUNCATED" : @""]
         drawAtPoint:NSMakePoint(28.0, 142.0)
      withAttributes:values];
  [@"| REST // || 2X // LOOP AND SYNC BELOW"
         drawAtPoint:NSMakePoint(1032.0, 142.0)
      withAttributes:labels];
  constexpr uint32_t visibleSteps = 24u;
  constexpr CGFloat cellPitch = 52.0;
  for (uint32_t index = 0u; index < visibleSteps; ++index) {
    const NSRect cell = NSMakeRect(
        28.0 + static_cast<CGFloat>(index) * cellPitch, 166.0, 46.0, 34.0);
    const bool populated = index < compiled.program.count;
    [s3g::clap_gui::color(populated ? 0x24282a : 0x151719) setFill];
    NSRectFill(cell);
    [s3g::clap_gui::color(populated ? 0x555b5e : 0x292c2e) setStroke];
    NSFrameRect(cell);
    if (!populated)
      continue;
    const auto &step = compiled.program.steps[index];
    const bool rest = (step.flags & s3g::kAcapellaForcedRest) != 0u;
    NSString *symbol =
        rest
            ? @"REST"
            : [NSString stringWithUTF8String:formantPhonemeLabel(step.phoneme)];
    const NSSize size = [symbol sizeWithAttributes:labels];
    [symbol drawAtPoint:NSMakePoint(NSMidX(cell) - size.width * 0.5,
                                    cell.origin.y + 9.0)
         withAttributes:labels];
  }
}

- (void)drawOpenMenu:(const s3g::clap_gui::Style &)style {
  const ParamDef *def = paramDef(_openMenu);
  if (!def || !def->stepped)
    return;
  const uint32_t count = std::min<uint32_t>(
      32u,
      static_cast<uint32_t>(std::lround(def->maximum - def->minimum)) + 1u);
  std::array<NSString *, 32u> items{};
  for (uint32_t index = 0u; index < count; ++index) {
    char text[64]{};
    paramsValueToText(&_plugin->plugin, def->id,
                      def->minimum + static_cast<double>(index), text,
                      sizeof(text));
    items[index] = [[NSString stringWithUTF8String:text] uppercaseString];
  }
  const NSRect rect =
      NSMakeRect(_menuOrigin.x, _menuOrigin.y, _menuWidth,
                 kFormantMenuItemHeight * static_cast<CGFloat>(count));
  const int selected = static_cast<int>(
      std::lround(loadValue(*_plugin, def->id) - def->minimum));
  s3g::clap_gui::drawDropdownMenu(rect, kFormantMenuItemHeight, items.data(),
                                  count, selected, _hoverMenuItem,
                                  s3g::clap_gui::softValueAttrs(), style);
}

- (void)drawRect:(NSRect)dirty {
  (void)dirty;
  if (!_plugin)
    return;
  s3g::clap_gui::Style style;
  [style.bg setFill];
  NSRectFill([self bounds]);
  char preset[64]{};
  paramsValueToText(&_plugin->plugin, kPresetParamId,
                    loadValue(*_plugin, kPresetParamId), preset,
                    sizeof(preset));
  const auto titleBand = s3g::gui_layout::matrixTitleBand(kFormantCanvas);
  s3g::clap_gui::drawProcessorTitleBand(
      @"s3g PROCESSOR FORMANT MATRIX",
      [[NSString stringWithUTF8String:preset] uppercaseString],
      s3g::clap_gui::peakDbText(
          _plugin->outputPeak.load(std::memory_order_relaxed)),
      titleBand, s3g::clap_gui::softTitleAttrs(),
      s3g::clap_gui::softLabelAttrs(), s3g::clap_gui::softValueAttrs(), style);
  [self drawPageBand:style];
  if (_page == kFormantPageRoute)
    [self drawRoutePage:style];
  else {
    if (_page == kFormantPagePhrase)
      [self drawPhraseHeader:style];
    [self drawGenericPage:style];
  }
  if (_openMenu != CLAP_INVALID_ID)
    [self drawOpenMenu:style];
}

- (uint32_t)openMenuItemCount {
  const ParamDef *def = paramDef(_openMenu);
  return def && def->stepped
             ? std::min<uint32_t>(32u, static_cast<uint32_t>(std::lround(
                                           def->maximum - def->minimum)) +
                                           1u)
             : 0u;
}

- (NSRect)openMenuRect {
  return NSMakeRect(_menuOrigin.x, _menuOrigin.y, _menuWidth,
                    kFormantMenuItemHeight *
                        static_cast<CGFloat>([self openMenuItemCount]));
}

- (void)openMenuForParam:(clap_id)id box:(NSRect)box {
  const ParamDef *def = paramDef(id);
  if (!def || !def->stepped)
    return;
  if ([self phraseIsEditing]) {
    [[self window] makeFirstResponder:self];
  }
  const CGFloat height =
      kFormantMenuItemHeight *
      static_cast<CGFloat>(std::min<uint32_t>(
          32u, static_cast<uint32_t>(std::lround(def->maximum - def->minimum)) +
                   1u));
  const CGFloat below = NSMaxY(box) + 3.0;
  _openMenu = id;
  _hoverMenuItem = -1;
  _menuOrigin = NSMakePoint(
      box.origin.x, below + height <= static_cast<CGFloat>(kGuiHeight) - 8.0
                        ? below
                        : std::max<CGFloat>(34.0, box.origin.y - height - 3.0));
  _menuWidth = box.size.width;
  [self syncPhraseFieldVisibility];
  [self setNeedsDisplay:YES];
}

- (void)updateMenuHover:(NSPoint)point {
  if (_openMenu == CLAP_INVALID_ID)
    return;
  const int hover = s3g::clap_gui::dropdownHitIndex(point, [self openMenuRect],
                                                    kFormantMenuItemHeight,
                                                    [self openMenuItemCount]);
  if (hover == _hoverMenuItem)
    return;
  _hoverMenuItem = hover;
  [self setNeedsDisplay:YES];
}

- (void)updateDrag:(NSPoint)point {
  if (_dragParam == CLAP_INVALID_ID || !_plugin)
    return;
  const ParamDef *def = paramDef(_dragParam);
  if (!def || def->stepped)
    return;
  CGFloat norm = _dragVertical
                     ? 1.0 - (point.y - _dragTrackStart) / _dragTrackLength
                     : (point.x - _dragTrackStart) / _dragTrackLength;
  norm = std::clamp(norm, static_cast<CGFloat>(0.0), static_cast<CGFloat>(1.0));
  if (_plugin->guiParamEvents.available() <= 1u)
    return;
  if (!updateGuiParamGesture(*_plugin, _dragParam,
                             def->minimum + static_cast<double>(norm) *
                                                (def->maximum - def->minimum)))
    return;
  [self setNeedsDisplay:YES];
}

- (void)beginDrag:(clap_id)id
       trackStart:(CGFloat)start
      trackLength:(CGFloat)length
         vertical:(BOOL)vertical
            event:(NSEvent *)event
            point:(NSPoint)point {
  [self finishDrag];
  double reset = 0.0;
  if (s3g::clap_gui::sliderDoubleClickDefault(event, &_plugin->plugin, id,
                                              &reset)) {
    if (!queueFormantParamValue(*_plugin, id, reset))
      NSBeep();
    [self setNeedsDisplay:YES];
    return;
  }
  _dragParam = id;
  _dragVertical = vertical;
  _dragTrackStart = start;
  _dragTrackLength = std::max<CGFloat>(1.0, length);
  // Reserve Value and End slots before publishing Begin. Updates retain one
  // queue slot for the matching End gesture if a sleeping host stops draining.
  if (_plugin->guiParamEvents.available() < 3u ||
      !beginGuiParamGesture(*_plugin, id)) {
    _dragParam = CLAP_INVALID_ID;
    NSBeep();
    return;
  }
  [self updateDrag:point];
}

- (BOOL)makeCurrentMatrixCustom {
  if (loadValue(*_plugin, kMatrixModeParamId) < 4.5) {
    return queueFormantParamValue(*_plugin, kMatrixModeParamId, 5.0);
  }
  return YES;
}

- (BOOL)prepareMatrixCellEdit {
  const uint32_t transactionCount =
      loadValue(*_plugin, kMatrixModeParamId) < 4.5 ? 2u : 1u;
  if (_plugin->guiParamEvents.available() < transactionCount * 3u ||
      ![self makeCurrentMatrixCustom]) {
    NSBeep();
    return NO;
  }
  return YES;
}

- (void)applySceneOperation:(uint32_t)operation {
  std::array<float, kMatrixCells> values{};
  bool targetSceneB = _editingSceneB;
  if (operation == 3u)
    targetSceneB = !_editingSceneB;
  if (operation == 3u) {
    for (uint32_t destination = 0u; destination < kMatrixBands; ++destination) {
      for (uint32_t source = 0u; source < kMatrixBands; ++source) {
        const uint32_t cell = destination * kMatrixBands + source;
        values[cell] = static_cast<float>(loadValue(
            *_plugin, matrixParamId(_editingSceneB, destination, source)));
      }
    }
  } else if (operation == 0u) {
    for (uint32_t band = 0u; band < kMatrixBands; ++band) {
      values[band * kMatrixBands + band] = 1.0f;
    }
  } else if (operation == 1u) {
    for (uint32_t destination = 0u; destination < kMatrixBands; ++destination) {
      for (uint32_t source = 0u; source < kMatrixBands; ++source) {
        _randomSeed = _randomSeed * 1664525u + 1013904223u;
        const uint32_t cell = destination * kMatrixBands + source;
        const float unit =
            static_cast<float>((_randomSeed >> 8u) & 0xffffu) / 65535.0f;
        const bool keep =
            source == destination || ((_randomSeed >> 2u) % 7u) == 0u;
        if (keep) {
          const float gain = 0.15f + unit * 0.70f;
          values[cell] = ((_randomSeed >> 1u) & 1u) != 0u ? -gain : gain;
        }
      }
    }
  }
  if (queueGuiMatrixValues(*_plugin, targetSceneB, values)) {
    if (operation == 3u)
      _editingSceneB = targetSceneB;
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CUSTOM");
  } else {
    NSBeep();
  }
  [self setNeedsDisplay:YES];
}

- (BOOL)handleVisibleParamAtPoint:(NSPoint)point event:(NSEvent *)event {
  if (_page == kFormantPageRoute) {
    constexpr clap_id routeParams[]{
        kOutputParamId,
        kMatrixModeParamId,
        kMatrixMorphParamId,
        kCustomMatrixMorphParamId,
        kCouplingParamId,
        kBandLayoutParamId,
        kBankModeParamId,
        kModulatorSourceParamId,
        kMicGainParamId,
        kBankAmountParamId,
        kArticulationThruParamId,
        kAnalysisBlendParamId,
        kBankAttackParamId,
        kBankReleaseParamId,
        kOpenLevelParamId,
        kVoicingModeParamId,
        kVoicingThresholdParamId,
        kVoicedLevelParamId,
        kUnvoicedLevelParamId,
    };
    for (clap_id id : routeParams) {
      const s3g::gui_layout::Panel *panel = nullptr;
      uint32_t row = 0u;
      if (!formantRouteControlLocation(id, panel, row))
        continue;
      const ParamDef *def = paramDef(id);
      const NSRect hit = formantParamHitRect(*panel, row, def->stepped);
      if (!NSPointInRect(point, hit))
        continue;
      if (def->stepped)
        [self openMenuForParam:id box:hit];
      else
        [self beginDrag:id
             trackStart:s3g::gui_layout::processorControlX(panel->frame.x)
            trackLength:s3g::gui_layout::processorTrackWidth(panel->frame.width)
               vertical:NO
                  event:event
                  point:point];
      return YES;
    }
    return NO;
  }
  const FormantParamSpan params = formantParamsForPage(_page);
  for (uint32_t index = 0u; index < params.count; ++index) {
    const clap_id id = params.data[index];
    const s3g::gui_layout::Panel *panel = nullptr;
    uint32_t row = 0u;
    if (!formantGenericControlLocation(_page, id, panel, row))
      continue;
    const ParamDef *def = paramDef(id);
    const NSRect hit = formantParamHitRect(*panel, row, def->stepped);
    if (!NSPointInRect(point, hit))
      continue;
    if (def->stepped)
      [self openMenuForParam:id box:hit];
    else
      [self beginDrag:id
           trackStart:s3g::gui_layout::processorControlX(panel->frame.x)
          trackLength:s3g::gui_layout::processorTrackWidth(panel->frame.width)
             vertical:NO
                event:event
                point:point];
    return YES;
  }
  return NO;
}

- (void)mouseDown:(NSEvent *)event {
  if (!_plugin)
    return;
  if (_page == kFormantPageRoute && ![self phraseIsEditing]) {
    [[self window] makeFirstResponder:self];
  }
  const NSPoint point =
      [self convertPoint:[event locationInWindow] fromView:nil];
  if (_openMenu != CLAP_INVALID_ID) {
    const ParamDef *def = paramDef(_openMenu);
    const int hit = s3g::clap_gui::dropdownHitIndex(point, [self openMenuRect],
                                                    kFormantMenuItemHeight,
                                                    [self openMenuItemCount]);
    if (hit >= 0 && def) {
      if (!queueFormantParamValue(
              *_plugin, def->id,
              def->minimum + static_cast<double>(hit)))
        NSBeep();
    }
    _openMenu = CLAP_INVALID_ID;
    _hoverMenuItem = -1;
    [self syncPhraseFieldVisibility];
    [self setNeedsDisplay:YES];
    return;
  }

  const auto titleBand = s3g::gui_layout::matrixTitleBand(kFormantCanvas);
  if (NSPointInRect(point, s3g::clap_gui::cocoaRect(titleBand.presetMenu))) {
    [self openMenuForParam:kPresetParamId
                       box:s3g::clap_gui::cocoaRect(titleBand.presetMenu)];
    return;
  }
  if (s3g::clap_gui::handleProcessorTitleClick(
          point, &_plugin->plugin, @"Formant Matrix", titleBand,
          _titlePresetName, sizeof(_titlePresetName), kOutputParamId)) {
    [self setNeedsDisplay:YES];
    return;
  }
  for (uint32_t page = 0u; page < kFormantPageCount; ++page) {
    if (!NSPointInRect(point, formantPageButtonRect(page)))
      continue;
    if (_page == kFormantPagePhrase && page != _page &&
        [self phraseIsEditing]) {
      [[self window] makeFirstResponder:self];
    }
    [self finishDrag];
    _page = page;
    [self syncPhraseFieldVisibility];
    [self setNeedsDisplay:YES];
    return;
  }

  if (_page == kFormantPageRoute) {
    if (NSPointInRect(point, formantSceneButtonRect(false)) ||
        NSPointInRect(point, formantSceneButtonRect(true))) {
      _editingSceneB = NSPointInRect(point, formantSceneButtonRect(true));
      [self setNeedsDisplay:YES];
      return;
    }
    for (uint32_t operation = 0u; operation < 4u; ++operation) {
      if (!NSPointInRect(point, formantSceneOperationRect(operation)))
        continue;
      [self applySceneOperation:operation];
      return;
    }
    for (uint32_t destination = 0u; destination < kMatrixBands; ++destination) {
      for (uint32_t source = 0u; source < kMatrixBands; ++source) {
        if (!NSPointInRect(point, formantMatrixCellRect(destination, source)))
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
          if (([event modifierFlags] & NSEventModifierFlagOption) != 0u) {
            next = -std::abs(next);
          }
          if (![self prepareMatrixCellEdit])
            return;
          if (!queueFormantParamValue(*_plugin, id, next))
            NSBeep();
        }
        [self setNeedsDisplay:YES];
        return;
      }
    }
    for (uint32_t band = 0u; band < kMatrixBands; ++band) {
      if (!NSPointInRect(point, formantBandTrimHitRect(band)))
        continue;
      [self beginDrag:bandTrimParamId(band)
           trackStart:748.0
          trackLength:82.0
             vertical:YES
                event:event
                point:point];
      return;
    }
    const clap_id selectedId =
        matrixParamId(_editingSceneB, _selectedDestination, _selectedSource);
    if (NSPointInRect(
            point, formantParamHitRect(kFormantCrosspointPanel, 0u, false))) {
      if (![self prepareMatrixCellEdit])
        return;
      [self beginDrag:selectedId
           trackStart:s3g::gui_layout::processorControlX(
                          kFormantCrosspointPanel.frame.x)
          trackLength:s3g::gui_layout::processorTrackWidth(
                          kFormantCrosspointPanel.frame.width)
             vertical:NO
                event:event
                point:point];
      return;
    }
    if (NSPointInRect(point, formantCrosspointActionRect(0u))) {
      if (![self prepareMatrixCellEdit])
        return;
      const double current = loadValue(*_plugin, selectedId);
      if (!queueFormantParamValue(
              *_plugin, selectedId,
              std::abs(current) < 0.001 ? 0.25 : -current))
        NSBeep();
      [self setNeedsDisplay:YES];
      return;
    }
    if (NSPointInRect(point, formantCrosspointActionRect(1u))) {
      if (![self prepareMatrixCellEdit])
        return;
      if (!queueFormantParamValue(*_plugin, selectedId, 0.0))
        NSBeep();
      [self setNeedsDisplay:YES];
      return;
    }
  }

  if (_page == kFormantPagePhrase &&
      NSPointInRect(point, formantCompileRect())) {
    if ([self phraseIsEditing]) {
      // Ending the field edit invokes controlTextDidEndEditing:, which owns
      // this commit. Avoid publishing the unchanged phrase again later.
      [[self window] makeFirstResponder:self];
    } else {
      [self commitPhrase:self];
    }
    return;
  }
  [self handleVisibleParamAtPoint:point event:event];
}

- (void)mouseDragged:(NSEvent *)event {
  const NSPoint point =
      [self convertPoint:[event locationInWindow] fromView:nil];
  [self updateMenuHover:point];
  [self updateDrag:point];
}

- (void)mouseMoved:(NSEvent *)event {
  [self updateMenuHover:[self convertPoint:[event locationInWindow]
                                  fromView:nil]];
}

- (void)mouseUp:(NSEvent *)event {
  (void)event;
  [self finishDrag];
}

- (void)keyDown:(NSEvent *)event {
  if (_page == kFormantPageRoute) {
    NSString *key = [event charactersIgnoringModifiers];
    if ([key length] > 0u) {
      const unichar character = [key characterAtIndex:0u];
      if (character == NSLeftArrowFunctionKey && _selectedSource > 0u) {
        --_selectedSource;
      } else if (character == NSRightArrowFunctionKey &&
                 _selectedSource + 1u < kMatrixBands) {
        ++_selectedSource;
      } else if (character == NSUpArrowFunctionKey &&
                 _selectedDestination > 0u) {
        --_selectedDestination;
      } else if (character == NSDownArrowFunctionKey &&
                 _selectedDestination + 1u < kMatrixBands) {
        ++_selectedDestination;
      } else if (character == NSDeleteCharacter ||
                 character == NSBackspaceCharacter) {
        if (![self prepareMatrixCellEdit])
          return;
        if (!queueFormantParamValue(
                *_plugin,
                matrixParamId(_editingSceneB, _selectedDestination,
                              _selectedSource),
                0.0))
          NSBeep();
      } else {
        [super keyDown:event];
        return;
      }
      [self setNeedsDisplay:YES];
      return;
    }
  }
  [super keyDown:event];
}

- (void)commitPhrase:(id)sender {
  (void)sender;
  if (!_plugin)
    return;
  if (_plugin->guiParamEvents.available() < 3u) {
    NSBeep();
    return;
  }
  const bool published = publishTextPhrase(
      *_plugin, [[_phraseField stringValue] UTF8String]);
  if (published) {
    if (!queueFormantParamValue(*_plugin, kGestureSequenceParamId, 5.0))
      NSBeep();
  } else {
    NSBeep();
  }
  [self setNeedsDisplay:YES];
}

- (void)controlTextDidBeginEditing:(NSNotification *)notification {
  if ([notification object] == _phraseField) {
    s3g::clap_gui::styleNumberTextEditor(_phraseField);
  }
}

- (void)controlTextDidEndEditing:(NSNotification *)notification {
  if ([notification object] == _phraseField)
    [self commitPhrase:_phraseField];
}

- (BOOL)control:(NSControl *)control
               textView:(NSTextView *)textView
    doCommandBySelector:(SEL)selector {
  (void)textView;
  if (control == _phraseField && selector == @selector(insertNewline:)) {
    [[self window] makeFirstResponder:self];
    return YES;
  }
  return NO;
}

- (void)refresh:(NSTimer *)timer {
  (void)timer;
  if ([self isHidden] || !_plugin || !s3g::clap_support::hostAppIsActive())
    return;
  if (![self phraseIsEditing]) {
    const PhraseState phrase = loadPhrase(*_plugin);
    NSString *current = formantPhraseString(phrase);
    if (![[_phraseField stringValue] isEqualToString:current]) {
      [_phraseField setStringValue:current];
    }
  }
  [self syncPhraseFieldVisibility];
  [self setNeedsDisplay:YES];
}

- (void)startRefreshTimer {
  if (_timer)
    return;
  _timer = [NSTimer timerWithTimeInterval:1.0 / 20.0
                                   target:self
                                 selector:@selector(refresh:)
                                 userInfo:nil
                                  repeats:YES];
  [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer {
  if (_timer) {
    [_timer invalidate];
    _timer = nil;
  }
}

@end
