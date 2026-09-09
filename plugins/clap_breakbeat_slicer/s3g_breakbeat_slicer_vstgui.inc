// Literal Cocoa-to-VSTGUI adaptation: the four page layouts and the bank,
// marker, mixer and mutation actions below retain their original geometry and
// state transitions. Host/window/font services live in the common foundation.
namespace slicer_canvas {
#include "s3g_slicer_canvas_drawing.inc"
CRect bankRowRect(uint32_t index) {
  return box(24.0, 101.0 + static_cast<double>(index) * 100.0, 238.0, 92.0);
}

CRect bankPanelRect() { return box(18.0, 74.0, 250.0, 432.0); }

CRect slotChannelRect(uint32_t index) {
  const CRect row = bankRowRect(index);
  return box(row.left + 8.0, row.top + 64.0, 82.0, 19.0);
}

CRect slotAutomapRect(uint32_t index) {
  const CRect row = bankRowRect(index);
  return box(row.left + 98.0, row.top + 64.0, 132.0, 19.0);
}

CRect overviewWaveformRect(uint32_t index) {
  return box(292.0, 74.0 + static_cast<double>(index) * 112.0, 770.0, 104.0);
}

CRect waveformRect() { return box(294.0, 101.0, 756.0, 245.0); }

CRect waveformNavigatorRect() { return box(294.0, 350.0, 756.0, 8.0); }

CRect sampleToolboxRect() { return box(282.0, 74.0, 780.0, 328.0); }

CRect sampleLoadButtonRect() { return box(816.0, 77.0, 66.0, 17.0); }

CRect sampleClearButtonRect() { return box(888.0, 77.0, 66.0, 17.0); }

CRect sliceMapToolboxRect() { return box(282.0, 414.0, 380.0, 164.0); }

CRect playbackToolboxRect() { return box(674.0, 414.0, 388.0, 164.0); }

enum DetailMenuKind : int64_t {
  kDetailMenuNone = -1,
  kDetailMenuMidiChannel = 0,
  kDetailMenuSliceMethod,
  kDetailMenuStartNote,
  kDetailMenuTrigger,
  kDetailMenuRetrigger,
  kDetailMenuVoice,
  kDetailMenuPitch,
  kDetailMenuSync,
  kDetailMenuSliceLaunch,
  kDetailMenuSliceChoke,
};

enum DetailNumericControl : int64_t {
  kDetailNumericNone = -1,
  kDetailNumericPreRoll = 0,
  kDetailNumericMinimumSlice,
  kDetailNumericSourceTempo,
  kDetailNumericLoopCrossfade,
  kDetailNumericGlide,
  kDetailNumericSliceGain,
  kDetailNumericSlicePitch,
  kDetailNumericSlicePan,
  kDetailNumericCount,
};

constexpr int64_t kDetailNumericFieldTagBase = 4100;

CRect selectedSliceToolboxRect() { return box(282.0, 590.0, 510.0, 100.0); }

CRect timingToolboxRect() { return box(804.0, 590.0, 258.0, 100.0); }

CRect processorMenuRect(CRect panel, double y) {
  return box(
      static_cast<double>(s3g::gui_layout::processorControlX(panel.left)),
      y - 1.0,
      static_cast<double>(
          s3g::gui_layout::processorMenuWidth(panel.getWidth())),
      15.0);
}

CRect sliceMethodMenuRect() {
  return processorMenuRect(sliceMapToolboxRect(), 448.0);
}

double detailNumericY(uint32_t index) {
  static constexpr double rows[]{
      474.0, 500.0, 624.0, 650.0, 676.0,
  };
  return rows[std::min<uint32_t>(index,
                                 static_cast<uint32_t>(std::size(rows) - 1u))];
}

bool isSlicePropertyNumeric(uint32_t index) noexcept {
  return index >= kDetailNumericSliceGain && index <= kDetailNumericSlicePan;
}

uint32_t slicePropertyForNumeric(uint32_t index) noexcept {
  return index - static_cast<uint32_t>(kDetailNumericSliceGain);
}

double slicePropertyGroupX(uint32_t property) {
  return 296.0 + static_cast<double>(property) * 158.0;
}

constexpr double kSlicePropertyGroupWidth = 156.0;
constexpr double kSlicePropertySliderY = 624.0;

CRect slicePropertyTrackRect(uint32_t property) {
  return mixerStripSliderTrackRect(slicePropertyGroupX(property),
                                   kSlicePropertyGroupWidth,
                                   kSlicePropertySliderY);
}

CRect slicePropertyHitRect(uint32_t property) {
  return mixerStripSliderHitRect(slicePropertyGroupX(property),
                                 kSlicePropertyGroupWidth,
                                 kSlicePropertySliderY);
}

CRect slicePropertyFieldRect(uint32_t property) {
  constexpr double valueWidth = 40.0;
  return box(slicePropertyGroupX(property) + kSlicePropertyGroupWidth -
                 static_cast<double>(
                     s3g::gui_layout::kStandardMetrics.panelRightInset) -
                 valueWidth,
             kSlicePropertySliderY - 2.0, valueWidth, 16.0);
}

CRect detailNumericPanel(uint32_t index) {
  if (isSlicePropertyNumeric(index))
    return selectedSliceToolboxRect();
  return index < 2u ? sliceMapToolboxRect() : timingToolboxRect();
}

CRect detailNumericTrackRect(uint32_t index) {
  if (isSlicePropertyNumeric(index))
    return slicePropertyTrackRect(slicePropertyForNumeric(index));
  const CRect panel = detailNumericPanel(index);
  return box(
      static_cast<double>(s3g::gui_layout::processorControlX(minX(panel))),
      detailNumericY(index) + 1.0,
      static_cast<double>(
          s3g::gui_layout::processorTrackWidth(rectWidth(panel))),
      9.0);
}

CRect detailNumericHitRect(uint32_t index) {
  if (isSlicePropertyNumeric(index))
    return slicePropertyHitRect(slicePropertyForNumeric(index));
  const CRect panel = detailNumericPanel(index);
  return box(
      minX(panel) +
          static_cast<double>(s3g::gui_layout::kStandardMetrics.hitInset),
      detailNumericY(index) - 8.0,
      rectWidth(panel) -
          2.0 * static_cast<double>(s3g::gui_layout::kStandardMetrics.hitInset),
      static_cast<double>(s3g::gui_layout::kStandardMetrics.hitHeight));
}

CRect detailNumericFieldRect(uint32_t index) {
  if (isSlicePropertyNumeric(index))
    return slicePropertyFieldRect(slicePropertyForNumeric(index));
  const CRect panel = detailNumericPanel(index);
  return box(static_cast<double>(s3g::gui_layout::processorValueX(
                 minX(panel), rectWidth(panel))),
             detailNumericY(index) - 2.0,
             static_cast<double>(
                 s3g::gui_layout::kStandardMetrics.processorValueWidth),
             16.0);
}

CRect startNoteMenuRect() {
  return processorMenuRect(sliceMapToolboxRect(), 526.0);
}

CRect automapButtonRect() {
  const CRect panel = sliceMapToolboxRect();
  return box(maxX(panel) - 88.0, panel.top + 3.0, 76.0, 17.0);
}

CRect sliceActionButtonRect() {
  const CRect automap = automapButtonRect();
  return box(minX(automap) - 62.0, minY(automap), 56.0, rectHeight(automap));
}

CRect playbackMenuRect(uint32_t index) {
  return processorMenuRect(playbackToolboxRect(),
                           448.0 + static_cast<double>(index) * 26.0);
}

CRect sliceLaunchMenuRect() { return box(360.0, 650.0, 154.0, 15.0); }

CRect sliceReverseButtonRect() { return box(532.0, 648.0, 76.0, 19.0); }

CRect sliceChokeMenuRect() { return box(688.0, 650.0, 84.0, 15.0); }

std::string const kSlicerMethodItems[] = {
    "EQUAL 4", "EQUAL 8", "EQUAL 16", "EQUAL 32", "TRANSIENT",
};

std::string const kSlicerTriggerItems[] = {
    "AUTO",
    "GATE",
    "ONE SHOT",
    "TOGGLE",
};

std::string const kSlicerRetriggerItems[] = {
    "LAYER",
    "RESTART",
    "IGNORE",
};

std::string const kSlicerVoiceItems[] = {"POLY", "MONO", "LEGATO"};

std::string const kSlicerPitchItems[] = {
    "RATE",
    "STRETCH",
    "RATE BELOW / STRETCH ABOVE",
};

std::string const kSlicerSyncItems[] = {"FREE", "HOST"};

std::string const kSlicerLaunchItems[] = {
    "ONE SHOT", "GATE", "THRU", "LOOP", "PING PONG",
};

CRect detailMenuControlRect(int64_t kind, uint32_t slotIndex) {
  switch (kind) {
  case kDetailMenuMidiChannel:
    return slotChannelRect(slotIndex);
  case kDetailMenuSliceMethod:
    return sliceMethodMenuRect();
  case kDetailMenuStartNote:
    return startNoteMenuRect();
  case kDetailMenuTrigger:
    return playbackMenuRect(0u);
  case kDetailMenuRetrigger:
    return playbackMenuRect(1u);
  case kDetailMenuVoice:
    return playbackMenuRect(2u);
  case kDetailMenuPitch:
    return playbackMenuRect(3u);
  case kDetailMenuSync:
    return playbackMenuRect(4u);
  case kDetailMenuSliceLaunch:
    return sliceLaunchMenuRect();
  case kDetailMenuSliceChoke:
    return sliceChokeMenuRect();
  default:
    return CRect{};
  }
}

uint32_t detailMenuColumns(int64_t kind) {
  if (kind == kDetailMenuStartNote)
    return 8u;
  if (kind == kDetailMenuMidiChannel || kind == kDetailMenuSliceChoke)
    return 2u;
  return 1u;
}

CRect detailMenuDropdownRect(int64_t kind, uint32_t itemCount,
                             uint32_t slotIndex) {
  constexpr double itemHeight = 20.0;
  const uint32_t columns = detailMenuColumns(kind);
  const uint32_t rows = multiColumnMenuRows(itemCount, columns);
  const CRect control = detailMenuControlRect(kind, slotIndex);
  double width = rectWidth(control);
  if (kind == kDetailMenuStartNote)
    width = 640.0;
  else if (kind == kDetailMenuMidiChannel)
    width = 300.0;
  else if (kind == kDetailMenuSliceChoke)
    width = 240.0;
  const double height = itemHeight * static_cast<double>(rows);
  double x = minX(control);
  if (kind == kDetailMenuStartNote)
    x = std::clamp<double>(maxX(control) - width, 18.0,
                           static_cast<double>(kGuiWidth) - width - 18.0);
  else
    x = std::clamp<double>(x, 18.0,
                           static_cast<double>(kGuiWidth) - width - 18.0);
  double y = maxY(control) + 1.0;
  if (y + height > static_cast<double>(kGuiHeight) - 24.0)
    y = std::max<double>(42.0, minY(control) - height - 1.0);
  return box(x, y, width, height);
}

CRect editorTabRect(uint32_t index) {
  constexpr double widths[]{82.0, 96.0, 68.0, 72.0};
  double x = 292.0;
  for (uint32_t i = 0u; i < index; ++i)
    x += widths[i] + 6.0;
  return box(x, 45.0, widths[index], 18.0);
}

CRect mutateWaveformRect() { return box(292.0, 74.0, 770.0, 164.0); }

CRect mutateSlotRect(uint32_t index) {
  return box(292.0 + static_cast<double>(index) * 194.5, 250.0, 184.5, 64.0);
}

CRect mutateActionButtonRect(uint32_t index) {
  constexpr double widths[]{220.0, 156.0, 112.0, 112.0};
  double x = 292.0;
  for (uint32_t i = 0u; i < index; ++i)
    x += widths[i] + 8.0;
  return box(x, 326.0, widths[index], 24.0);
}

CRect mutateUsesPanelRect() { return box(292.0, 362.0, 770.0, 94.0); }

CRect mutateUseRect(uint32_t index) {
  return box(310.0 + static_cast<double>(index) * 122.0, 396.0, 112.0, 22.0);
}

CRect mutateOperationPanelRect() { return box(292.0, 468.0, 770.0, 108.0); }

CRect mutateOperationRect(uint32_t index) {
  return box(310.0 + static_cast<double>(index) * 144.0, 502.0, 136.0, 22.0);
}

CRect mutateOperationAlternateRect() { return box(310.0, 538.0, 226.0, 22.0); }

CRect embedAudioButtonRect() { return box(858.0, 45.0, 204.0, 18.0); }

CRect killAllButtonRect() { return box(774.0, 45.0, 78.0, 18.0); }

CRect keyboardRect() { return box(282.0, 704.0, 780.0, 50.0); }

CRect envelopePanelRect() { return box(18.0, 518.0, 250.0, 236.0); }

double envelopeSliderY(uint32_t index) {
  return 576.0 + static_cast<double>(index) * 42.0;
}

CRect envelopeSliderHitRect(uint32_t index) {
  const CRect panel = envelopePanelRect();
  return mixerStripSliderHitRect(minX(panel), rectWidth(panel),
                                 envelopeSliderY(index));
}

CRect envelopeSliderTrackRect(uint32_t index) {
  const CRect panel = envelopePanelRect();
  return mixerStripSliderTrackRect(minX(panel), rectWidth(panel),
                                   envelopeSliderY(index));
}

CRect mixerStripRect(uint32_t index) {
  return box(18.0 + static_cast<double>(index) * 166.0, 74.0, 160.0, 674.0);
}

CRect mixerMainRect() { return box(690.0, 74.0, 372.0, 130.0); }

CRect mixerAuxBusRect() { return box(690.0, 216.0, 372.0, 532.0); }

CRect mixerInsertTypeRect(uint32_t type) {
  const CRect panel = mixerAuxBusRect();
  const uint32_t column = type % 5u;
  const uint32_t row = type / 5u;
  return box(minX(panel) + 16.0 + static_cast<double>(column) * 68.0,
             minY(panel) + 36.0 + static_cast<double>(row) * 32.0, 64.0, 25.0);
}

CRect mixerInsertBypassRect() {
  const CRect panel = mixerAuxBusRect();
  return box(minX(panel) + 16.0, minY(panel) + 104.0, 104.0, 25.0);
}

CRect mixerInsertSwapRect() {
  const CRect panel = mixerAuxBusRect();
  return box(minX(panel) + 132.0, minY(panel) + 104.0, 104.0, 25.0);
}

CRect mixerInsertCloseRect() {
  const CRect panel = mixerAuxBusRect();
  return box(minX(panel) + 248.0, minY(panel) + 104.0, 108.0, 25.0);
}

CRect mixerInsertOptionRect() {
  const CRect panel = mixerAuxBusRect();
  return box(minX(panel) + 16.0, minY(panel) + 140.0, rectWidth(panel) - 32.0,
             25.0);
}

double mixerInsertParameterY(uint32_t parameter) {
  return minY(mixerAuxBusRect()) + 204.0 +
         static_cast<double>(parameter) * 64.0;
}

CRect mixerInsertParameterHitRect(uint32_t parameter) {
  const CRect panel = mixerAuxBusRect();
  return box(minX(panel) + 12.0, mixerInsertParameterY(parameter) + 17.0,
             rectWidth(panel) - 24.0, 28.0);
}

CRect mixerInsertParameterTrackRect(uint32_t parameter) {
  const CRect panel = mixerAuxBusRect();
  return box(minX(panel) + 16.0, mixerInsertParameterY(parameter) + 27.0,
             rectWidth(panel) - 32.0, 9.0);
}

CRect mixerDialRect(uint32_t index, uint32_t dial) {
  const CRect strip = mixerStripRect(index);
  return box(minX(strip) + 19.0 + (dial % 2u) * 76.0,
             minY(strip) + 72.0 + (dial / 2u) * 72.0, 50.0, 62.0);
}

CRect mixerInsertRect(uint32_t index, uint32_t insert) {
  const CRect strip = mixerStripRect(index);
  return box(minX(strip) + 84.0,
             minY(strip) + 218.0 + static_cast<double>(insert) * 32.0, 66.0,
             25.0);
}

double mixerAuxY(uint32_t index) { return minY(mixerStripRect(index)) + 296.0; }

CRect mixerAuxHitRect(uint32_t index) {
  const CRect strip = mixerStripRect(index);
  return mixerStripSliderHitRect(minX(strip), rectWidth(strip),
                                 mixerAuxY(index));
}

CRect mixerAuxTrackRect(uint32_t index) {
  const CRect strip = mixerStripRect(index);
  return mixerStripSliderTrackRect(minX(strip), rectWidth(strip),
                                   mixerAuxY(index));
}

CRect mixerMuteRect(uint32_t index) {
  const CRect strip = mixerStripRect(index);
  return box(minX(strip) + 14.0, minY(strip) + 326.0, 61.0, 25.0);
}

CRect mixerSoloRect(uint32_t index) {
  const CRect strip = mixerStripRect(index);
  return box(maxX(strip) - 75.0, minY(strip) + 326.0, 61.0, 25.0);
}

CRect mixerAuditionRect(uint32_t index) {
  const CRect strip = mixerStripRect(index);
  return box(minX(strip) + 14.0, minY(strip) + 360.0, rectWidth(strip) - 28.0,
             25.0);
}

CRect mixerFaderRect(uint32_t index) {
  const CRect strip = mixerStripRect(index);
  return box(minX(strip) + 24.0, minY(strip) + 425.0, 60.0, 183.0);
}

CRect mixerPeakRect(uint32_t index) {
  const CRect fader = mixerFaderRect(index);
  return box(minX(mixerStripRect(index)) + 119.0, minY(fader), 11.0,
             rectHeight(fader));
}

double mixerOutputSliderY() { return minY(mixerMainRect()) + 48.0; }

CRect mixerOutputHitRect() {
  const CRect panel = mixerMainRect();
  return mixerStripSliderHitRect(minX(panel), rectWidth(panel),
                                 mixerOutputSliderY());
}

CRect mixerOutputTrackRect() {
  const CRect panel = mixerMainRect();
  return mixerStripSliderTrackRect(minX(panel), rectWidth(panel),
                                   mixerOutputSliderY());
}

CRect mixerUnityRect() {
  const CRect panel = mixerMainRect();
  return box(maxX(panel) - 105.0, minY(panel) + 82.0, 88.0, 25.0);
}

CRect mixerBusEnableRect() {
  const CRect panel = mixerAuxBusRect();
  return box(minX(panel) + 16.0, minY(panel) + 34.0, rectWidth(panel) - 32.0,
             25.0);
}

CRect mixerBusLinkRect() {
  const CRect panel = mixerAuxBusRect();
  return box(minX(panel) + 16.0, minY(panel) + 478.0, 158.0, 25.0);
}

CRect mixerBusFieldSafeRect() {
  const CRect panel = mixerAuxBusRect();
  return box(minX(panel) + 188.0, minY(panel) + 478.0, 168.0, 25.0);
}

double mixerBusSliderY(uint32_t row) {
  return minY(mixerAuxBusRect()) + 92.0 + static_cast<double>(row) * 43.0;
}

CRect mixerBusSliderHitRect(uint32_t row) {
  const CRect panel = mixerAuxBusRect();
  return mixerStripSliderHitRect(minX(panel), rectWidth(panel),
                                 mixerBusSliderY(row));
}

CRect mixerBusSliderTrackRect(uint32_t row) {
  const CRect panel = mixerAuxBusRect();
  return mixerStripSliderTrackRect(minX(panel), rectWidth(panel),
                                   mixerBusSliderY(row));
}

std::string noteText(uint8_t note) {
  static const std::vector<std::string> names{
      "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return fmt("%s%d / %u", names[note % 12u], static_cast<int>(note / 12u) - 2,
             static_cast<unsigned>(note));
}

std::string shortNoteText(uint8_t note) {
  static const std::vector<std::string> names{
      "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return fmt("%s%d", names[note % 12u], static_cast<int>(note / 12u) - 2);
}

std::string launchModeText(s3g::breakbeat::LaunchMode mode) {
  switch (mode) {
  case s3g::breakbeat::LaunchMode::OneShot:
    return "ONE SHOT";
  case s3g::breakbeat::LaunchMode::Gate:
    return "GATE";
  case s3g::breakbeat::LaunchMode::Thru:
    return "THRU";
  case s3g::breakbeat::LaunchMode::Loop:
    return "LOOP";
  case s3g::breakbeat::LaunchMode::PingPong:
    return "PING PONG";
  }
  return "ONE SHOT";
}

std::string triggerModeText(s3g::breakbeat::TriggerMode mode) {
  switch (mode) {
  case s3g::breakbeat::TriggerMode::Auto:
    return "AUTO";
  case s3g::breakbeat::TriggerMode::Gate:
    return "GATE";
  case s3g::breakbeat::TriggerMode::OneShot:
    return "ONE SHOT";
  case s3g::breakbeat::TriggerMode::Toggle:
    return "TOGGLE";
  }
  return "AUTO";
}

std::string retriggerModeText(s3g::breakbeat::RetriggerMode mode) {
  switch (mode) {
  case s3g::breakbeat::RetriggerMode::Layer:
    return "LAYER";
  case s3g::breakbeat::RetriggerMode::Restart:
    return "RESTART";
  case s3g::breakbeat::RetriggerMode::Ignore:
    return "IGNORE";
  }
  return "RESTART";
}

std::string voiceModeText(s3g::breakbeat::VoiceMode mode) {
  switch (mode) {
  case s3g::breakbeat::VoiceMode::Poly:
    return "POLY";
  case s3g::breakbeat::VoiceMode::Mono:
    return "MONO";
  case s3g::breakbeat::VoiceMode::Legato:
    return "LEGATO";
  }
  return "POLY";
}

std::string pitchModeText(s3g::breakbeat::PitchMode mode) {
  switch (mode) {
  case s3g::breakbeat::PitchMode::Rate:
    return "RATE";
  case s3g::breakbeat::PitchMode::Stretch:
    return "STRETCH";
  case s3g::breakbeat::PitchMode::RateBelowStretchAbove:
    return "R↓ / S↑";
  }
  return "RATE";
}

std::string syncModeText(s3g::breakbeat::SyncMode mode) {
  return mode == s3g::breakbeat::SyncMode::Host ? "HOST" : "FREE";
}

std::string insertTypeText(InsertType type) {
  switch (type) {
  case InsertType::Filter:
    return "FILTER";
  case InsertType::Degrade:
    return "DEGRADE";
  case InsertType::Transient:
    return "TRANSIENT";
  case InsertType::Resonator:
    return "RESONATOR";
  case InsertType::Erosion:
    return "EROSION";
  case InsertType::Shifter:
    return "SHIFT";
  case InsertType::Wavefolder:
    return "FOLD";
  case InsertType::Repeater:
    return "REPEATER";
  case InsertType::TimeMangler:
    return "TIME";
  case InsertType::Off:
    return "OFF";
  }
  return "OFF";
}

std::string insertTypeShortText(InsertType type) {
  switch (type) {
  case InsertType::Filter:
    return "FLT";
  case InsertType::Degrade:
    return "DGR";
  case InsertType::Transient:
    return "TRN";
  case InsertType::Resonator:
    return "RSN";
  case InsertType::Erosion:
    return "ERO";
  case InsertType::Shifter:
    return "SHF";
  case InsertType::Wavefolder:
    return "FLD";
  case InsertType::Repeater:
    return "RPT";
  case InsertType::TimeMangler:
    return "TIM";
  case InsertType::Off:
    return "OFF";
  }
  return "OFF";
}

std::string filterModeText(FilterMode mode) {
  switch (mode) {
  case FilterMode::LowPass:
    return "LOW PASS";
  case FilterMode::BandPass:
    return "BAND PASS";
  case FilterMode::HighPass:
    return "HIGH PASS";
  case FilterMode::Notch:
    return "NOTCH";
  }
  return "LOW PASS";
}

std::string insertParameterLabel(const InsertSettings &insert,
                                 uint32_t parameter) {
  static const std::vector<std::string> filter{"CUTOFF", "RESONANCE", "DRIVE",
                                               "MIX"};
  static const std::vector<std::string> degrade{"RATE DIVIDE", "BIT DEPTH",
                                                "JITTER", "MIX"};
  static const std::vector<std::string> transient{"ATTACK", "SUSTAIN", "GATE",
                                                  "MIX"};
  static const std::vector<std::string> resonator{"TUNE", "FEEDBACK", "DAMPING",
                                                  "AMOUNT"};
  static const std::vector<std::string> erosion{"MOD RATE", "DEPTH", "FEEDBACK",
                                                "MIX"};
  static const std::vector<std::string> shifter{"FREQUENCY", "REGEN", "COLOR",
                                                "MIX"};
  static const std::vector<std::string> wavefolder{"DRIVE", "BIAS", "SHAPE",
                                                   "MIX"};
  static const std::vector<std::string> repeater{"BUFFER", "REPEATS",
                                                 "PITCH DECAY", "MIX"};
  if (parameter >= s3g::breakbeat::kInsertParameterCount)
    return "";
  if (insert.type == InsertType::TimeMangler) {
    if (parameter == 0u)
      return "WINDOW";
    if (parameter == 1u)
      return "PITCH";
    if (parameter == 2u)
      return insert.variant == 0u ? "RELEASE"
                                  : insert.variant == 1u ? "DECAY" : "BRAKE";
    return "MIX";
  }
  switch (insert.type) {
  case InsertType::Filter:
    return filter[parameter];
  case InsertType::Degrade:
    return degrade[parameter];
  case InsertType::Transient:
    return transient[parameter];
  case InsertType::Resonator:
    return resonator[parameter];
  case InsertType::Erosion:
    return erosion[parameter];
  case InsertType::Shifter:
    return shifter[parameter];
  case InsertType::Wavefolder:
    return wavefolder[parameter];
  case InsertType::Repeater:
    return repeater[parameter];
  case InsertType::TimeMangler:
    break;
  case InsertType::Off:
    return "UNASSIGNED";
  }
  return "";
}

std::string insertParameterValue(const InsertSettings &insert,
                                 uint32_t parameter) {
  if (parameter >= insert.values.size())
    return "";
  const float value = insert.values[parameter];
  if ((insert.type == InsertType::Repeater ||
       insert.type == InsertType::TimeMangler) &&
      parameter == 0u) {
    static const std::vector<std::string> windows{"8 ms",   "16 ms",  "32 ms",
                                                  "64 ms",  "125 ms", "250 ms",
                                                  "500 ms", "1000 ms"};
    const size_t index =
        static_cast<size_t>(std::clamp(std::lround(value * 7.0f), 0l, 7l));
    return windows[index];
  }
  if (insert.type == InsertType::Repeater && parameter == 1u) {
    const uint32_t repeats =
        1u + static_cast<uint32_t>(std::lround(value * 15.0f));
    return fmt("× %u", repeats);
  }
  if (insert.type == InsertType::Repeater && parameter == 2u) {
    return fmt("-%.1f st / repeat", value * 12.0f);
  }
  if (insert.type == InsertType::TimeMangler && parameter == 1u) {
    const float semitones = (value * 2.0f - 1.0f) * 24.0f;
    return fmt("%+.1f st", semitones);
  }
  if (insert.type == InsertType::TimeMangler && parameter == 2u) {
    if (insert.variant == 0u) {
      const float milliseconds = 2.0f * std::pow(100.0f, value);
      return fmt("%.0f ms", milliseconds);
    }
    if (insert.variant == 1u) {
      const float seconds = 0.125f * std::pow(128.0f, value);
      return seconds < 1.0f ? fmt("%.0f ms", seconds * 1000.0f)
                            : fmt("%.2f s", seconds);
    }
    return fmt("%.0f%%", value * 100.0f);
  }
  if (insert.type == InsertType::Filter && parameter == 0u) {
    const float frequency = 30.0f * std::pow(20000.0f / 30.0f, value);
    return frequency >= 1000.0f ? fmt("%.2f kHz", frequency / 1000.0f)
                                : fmt("%.0f Hz", frequency);
  }
  if (insert.type == InsertType::Filter && parameter == 1u) {
    const float q = 0.5f + 15.5f * value * value;
    return fmt("Q %.2f", q);
  }
  if (insert.type == InsertType::Degrade && parameter == 0u) {
    const uint32_t period =
        1u + static_cast<uint32_t>(std::lround(value * value * 95.0f));
    return fmt("÷ %u", period);
  }
  if (insert.type == InsertType::Degrade && parameter == 1u) {
    const uint32_t bits =
        4u + static_cast<uint32_t>(std::lround(value * 12.0f));
    return fmt("%u BIT", bits);
  }
  if (insert.type == InsertType::Transient &&
      (parameter == 0u || parameter == 1u))
    return fmt("%+.0f%%", (value * 2.0f - 1.0f) * 100.0f);
  if (insert.type == InsertType::Transient && parameter == 2u) {
    if (value <= 0.002f)
      return "OFF";
    return fmt("%+.1f dB", -72.0f + value * 48.0f);
  }
  if (insert.type == InsertType::Resonator && parameter == 0u) {
    const float frequency = 40.0f * std::pow(100.0f, value);
    return frequency >= 1000.0f ? fmt("%.2f kHz", frequency / 1000.0f)
                                : fmt("%.0f Hz", frequency);
  }
  if (insert.type == InsertType::Erosion && parameter == 0u) {
    const float frequency = 10.0f * std::pow(1600.0f, value);
    return frequency >= 1000.0f ? fmt("%.2f kHz", frequency / 1000.0f)
                                : fmt("%.0f Hz", frequency);
  }
  if (insert.type == InsertType::Shifter && parameter == 0u) {
    float frequency = 0.0f;
    if (insert.variant == 0u) {
      const float bipolar = value * 2.0f - 1.0f;
      frequency = std::copysign(bipolar * bipolar * 4000.0f, bipolar);
      return fmt("%+.0f Hz", frequency);
    }
    frequency = 20.0f * std::pow(1000.0f, value);
    return frequency >= 1000.0f ? fmt("%.2f kHz", frequency / 1000.0f)
                                : fmt("%.0f Hz", frequency);
  }
  if (insert.type == InsertType::Wavefolder && parameter == 0u) {
    const float drive = 1.0f + value * value * 31.0f;
    return fmt("%.1f×", drive);
  }
  if (insert.type == InsertType::Wavefolder && parameter == 1u)
    return fmt("%+.0f%%", (value * 2.0f - 1.0f) * 100.0f);
  if (insert.type == InsertType::Filter && parameter == 2u)
    return fmt("%.0f%%", value * 100.0f);
  return fmt("%.0f%%", value * 100.0f);
}

std::string insertSafetyText(InsertType type) {
  switch (type) {
  case InsertType::Filter:
    return "SHARED COEFFICIENTS  //  DRIVE ADDS NONLINEAR COLOR";
  case InsertType::Degrade:
    return "LOCKED HOLD CLOCK  //  NONLINEAR COLOR PROCESS";
  case InsertType::Transient:
    return "ONE LINKED DETECTOR CONTROLS EVERY SOURCE CHANNEL";
  case InsertType::Resonator:
    return "SHARED TUNE AND CLOCK  //  DISCRETE CHANNEL STATE";
  case InsertType::Erosion:
    return "SHARED MOD CLOCK  //  DISCRETE MULTICHANNEL DELAYS";
  case InsertType::Shifter:
    return "REGEN GOVERNOR  //  PERIODIC SQUASH + SMOOTH RECOVERY";
  case InsertType::Wavefolder:
    return "4× SUBSTEP ANTIALIASING  //  NONLINEAR COLOR";
  case InsertType::Repeater:
    return "TRANSIENT CAPTURE  //  ONE READ/WRITE CLOCK FOR ALL LANES";
  case InsertType::TimeMangler:
    return "TRANSIENT CAPTURE  //  SHARED FRACTIONAL READ POSITION";
  case InsertType::Off:
    return "SELECT A DEVICE FOR THIS POST-PLAYBACK INSERT";
  }
  return "";
}

std::string portableSlotFilename(const Plugin &instance, uint32_t slot) {
  if (slot >= instance.samplePaths.size() || instance.samplePaths[slot].empty())
    return "EMPTY";
  const auto path =
      s3g::sample_storage::abbreviatedPath(instance.samplePaths[slot], 32u);
  return str(path.c_str());
}

class Editor final : public Drawing {
public:
  explicit Editor(Plugin &p) : _instance(&p) {}
  ~Editor() override { stopRefresh(); }
  void service() override { serviceSampleLoads(*_instance); }
  void stopRefresh() override {
    View::stopRefresh();
    mouseUp();
    closeDetailMenu();
  }
  void paramEvent(s3g::clap_gui::ParamEventKind kind, double value = 0) {
    using Kind = s3g::clap_gui::ParamEventKind;
    if (kind == Kind::GestureBegin) {
      if (paramEditing || _instance->guiParamEvents.available() < 3)
        return;
    } else if (!paramEditing || (kind == Kind::Value &&
                                 _instance->guiParamEvents.available() <= 1))
      return;
    if (_instance->guiParamEvents.push({kind, kOutputGainParamId, value})) {
      if (kind == Kind::GestureBegin)
        paramEditing = true;
      if (kind == Kind::GestureEnd)
        paramEditing = false;
      if (kind == s3g::clap_gui::ParamEventKind::Value)
        setParam(*_instance, kOutputGainParamId, value);
      const auto *host = _instance->host;
      const auto *params = host && host->get_extension
                               ? static_cast<const clap_host_params_t *>(
                                     host->get_extension(host, CLAP_EXT_PARAMS))
                               : nullptr;
      s3g::clap_gui::requestParamEventService(host, params);
    }
  }
  void paramValue(double v) {
    paramEvent(s3g::clap_gui::ParamEventKind::Value, v);
  }
  bool pointerDown(MouseDownEvent &e) override {
    if (e.buttonState.isRight()) {
      rightMouseDown(e);
      return true;
    }
    if (!e.buttonState.isLeft())
      return false;
    mouseDown(e);
    if (_detailMenuKind != kDetailMenuNone)
      showDetailMenu();
    return true;
  }
  void pointerMove(MouseMoveEvent &e) override {
    mouseDragged(e);
    e.consumed = true;
  }
  void pointerUp(MouseUpEvent &) override { mouseUp(); }
  void wheel(MouseWheelEvent &e) override {
    scrollWheel(e);
    e.consumed = true;
  }
  void closeDetailMenu() {
    _detailMenuKind = kDetailMenuNone;
    _detailMenuHover = -1;
  }
  void popupDismissed() override { closeDetailMenu(); }
  int64_t detailMenuHitAtPoint(CPoint) {
    return -1;
  } // canvas popup owns its events
  void updateDetailNumericFields() {
  } // fields are drawn/registered with the canvas
  void showDetailMenu() {
    const uint32_t count = detailMenuItemCount();
    const auto *slot = selectedSampleSlot();
    if (!count || !slot) {
      closeDetailMenu();
      return;
    }
    std::vector<std::string> items;
    int selected = -1;
    for (uint32_t i = 0; i < count; ++i) {
      switch (_detailMenuKind) {
      case kDetailMenuMidiChannel:
        items.push_back(i ? fmt("CHANNEL %u", i) : "OMNI");
        selected = _instance->controlBank->slots[_detailMenuSlot].midiChannel;
        break;
      case kDetailMenuSliceMethod:
        items.push_back(kSlicerMethodItems[i]);
        selected = int(_sliceModeSelection);
        break;
      case kDetailMenuStartNote:
        items.push_back(noteText(uint8_t(i)));
        selected = slot->rootNote;
        break;
      case kDetailMenuTrigger:
        items.push_back(kSlicerTriggerItems[i]);
        selected = int(slot->triggerMode);
        break;
      case kDetailMenuRetrigger:
        items.push_back(kSlicerRetriggerItems[i]);
        selected = int(slot->retriggerMode);
        break;
      case kDetailMenuVoice:
        items.push_back(kSlicerVoiceItems[i]);
        selected = int(slot->voiceMode);
        break;
      case kDetailMenuPitch:
        items.push_back(kSlicerPitchItems[i]);
        selected = int(slot->pitchMode);
        break;
      case kDetailMenuSync:
        items.push_back(kSlicerSyncItems[i]);
        selected = int(slot->syncMode);
        break;
      case kDetailMenuSliceLaunch:
        items.push_back(kSlicerLaunchItems[i]);
        selected = int(slot->slices[selectedSliceIndex(*slot)].launchMode);
        break;
      case kDetailMenuSliceChoke:
        items.push_back(i ? fmt("GROUP %u", i) : "OFF");
        selected = slot->slices[selectedSliceIndex(*slot)].chokeGroup;
        break;
      default:
        break;
      }
    }
    // The original note/channel grids are column-major.
    const auto columns = detailMenuColumns(_detailMenuKind);
    const auto rows = multiColumnMenuRows(count, columns);
    std::vector<std::string> grid(rows * columns);
    int gridSelection = -1;
    for (uint32_t i = 0; i < count; ++i) {
      const auto at = (i % rows) * columns + i / rows;
      grid[at] = items[i];
      if (int(i) == selected)
        gridSelection = int(at);
    }
    openPopup(
        detailMenuControlRect(_detailMenuKind, _detailMenuSlot),
        std::move(grid), gridSelection,
        [this, rows, columns, count](int n) {
          const auto i = uint32_t(n) % columns * rows + uint32_t(n) / columns;
          if (i < count)
            applyDetailMenuSelection(i);
          closeDetailMenu();
        },
        int(columns),
        detailMenuDropdownRect(_detailMenuKind, count, _detailMenuSlot));
  }
  size_t selectedSliceIndex(const s3g::breakbeat::SampleSlot &s) {
    return size_t(std::clamp<int64_t>(_selectedSlice, 0,
                                      std::max<int64_t>(0, s.sliceCount - 1)));
  }
  void numericFields() {
    if (_page != 1)
      return;
    const auto *s = selectedSampleSlot();
    if (!s)
      return;
    const bool mapped = s->asset && slotHasCompleteMap(*s) && s->sliceCount;
    for (uint32_t i = 0; i < kDetailNumericCount; ++i) {
      if (isSlicePropertyNumeric(i) &&
          (!mapped ||
           (i == kDetailNumericSlicePan && s->asset->channelCount > 2)))
        continue;
      const auto &slice = s->slices[selectedSliceIndex(*s)];
      const double values[] = {
          double(_instance->transientPreRollMicroseconds),
          double(_instance->minimumTransientSliceMilliseconds),
          s->sourceTempoBpm,
          s->loopCrossfade * 100.,
          s->glideSeconds * 1000.,
          slice.gain,
          slice.transposeSemitones,
          slice.pan};
      number(detailNumericFieldRect(i), values[i],
             [this, i](double v) { setNumeric(i, v); });
    }
  }
  void openSamples() {
    foundation::FileDialogOptions o;
    o.title = "Load breaks";
    auto paths = foundation::runFileDialogs(getFrame(), o);
    loadPaths(paths, selectedSlot());
  }
  void loadPaths(const std::vector<std::string> &paths, uint32_t slot) {
    for (const auto &path : paths) {
      if (slot >= 4)
        break;
      queueSampleLoad(*_instance, slot++, path);
    }
    _selectedSlice = 0;
    _zoom = 1;
    _visibleStart = 0;
  }
  bool dropped(const std::vector<std::string> &paths, CPoint point) override {
    uint32_t target = selectedSlot();
    for (uint32_t i = 0; i < 4; ++i) {
      if (pointInRect(point, _page == 2 ? mixerStripRect(i) : bankRowRect(i)) ||
          (_page == 0 && pointInRect(point, overviewWaveformRect(i))) ||
          (_page == 3 && pointInRect(point, mutateSlotRect(i)))) {
        target = i;
        break;
      }
    }
    if (paths.empty())
      return false;
    _instance->selectedSlot = target;
    loadPaths(paths, target);
    return true;
  }
  void exportCurrentBreak() {
    if (!selectedSampleSlot() || !selectedSampleSlot()->asset) {
      _instance->status = "LOAD A BREAK BEFORE EXPORTING";
      return;
    }
    foundation::FileDialogOptions o;
    o.save = true;
    o.title = "Export Rendered Break";
    o.extension = "wav";
    o.extensionDescription = "32-bit float WAV";
    o.defaultSaveName =
        foundation::pathFromUtf8(
            portableSlotFilename(*_instance, selectedSlot()).c_str())
            .stem()
            .u8string() +
        "-rendered.wav";
    auto path = foundation::runFileDialog(getFrame(), o);
    if (path.empty())
      return;
    _instance->status = queueMutationExport(*_instance, selectedSlot(), path)
                            ? "RENDERING BREAK FOR WAV EXPORT"
                            : "COULD NOT QUEUE THE WAV EXPORT";
  }
  void drawMixerInsertParameter(const std::string &name,
                                const std::string &value, double n,
                                uint32_t parameter, TextStyle a, TextStyle v,
                                const Style &s) {
    const auto panel = mixerAuxBusRect();
    const auto y = mixerInsertParameterY(parameter);
    drawAt(name, CPoint(panel.left + 16, y), a);
    drawBoundedRightText(
        value, box(panel.left + 150, y - 1, panel.getWidth() - 166, 17), v);
    const auto track = mixerInsertParameterTrackRect(parameter);
    foundation::drawHorizontalSlider(*context, track, n,
                                     track.getCenter().y - 6.5, 13, s);
  }
  void setNumeric(int64_t index, double value) {
    if (index == kDetailNumericPreRoll) {
      _instance->transientPreRollMicroseconds =
          static_cast<uint32_t>(std::llround(std::clamp(
              value, 0.0,
              static_cast<double>(kMaximumTransientPreRollMicroseconds))));
      markStateDirty(*_instance);
      _instance->status = "TRANSIENT PRE-ROLL UPDATED";
    } else if (index == kDetailNumericMinimumSlice) {
      _instance->minimumTransientSliceMilliseconds =
          static_cast<uint32_t>(std::llround(std::clamp(
              value, 0.0,
              static_cast<double>(kMaximumMinimumTransientSliceMilliseconds))));
      markStateDirty(*_instance);
      _instance->status = "MINIMUM TRANSIENT SLICE UPDATED";
    } else if (isSlicePropertyNumeric(static_cast<uint32_t>(index))) {
      auto bank = editableBank(*_instance);
      auto &slot = bank->slots[selectedSlot()];
      if (!slot.asset || !slotHasCompleteMap(slot) || slot.sliceCount == 0u)
        return;
      const std::size_t selected = static_cast<std::size_t>(std::clamp<int64_t>(
          _selectedSlice, 0, static_cast<int64_t>(slot.sliceCount) - 1));
      auto &slice = slot.slices[selected];
      if (index == kDetailNumericSliceGain) {
        slice.gain = static_cast<float>(std::clamp(value, 0.0, 4.0));
        _instance->status = "SLICE GAIN UPDATED";
      } else if (index == kDetailNumericSlicePitch) {
        slice.transposeSemitones =
            static_cast<float>(std::clamp(value, -96.0, 96.0));
        _instance->status = "SLICE PITCH UPDATED";
      } else if (slot.asset->channelCount <= 2u) {
        slice.pan = static_cast<float>(std::clamp(value, -1.0, 1.0));
        _instance->status = "SLICE PAN UPDATED";
      } else {
        _instance->status = "PAN IS BYPASSED FOR MULTICHANNEL SOURCES";
        updateDetailNumericFields();
        return;
      }
      (void)publishBank(*_instance, std::move(bank));
    } else {
      auto bank = editableBank(*_instance);
      auto &slot = bank->slots[selectedSlot()];
      if (index == kDetailNumericSourceTempo) {
        slot.sourceTempoBpm =
            static_cast<float>(std::clamp(value, 20.0, 999.0));
        _instance->status = "MAPPED SLICE SOURCE BPM UPDATED";
      } else if (index == kDetailNumericLoopCrossfade) {
        slot.loopCrossfade =
            static_cast<float>(std::clamp(value, 0.0, 50.0) * 0.01);
        _instance->status = "MAPPED SLICE LOOP CROSSFADE UPDATED";
      } else {
        slot.glideSeconds =
            static_cast<float>(std::clamp(value, 0.0, 2000.0) * 0.001);
        _instance->status = "MAPPED SLICE GLIDE UPDATED";
      }
      (void)publishBank(*_instance, std::move(bank));
    }
    updateDetailNumericFields();
    invalid();
  }
  const BankSnapshot *displayBank() {
    return _dragBank ? _dragBank.get() : _instance->controlBank.get();
  }

  uint32_t selectedSlot() {
    return std::min<uint32_t>(
        _instance->selectedSlot,
        static_cast<uint32_t>(s3g::breakbeat::kMaximumSampleSlots - 1u));
  }

  const s3g::breakbeat::SampleSlot *selectedSampleSlot() {
    const auto *bank = displayBank();
    return bank ? &bank->slots[selectedSlot()] : nullptr;
  }

  uint32_t visibleFrames(uint32_t total) {
    if (total == 0u)
      return 0u;
    _zoom = std::clamp<double>(
        _zoom, 1.0, std::max<double>(1.0, static_cast<double>(total) / 32.0));
    return std::clamp<uint32_t>(
        static_cast<uint32_t>(std::ceil(static_cast<double>(total) / _zoom)),
        1u, total);
  }

  uint32_t clampedStart(uint32_t total) {
    const uint32_t visible = visibleFrames(total);
    _visibleStart =
        std::min(_visibleStart, total > visible ? total - visible : 0u);
    return _visibleStart;
  }

  double xForFrame(uint32_t frame, uint32_t total) {
    const CRect rect = waveformRect();
    const uint32_t visible = visibleFrames(total);
    const uint32_t start = clampedStart(total);
    return rect.left +
           rect.getWidth() *
               (static_cast<double>(frame) - static_cast<double>(start)) /
               static_cast<double>(visible);
  }

  uint32_t frameForX(double x, uint32_t total) {
    const CRect rect = waveformRect();
    const uint32_t visible = visibleFrames(total);
    const uint32_t start = clampedStart(total);
    const double fraction =
        std::clamp<double>((x - rect.left) / rect.getWidth(), 0.0, 1.0);
    return std::min<uint32_t>(
        total - 1u,
        start + static_cast<uint32_t>(std::llround(fraction * (visible - 1u))));
  }

  CRect viewportHandleForTotal(uint32_t total) {
    const CRect track = inset(waveformNavigatorRect(), 1.0, 1.0);
    if (total == 0u)
      return track;
    const uint32_t visible = visibleFrames(total);
    const uint32_t start = clampedStart(total);
    const double fractionVisible =
        static_cast<double>(visible) / static_cast<double>(total);
    const double width = std::clamp<double>(track.getWidth() * fractionVisible,
                                            30.0, track.getWidth());
    const double travel = track.getWidth() - width;
    const double position =
        total > visible
            ? static_cast<double>(start) / static_cast<double>(total - visible)
            : 0.0;
    return box(track.left + travel * position, track.top, width,
               track.getHeight());
  }

  void setViewportHandleOrigin(double origin, uint32_t total) {
    if (total == 0u)
      return;
    const CRect track = inset(waveformNavigatorRect(), 1.0, 1.0);
    const uint32_t visible = visibleFrames(total);
    const CRect handle = viewportHandleForTotal(total);
    const double travel = track.getWidth() - handle.getWidth();
    if (travel <= 0.0 || total <= visible) {
      _visibleStart = 0u;
      return;
    }
    const double position =
        std::clamp<double>((origin - track.left) / travel, 0.0, 1.0);
    _visibleStart = static_cast<uint32_t>(
        std::llround(position * static_cast<double>(total - visible)));
  }

  int64_t markerNearPoint(CPoint point) {
    const auto *slot = selectedSampleSlot();
    if (!slot || !slot->asset)
      return -1;
    for (std::size_t index = 1u; index < slot->sliceCount; ++index) {
      const double x =
          xForFrame(slot->slices[index].startFrame, slot->asset->frameCount());
      if (std::abs(point.x - x) <= 7.0)
        return static_cast<int64_t>(index);
    }
    return -1;
  }

  int64_t loopHandleNearPoint(CPoint point) {
    const auto *slot = selectedSampleSlot();
    if (!slot || !slot->asset || !slotHasCompleteMap(*slot) ||
        _selectedSlice < 0 ||
        static_cast<std::size_t>(_selectedSlice) >= slot->sliceCount)
      return -1;
    const auto &slice = slot->slices[static_cast<std::size_t>(_selectedSlice)];
    if (slice.launchMode != s3g::breakbeat::LaunchMode::Loop &&
        slice.launchMode != s3g::breakbeat::LaunchMode::PingPong)
      return -1;
    const uint32_t loopStart =
        slice.loopStartFrame == 0u && slice.loopEndFrame == 0u
            ? slice.startFrame
            : slice.loopStartFrame;
    const uint32_t loopEnd =
        slice.loopStartFrame == 0u && slice.loopEndFrame == 0u
            ? slice.endFrame
            : slice.loopEndFrame;
    const double startX = xForFrame(loopStart, slot->asset->frameCount());
    const double endX = xForFrame(loopEnd, slot->asset->frameCount());
    const double startDistance = std::abs(point.x - startX);
    const double endDistance = std::abs(point.x - endX);
    if (std::min(startDistance, endDistance) > 8.0)
      return -1;
    return startDistance <= endDistance ? 0 : 1;
  }

  int64_t sliceAtFrame(uint32_t frame) {
    const auto *slot = selectedSampleSlot();
    if (!slot)
      return -1;
    for (std::size_t index = 0u; index < slot->sliceCount; ++index) {
      if (frame >= slot->slices[index].startFrame &&
          frame < slot->slices[index].endFrame)
        return static_cast<int64_t>(index);
    }
    return -1;
  }

  uint32_t detailMenuItemCount() {
    switch (_detailMenuKind) {
    case kDetailMenuMidiChannel:
      return 17u;
    case kDetailMenuSliceMethod:
      return 5u;
    case kDetailMenuStartNote: {
      const auto *slot = selectedSampleSlot();
      if (!slot)
        return 0u;
      const uint32_t maximum =
          slot->sliceCount == 0u ? 127u : 128u - slot->sliceCount;
      return maximum + 1u;
    }
    case kDetailMenuTrigger:
      return 4u;
    case kDetailMenuRetrigger:
    case kDetailMenuVoice:
    case kDetailMenuPitch:
      return 3u;
    case kDetailMenuSync:
      return 2u;
    case kDetailMenuSliceLaunch:
      return 5u;
    case kDetailMenuSliceChoke:
      return 17u;
    default:
      return 0u;
    }
  }

  void applyDetailMenuSelection(int64_t selection) {
    if (selection < 0)
      return;
    if (_detailMenuKind == kDetailMenuMidiChannel) {
      if (selection <= 16 &&
          setSlotMidiChannel(*_instance, _detailMenuSlot,
                             static_cast<uint8_t>(selection))) {
        _instance->status = selection == 0 ? "BREAK MIDI SET TO OMNI"
                                           : "BREAK MIDI CHANNEL UPDATED";
      }
    } else if (_detailMenuKind == kDetailMenuSliceMethod) {
      if (selection > 4)
        return;
      _sliceModeSelection = selection;
      _instance->status = "SLICE METHOD SELECTED - PRESS SLICE";
    } else if (_detailMenuKind == kDetailMenuStartNote) {
      if (selection <= 127 &&
          setSlotRootNote(*_instance, selectedSlot(),
                          static_cast<uint8_t>(selection))) {
        _instance->status = "START NOTE UPDATED - PRESS AUTO MAP";
      }
    } else if (_detailMenuKind >= kDetailMenuTrigger &&
               _detailMenuKind <= kDetailMenuSync) {
      auto bank = editableBank(*_instance);
      auto &slot = bank->slots[selectedSlot()];
      const char *status = "SLICE PLAYBACK UPDATED";
      if (_detailMenuKind == kDetailMenuTrigger && selection <= 3) {
        slot.triggerMode = static_cast<s3g::breakbeat::TriggerMode>(selection);
        status = "MAPPED SLICE TRIGGER UPDATED";
      } else if (_detailMenuKind == kDetailMenuRetrigger && selection <= 2) {
        slot.retriggerMode =
            static_cast<s3g::breakbeat::RetriggerMode>(selection);
        status = "MAPPED SLICE RETRIGGER UPDATED";
      } else if (_detailMenuKind == kDetailMenuVoice && selection <= 2) {
        slot.voiceMode = static_cast<s3g::breakbeat::VoiceMode>(selection);
        status = "MAPPED SLICE VOICE MODE UPDATED";
      } else if (_detailMenuKind == kDetailMenuPitch && selection <= 2) {
        slot.pitchMode = static_cast<s3g::breakbeat::PitchMode>(selection);
        status = "MAPPED SLICE PITCH MODE UPDATED";
      } else if (_detailMenuKind == kDetailMenuSync && selection <= 1) {
        slot.syncMode = static_cast<s3g::breakbeat::SyncMode>(selection);
        status = "MAPPED SLICE TEMPO SYNC UPDATED";
      } else {
        return;
      }
      if (publishBank(*_instance, std::move(bank)))
        _instance->status = status;
    } else if (_detailMenuKind == kDetailMenuSliceLaunch ||
               _detailMenuKind == kDetailMenuSliceChoke) {
      auto bank = editableBank(*_instance);
      auto &slot = bank->slots[selectedSlot()];
      if (!slotHasCompleteMap(slot) || slot.sliceCount == 0u)
        return;
      const std::size_t selected = static_cast<std::size_t>(std::clamp<int64_t>(
          _selectedSlice, 0, static_cast<int64_t>(slot.sliceCount) - 1));
      if (_detailMenuKind == kDetailMenuSliceLaunch && selection <= 4) {
        slot.slices[selected].launchMode =
            static_cast<s3g::breakbeat::LaunchMode>(selection);
        if (publishBank(*_instance, std::move(bank)))
          _instance->status = "SLICE LAUNCH MODE UPDATED";
      } else if (_detailMenuKind == kDetailMenuSliceChoke && selection <= 16) {
        slot.slices[selected].chokeGroup = static_cast<uint8_t>(selection);
        if (publishBank(*_instance, std::move(bank)))
          _instance->status = "SLICE CHOKE GROUP UPDATED";
      }
    }
    invalid();
  }

  void updateDetailNumericControl(uint32_t index, CPoint point) {
    if (index >= kDetailNumericCount)
      return;
    const CRect track = detailNumericTrackRect(index);
    const double normalized = std::clamp(
        static_cast<double>((point.x - minX(track)) / rectWidth(track)), 0.0,
        1.0);
    if (index == kDetailNumericPreRoll) {
      _instance->transientPreRollMicroseconds = static_cast<uint32_t>(
          std::llround(normalized * kMaximumTransientPreRollMicroseconds));
      _instance->status = "TRANSIENT PRE-ROLL UPDATED";
    } else if (index == kDetailNumericMinimumSlice) {
      _instance->minimumTransientSliceMilliseconds = static_cast<uint32_t>(
          std::llround(normalized * kMaximumMinimumTransientSliceMilliseconds));
      _instance->status = "MINIMUM TRANSIENT SLICE UPDATED";
    } else if (isSlicePropertyNumeric(index)) {
      auto bank = editableBank(*_instance);
      auto &slot = bank->slots[selectedSlot()];
      if (!slot.asset || !slotHasCompleteMap(slot) || slot.sliceCount == 0u)
        return;
      const std::size_t selected = static_cast<std::size_t>(std::clamp<int64_t>(
          _selectedSlice, 0, static_cast<int64_t>(slot.sliceCount) - 1));
      auto &slice = slot.slices[selected];
      if (index == kDetailNumericSliceGain) {
        slice.gain = static_cast<float>(normalized * 4.0);
        _instance->status = "SLICE GAIN UPDATED";
      } else if (index == kDetailNumericSlicePitch) {
        slice.transposeSemitones =
            static_cast<float>(-24.0 + normalized * 48.0);
        _instance->status = "SLICE PITCH UPDATED";
      } else if (slot.asset->channelCount <= 2u) {
        slice.pan = static_cast<float>(-1.0 + normalized * 2.0);
        _instance->status = "SLICE PAN UPDATED";
      } else {
        _instance->status = "PAN IS BYPASSED FOR MULTICHANNEL SOURCES";
        return;
      }
      (void)publishBank(*_instance, std::move(bank), false);
    } else {
      auto bank = editableBank(*_instance);
      auto &slot = bank->slots[selectedSlot()];
      if (index == kDetailNumericSourceTempo) {
        slot.sourceTempoBpm = static_cast<float>(20.0 + normalized * 979.0);
        _instance->status = "MAPPED SLICE SOURCE BPM UPDATED";
      } else if (index == kDetailNumericLoopCrossfade) {
        slot.loopCrossfade = static_cast<float>(normalized * 0.5);
        _instance->status = "MAPPED SLICE LOOP CROSSFADE UPDATED";
      } else {
        slot.glideSeconds = static_cast<float>(normalized * 2.0);
        _instance->status = "MAPPED SLICE GLIDE UPDATED";
      }
      (void)publishBank(*_instance, std::move(bank), false);
    }
    updateDetailNumericFields();
    invalid();
  }

  void clearSelectedSlot() {
    const uint32_t index = selectedSlot();
    cancelSampleLoad(*_instance, index);
    auto bank = editableBank(*_instance);
    s3g::breakbeat::clearMappingsForSlot(*bank, static_cast<uint8_t>(index));
    const uint8_t root = bank->slots[index].rootNote;
    const uint8_t channel = bank->slots[index].midiChannel;
    const float mixerGain = bank->slots[index].mixerGain;
    const float mixerPan = bank->slots[index].mixerPan;
    const float mixerLowEqDb = bank->slots[index].mixerLowEqDb;
    const float mixerMidEqDb = bank->slots[index].mixerMidEqDb;
    const float mixerHighEqDb = bank->slots[index].mixerHighEqDb;
    const float mixerMidFrequencyHz = bank->slots[index].mixerMidFrequencyHz;
    const float mixerAuxSend = bank->slots[index].mixerAuxSend;
    const auto inserts = bank->slots[index].inserts;
    const s3g::breakbeat::Envelope envelope = bank->slots[index].envelope;
    const bool muted = bank->slots[index].muted;
    const bool solo = bank->slots[index].solo;
    bank->slots[index] = {};
    bank->slots[index].rootNote = root;
    bank->slots[index].mappedRootNote = root;
    bank->slots[index].midiChannel = channel;
    bank->slots[index].envelope = envelope;
    bank->slots[index].mixerGain = mixerGain;
    bank->slots[index].mixerPan = mixerPan;
    bank->slots[index].mixerLowEqDb = mixerLowEqDb;
    bank->slots[index].mixerMidEqDb = mixerMidEqDb;
    bank->slots[index].mixerHighEqDb = mixerHighEqDb;
    bank->slots[index].mixerMidFrequencyHz = mixerMidFrequencyHz;
    bank->slots[index].mixerAuxSend = mixerAuxSend;
    bank->slots[index].inserts = inserts;
    bank->slots[index].muted = muted;
    bank->slots[index].solo = solo;
    if (publishBank(*_instance, std::move(bank), false)) {
      _instance->samplePaths[index].clear();
      _instance->sampleFileBytes[index] = 0u;
      _instance->projectFiles[index].clear();
      _instance->projectStoragePending[index] = false;
      _instance->analyses[index].reset();
      resetSlotVariations(*_instance, index);
      markStateDirty(*_instance);
      _instance->status = "SLOT CLEARED";
    }
    _selectedSlice = 0;
  }

  void makeEqual(size_t count) {
    const auto *slot = selectedSampleSlot();
    if (!slot || !slot->asset)
      return;
    const std::size_t requested = count;
    count = std::min(count,
                     s3g::breakbeat::maximumSlicesForStartNote(slot->rootNote));
    if (replaceSlotSlices(
            *_instance, selectedSlot(),
            s3g::breakbeat::makeEqualSlices(*slot->asset, count))) {
      _selectedSlice = 0;
      _instance->status = count < requested
                              ? "SLICES CAPPED BY START NOTE - PRESS AUTO MAP"
                              : "SLICES CHANGED - PRESS AUTO MAP";
    }
  }

  void applySelectedSliceMethod() {
    const uint32_t selected = selectedSlot();
    const auto *slot = selectedSampleSlot();
    if (!slot || !slot->asset) {
      _instance->status = "LOAD A BREAK BEFORE SLICING";
      return;
    }
    const int64_t method = std::clamp<int64_t>(_sliceModeSelection, 0, 4);
    if (method < 4) {
      makeEqual(static_cast<std::size_t>(4u << method));
    } else if (_instance->analyses[selected]) {
      makeTransient();
    } else {
      _instance->status = "TRANSIENT ANALYSIS IS NOT AVAILABLE";
    }
  }

  void makeTransient() {
    const uint32_t selected = selectedSlot();
    const auto *slot = selectedSampleSlot();
    const auto analysis = _instance->analyses[selected];
    if (!slot || !slot->asset || !analysis)
      return;
    const uint32_t zeroRadius =
        static_cast<uint32_t>(std::lround(slot->asset->sampleRate * 0.004));
    if (replaceSlotSlices(
            *_instance, selected,
            s3g::breakbeat::makeTransientSlices(
                *slot->asset, *analysis,
                s3g::breakbeat::maximumSlicesForStartNote(slot->rootNote),
                zeroRadius, _instance->transientPreRollMicroseconds,
                static_cast<uint32_t>(std::lround(
                    slot->asset->sampleRate *
                    _instance->minimumTransientSliceMilliseconds * 0.001))))) {
      _selectedSlice = 0;
      _instance->status = "SLICES CHANGED - PRESS AUTO MAP";
    }
  }

  void audition() {
    const auto *bank = displayBank();
    if (!bank)
      return;
    const int note = mappedNoteFor(
        *bank, selectedSlot(),
        static_cast<uint32_t>(std::max<int64_t>(0, _selectedSlice)));
    if (note < 0) {
      _instance->status = "SELECTED SLICE IS NOT MAPPED";
      return;
    }
    _instance->pendingAuditionNote.store(static_cast<uint32_t>(note + 1),
                                         std::memory_order_release);
    _instance->pendingAuditionChannel.store(
        bank->slots[selectedSlot()].midiChannel, std::memory_order_release);
    requestProcess(*_instance);
  }

  void updateEnvelopeDragAt(CPoint point) {
    if (_envelopeDragIndex < 0 || _envelopeDragIndex > 3 || !_dragBank)
      return;
    auto &slot = _dragBank->slots[selectedSlot()];
    if (!slot.asset || slot.sliceCount == 0u)
      return;
    auto &envelope = slot.envelope;
    const CRect track =
        envelopeSliderTrackRect(static_cast<uint32_t>(_envelopeDragIndex));
    float normalized = static_cast<float>(
        std::clamp((point.x - minX(track)) / rectWidth(track), 0.0, 1.0));
    if (_envelopeDragIndex == 2) {
      envelope.sustain = normalized;
    } else {
      const float otherTiming =
          _envelopeDragIndex == 0
              ? envelope.decayProportion + envelope.releaseProportion
              : _envelopeDragIndex == 1
                    ? envelope.attackProportion + envelope.releaseProportion
                    : envelope.attackProportion + envelope.decayProportion;
      normalized = std::min(normalized, std::max(0.0f, 1.0f - otherTiming));
      if (_envelopeDragIndex == 0)
        envelope.attackProportion = normalized;
      else if (_envelopeDragIndex == 1)
        envelope.decayProportion = normalized;
      else
        envelope.releaseProportion = normalized;
    }
    invalid();
  }

  void updateMixerDragAt(CPoint point) {
    if (_mixerDragKind == 3) {
      const CRect track = mixerOutputTrackRect();
      const double normalized =
          std::clamp((point.x - minX(track)) / rectWidth(track), 0.0, 1.0);
      paramValue(-60.0 + normalized * 72.0);
      requestProcess(*_instance);
      invalid();
      return;
    }
    if (_mixerDragKind >= 20 && _mixerDragKind <= 23) {
      if (!_dragBank || _mixerDragSlot >= _dragBank->slots.size() ||
          _insertEditorIndex >= s3g::breakbeat::kInsertSlotsPerStrip)
        return;
      const uint32_t parameter = static_cast<uint32_t>(_mixerDragKind - 20);
      const CRect track = mixerInsertParameterTrackRect(parameter);
      auto &insert =
          _dragBank->slots[_mixerDragSlot].inserts[_insertEditorIndex];
      insert.values[parameter] = static_cast<float>(
          std::clamp((point.x - minX(track)) / rectWidth(track), 0.0, 1.0));
      (void)publishMixerBank(*_instance, _dragBank, false);
      invalid();
      return;
    }
    if (_mixerDragKind >= 9 && _mixerDragKind <= 16) {
      if (!_dragBank)
        return;
      const uint32_t row = static_cast<uint32_t>(_mixerDragKind - 9);
      if (_dragBank->auxFieldSafe && row >= 3u && row <= 5u)
        return;
      const CRect track = mixerBusSliderTrackRect(row);
      const float normalized = static_cast<float>(
          std::clamp((point.x - minX(track)) / rectWidth(track), 0.0, 1.0));
      switch (row) {
      case 0u:
        _dragBank->auxPress = normalized;
        break;
      case 1u:
        _dragBank->auxSnap = normalized * 2.0f - 1.0f;
        break;
      case 2u:
        _dragBank->auxRecovery = normalized;
        break;
      case 3u:
        _dragBank->auxSaturation = normalized;
        break;
      case 4u:
        _dragBank->auxBite = normalized;
        break;
      case 5u:
        _dragBank->auxClip = normalized;
        break;
      case 6u:
        _dragBank->auxTilt = normalized * 2.0f - 1.0f;
        break;
      case 7u:
        _dragBank->auxReturnDb = -60.0f + normalized * 72.0f;
        break;
      default:
        break;
      }
      (void)publishMixerBank(*_instance, _dragBank, false);
      invalid();
      return;
    }
    if (!_dragBank || _mixerDragSlot >= _dragBank->slots.size())
      return;
    auto &slot = _dragBank->slots[_mixerDragSlot];
    if (_mixerDragKind == 1) {
      const CRect fader = mixerFaderRect(_mixerDragSlot);
      const double normalized = std::clamp<double>(
          (maxY(fader) - point.y) / rectHeight(fader), 0.0, 1.0);
      const double db = -60.0 + static_cast<double>(normalized) * 66.0;
      slot.mixerGain = normalized <= 0.0001
                           ? 0.0f
                           : static_cast<float>(std::pow(10.0, db / 20.0));
    } else if (_mixerDragKind == 8) {
      const CRect track = mixerAuxTrackRect(_mixerDragSlot);
      slot.mixerAuxSend = static_cast<float>(
          std::clamp((point.x - minX(track)) / rectWidth(track), 0.0, 1.0));
    } else if (_mixerDragKind == 2 ||
               (_mixerDragKind >= 4 && _mixerDragKind <= 7)) {
      const double delta = (_mixerDragStartPoint.y - point.y) +
                           (point.x - _mixerDragStartPoint.x);
      double normalized = 0.0;
      if (_mixerDragKind == 2)
        normalized = (_mixerDragStartValue + 1.0) * 0.5;
      else if (_mixerDragKind == 6)
        normalized =
            std::log(_mixerDragStartValue / 120.0) / std::log(8000.0 / 120.0);
      else
        normalized = (_mixerDragStartValue + 12.0) / 24.0;
      normalized = std::clamp(normalized + delta / 160.0, 0.0, 1.0);
      if (_mixerDragKind == 2) {
        slot.mixerPan = static_cast<float>(normalized * 2.0 - 1.0);
        if (std::abs(slot.mixerPan) < 0.025f)
          slot.mixerPan = 0.0f;
      } else if (_mixerDragKind == 4) {
        slot.mixerLowEqDb = static_cast<float>(normalized * 24.0 - 12.0);
      } else if (_mixerDragKind == 5) {
        slot.mixerMidEqDb = static_cast<float>(normalized * 24.0 - 12.0);
      } else if (_mixerDragKind == 6) {
        slot.mixerMidFrequencyHz =
            static_cast<float>(120.0 * std::pow(8000.0 / 120.0, normalized));
      } else {
        slot.mixerHighEqDb = static_cast<float>(normalized * 24.0 - 12.0);
      }
    }
    // Publish a post-playback mixer snapshot for every drag update. This is
    // independent of voice/sample state, so an active slice hears the change
    // on the next process block instead of waiting for a retrigger.
    (void)publishMixerBank(*_instance, _dragBank, false);
    invalid();
  }

  void handleMixerMouseDown(CPoint point) {
    for (uint32_t index = 0u; index < s3g::breakbeat::kMaximumSampleSlots;
         ++index) {
      const CRect strip = mixerStripRect(index);
      if (!pointInRect(point, strip))
        continue;
      _instance->selectedSlot = index;
      const auto *displayed = displayBank();
      if (!displayed)
        return;
      const auto &slot = displayed->slots[index];
      for (uint32_t insert = 0u; insert < s3g::breakbeat::kInsertSlotsPerStrip;
           ++insert) {
        if (!pointInRect(point, mixerInsertRect(index, insert)))
          continue;
        _insertEditorSlot = static_cast<int64_t>(index);
        _insertEditorIndex = insert;
        _instance->status = "BREAK INSERT EDITOR OPEN";
        invalid();
        return;
      }
      _insertEditorSlot = -1;
      if (pointInRect(point, mixerFaderRect(index))) {
        _mixerDragKind = 1;
        _mixerDragSlot = index;
        _dragBank = editableBank(*_instance);
        updateMixerDragAt(point);
      } else if (pointInRect(point, mixerMuteRect(index))) {
        auto bank = editableBank(*_instance);
        bank->slots[index].muted = !bank->slots[index].muted;
        if (publishMixerBank(*_instance, std::move(bank)))
          _instance->status = "BREAK MUTE UPDATED";
      } else if (pointInRect(point, mixerSoloRect(index))) {
        auto bank = editableBank(*_instance);
        bank->slots[index].solo = !bank->slots[index].solo;
        if (publishMixerBank(*_instance, std::move(bank)))
          _instance->status = "BREAK SOLO UPDATED";
      } else if (pointInRect(point, mixerAuditionRect(index))) {
        _selectedSlice = 0;
        audition();
      } else if (pointInRect(point, mixerAuxHitRect(index))) {
        _mixerDragKind = 8;
        _mixerDragSlot = index;
        _dragBank = editableBank(*_instance);
        updateMixerDragAt(point);
      } else {
        constexpr std::array<int64_t, 5u> kinds{{2, 4, 5, 6, 7}};
        for (uint32_t dial = 0u; dial < kinds.size(); ++dial) {
          if (!pointInRect(point, mixerDialRect(index, dial)))
            continue;
          if (dial == 0u && slot.asset && slot.asset->channelCount > 2u) {
            _instance->status = "PAN IS LOCKED FOR MULTICHANNEL BREAKS";
            break;
          }
          _mixerDragKind = kinds[dial];
          _mixerDragSlot = index;
          _mixerDragStartPoint = point;
          _mixerDragStartValue =
              dial == 0u
                  ? slot.mixerPan
                  : dial == 1u
                        ? slot.mixerLowEqDb
                        : dial == 2u ? slot.mixerMidEqDb
                                     : dial == 3u ? slot.mixerMidFrequencyHz
                                                  : slot.mixerHighEqDb;
          _dragBank = editableBank(*_instance);
          break;
        }
      }
      invalid();
      return;
    }
    if (pointInRect(point, mixerOutputHitRect())) {
      _mixerDragKind = 3;
      paramEvent(s3g::clap_gui::ParamEventKind::GestureBegin);
      updateMixerDragAt(point);
      return;
    }
    if (pointInRect(point, mixerUnityRect())) {
      paramEvent(s3g::clap_gui::ParamEventKind::GestureBegin);
      paramValue(0);
      paramEvent(s3g::clap_gui::ParamEventKind::GestureEnd);
      markStateDirty(*_instance);
      requestProcess(*_instance);
      _instance->status = "MAIN OUTPUT RETURNED TO UNITY";
      invalid();
      return;
    }
    if (_insertEditorSlot >= 0 && static_cast<std::size_t>(_insertEditorSlot) <
                                      s3g::breakbeat::kMaximumSampleSlots) {
      const uint32_t slotIndex = static_cast<uint32_t>(_insertEditorSlot);
      for (uint32_t type = 0u;
           type <= static_cast<uint32_t>(InsertType::TimeMangler); ++type) {
        if (!pointInRect(point, mixerInsertTypeRect(type)))
          continue;
        auto bank = editableBank(*_instance);
        bank->slots[slotIndex].inserts[_insertEditorIndex] =
            s3g::breakbeat::defaultInsertSettings(
                static_cast<InsertType>(type));
        if (publishMixerBank(*_instance, std::move(bank)))
          _instance->status =
              type == 0u ? "BREAK INSERT CLEARED" : "BREAK INSERT ASSIGNED";
        invalid();
        return;
      }
      if (pointInRect(point, mixerInsertBypassRect())) {
        auto bank = editableBank(*_instance);
        auto &insert = bank->slots[slotIndex].inserts[_insertEditorIndex];
        if (insert.type != InsertType::Off) {
          insert.bypassed = !insert.bypassed;
          const bool bypassed = insert.bypassed;
          if (publishMixerBank(*_instance, std::move(bank)))
            _instance->status =
                bypassed ? "BREAK INSERT BYPASSED" : "BREAK INSERT ENABLED";
        }
        invalid();
        return;
      }
      if (pointInRect(point, mixerInsertSwapRect())) {
        auto bank = editableBank(*_instance);
        auto &inserts = bank->slots[slotIndex].inserts;
        std::swap(inserts[0u], inserts[1u]);
        _insertEditorIndex = 1u - _insertEditorIndex;
        if (publishMixerBank(*_instance, std::move(bank)))
          _instance->status = "BREAK INSERT ORDER SWAPPED";
        invalid();
        return;
      }
      if (pointInRect(point, mixerInsertCloseRect())) {
        _insertEditorSlot = -1;
        _instance->status = "BREAK BUS VIEW RESTORED";
        invalid();
        return;
      }
      if (pointInRect(point, mixerInsertOptionRect())) {
        auto bank = editableBank(*_instance);
        auto &insert = bank->slots[slotIndex].inserts[_insertEditorIndex];
        if (insert.type == InsertType::Filter) {
          const uint8_t next = (static_cast<uint8_t>(insert.mode) + 1u) % 4u;
          insert.mode = static_cast<FilterMode>(next);
          _instance->status = "FILTER MODE UPDATED";
        } else if (insert.type == InsertType::Erosion ||
                   insert.type == InsertType::Shifter ||
                   insert.type == InsertType::Wavefolder) {
          insert.variant = insert.variant == 0u ? 1u : 0u;
          _instance->status = "BREAK INSERT MODE UPDATED";
        } else if (insert.type == InsertType::Repeater ||
                   insert.type == InsertType::TimeMangler) {
          insert.variant = static_cast<uint8_t>((insert.variant + 1u) % 3u);
          _instance->status = "BUFFER INSERT MODE UPDATED";
        } else if (insert.type != InsertType::Off) {
          insert = s3g::breakbeat::defaultInsertSettings(insert.type);
          _instance->status = "BREAK INSERT RESET";
        }
        (void)publishMixerBank(*_instance, std::move(bank));
        invalid();
        return;
      }
      for (uint32_t parameter = 0u;
           parameter < s3g::breakbeat::kInsertParameterCount; ++parameter) {
        if (!pointInRect(point, mixerInsertParameterHitRect(parameter)))
          continue;
        const auto *displayed = displayBank();
        if (!displayed ||
            displayed->slots[slotIndex].inserts[_insertEditorIndex].type ==
                InsertType::Off)
          return;
        _mixerDragKind = static_cast<int64_t>(20u + parameter);
        _mixerDragSlot = slotIndex;
        _dragBank = editableBank(*_instance);
        updateMixerDragAt(point);
        return;
      }
      // The editor owns the former Break Bus rectangle while open.
      if (pointInRect(point, mixerAuxBusRect()))
        return;
    }
    if (pointInRect(point, mixerBusEnableRect())) {
      auto bank = editableBank(*_instance);
      bank->auxEnabled = !bank->auxEnabled;
      if (publishMixerBank(*_instance, std::move(bank)))
        _instance->status = "AUX BUS UPDATED";
      invalid();
      return;
    }
    if (pointInRect(point, mixerBusLinkRect())) {
      auto bank = editableBank(*_instance);
      const uint8_t next = (static_cast<uint8_t>(bank->auxLinkMode) + 1u) % 3u;
      bank->auxLinkMode = static_cast<s3g::BreakBusLinkMode>(next);
      if (publishMixerBank(*_instance, std::move(bank)))
        _instance->status = "BREAK BUS LINK MODE UPDATED";
      invalid();
      return;
    }
    if (pointInRect(point, mixerBusFieldSafeRect())) {
      auto bank = editableBank(*_instance);
      bank->auxFieldSafe = !bank->auxFieldSafe;
      const bool fieldSafe = bank->auxFieldSafe;
      if (publishMixerBank(*_instance, std::move(bank)))
        _instance->status = fieldSafe ? "FIELD SAFE DISABLES NONLINEAR STAGES"
                                      : "BREAK BUS NONLINEAR STAGES ENABLED";
      invalid();
      return;
    }
    for (uint32_t row = 0u; row < 8u; ++row) {
      if (!pointInRect(point, mixerBusSliderHitRect(row)))
        continue;
      const auto *bank = displayBank();
      if (bank && bank->auxFieldSafe && row >= 3u && row <= 5u) {
        _instance->status = "SAT / BITE / CLIP BYPASSED IN FIELD SAFE";
        invalid();
        return;
      }
      _mixerDragKind = static_cast<int64_t>(9u + row);
      _dragBank = editableBank(*_instance);
      updateMixerDragAt(point);
      return;
    }
  }

  void editSelectedSliceControl(uint32_t index) {
    auto bank = editableBank(*_instance);
    auto &slot = bank->slots[selectedSlot()];
    if (!slot.asset || slot.sliceCount == 0u)
      return;
    if (!slotHasCompleteMap(slot)) {
      _instance->status = "AUTO MAP BEFORE EDITING SLICE PROPERTIES";
      return;
    }
    const std::size_t sliceIndex = static_cast<std::size_t>(std::clamp<int64_t>(
        _selectedSlice, 0, static_cast<int64_t>(slot.sliceCount) - 1));
    auto &slice = slot.slices[sliceIndex];
    switch (index) {
    case 0u:
      slice.gain = std::max(0.0f, slice.gain - 0.05f);
      break;
    case 1u:
      slice.gain = std::min(4.0f, slice.gain + 0.05f);
      break;
    case 2u:
      slice.transposeSemitones =
          std::max(-96.0f, slice.transposeSemitones - 1.0f);
      break;
    case 3u:
      slice.transposeSemitones =
          std::min(96.0f, slice.transposeSemitones + 1.0f);
      break;
    case 4u:
      if (slot.asset->channelCount <= 2u)
        slice.pan = std::max(-1.0f, slice.pan - 0.1f);
      break;
    case 5u:
      if (slot.asset->channelCount <= 2u)
        slice.pan = std::min(1.0f, slice.pan + 0.1f);
      break;
    case 6u:
      slice.reverse = !slice.reverse;
      break;
    case 7u: {
      constexpr uint32_t modeCount = 5u;
      const uint32_t next =
          (static_cast<uint32_t>(slice.launchMode) + 1u) % modeCount;
      slice.launchMode = static_cast<s3g::breakbeat::LaunchMode>(next);
      break;
    }
    case 8u:
      slice.chokeGroup = static_cast<uint8_t>(
          std::max(0, static_cast<int>(slice.chokeGroup) - 1));
      break;
    case 9u:
      slice.chokeGroup = static_cast<uint8_t>(
          std::min(16, static_cast<int>(slice.chokeGroup) + 1));
      break;
    default:
      return;
    }
    const bool multichannelPanBypass =
        slot.asset->channelCount > 2u && (index == 4u || index == 5u);
    if (publishBank(*_instance, std::move(bank)))
      _instance->status = multichannelPanBypass
                              ? "PAN IS BYPASSED FOR MULTICHANNEL SOURCES"
                              : "SLICE PROPERTY UPDATED";
  }

  void mouseDown(MouseDownEvent &event) {
    const CPoint point = event.mousePosition;
    if (_detailMenuKind != kDetailMenuNone) {
      const int64_t selection = detailMenuHitAtPoint(point);
      if (selection >= 0) {
        applyDetailMenuSelection(selection);
        closeDetailMenu();
        updateDetailNumericFields();
        invalid();
        return;
      }
      if (!pointInRect(
              point, detailMenuControlRect(_detailMenuKind, _detailMenuSlot))) {
        closeDetailMenu();
        updateDetailNumericFields();
      }
    }
    for (uint32_t index = 0u; index < 4u; ++index) {
      if (pointInRect(point, editorTabRect(index))) {
        closeDetailMenu();
        _page = index;
        updateDetailNumericFields();
        invalid();
        return;
      }
    }
    if (pointInRect(point, killAllButtonRect())) {
      _instance->pendingKillAll.store(true, std::memory_order_release);
      _instance->status = "ALL SAMPLE PLAYBACK STOPPED";
      invalid();
      return;
    }
    if (pointInRect(point, embedAudioButtonRect())) {
      switch (_instance->storageMode) {
      case s3g::sample_storage::StorageMode::Project:
        _instance->storageMode = s3g::sample_storage::StorageMode::Link;
        _instance->status = "LINK MODE - REFERENCED FILES ARE REQUIRED";
        break;
      case s3g::sample_storage::StorageMode::Link:
        _instance->storageMode = s3g::sample_storage::StorageMode::Embed;
        _instance->status = "EMBED MODE - DECODED PCM ENTERS HOST STATE";
        break;
      case s3g::sample_storage::StorageMode::Embed:
        _instance->storageMode = s3g::sample_storage::StorageMode::Project;
        _instance->status = "PROJECT MODE - PREPARING PROJECT MEDIA";
        break;
      }
      serviceProjectStorage(*_instance, true);
      markStateDirty(*_instance);
      invalid();
      return;
    }
    if (_page == 2u) {
      handleMixerMouseDown(point);
      return;
    }
    for (uint32_t index = 0u; index < s3g::breakbeat::kMaximumSampleSlots;
         ++index) {
      if (pointInRect(point, slotChannelRect(index))) {
        _instance->selectedSlot = index;
        _detailMenuKind = kDetailMenuMidiChannel;
        _detailMenuHover = -1;
        _detailMenuSlot = index;
        updateDetailNumericFields();
        invalid();
        return;
      }
      if (pointInRect(point, slotAutomapRect(index))) {
        if (automapSlot(*_instance, index))
          _instance->status = "BREAK SLICES AUTO-MAPPED";
        else
          _instance->status = "LOAD OR REDUCE SLICES BEFORE AUTO MAP";
        invalid();
        return;
      }
      if (pointInRect(point, bankRowRect(index))) {
        _instance->selectedSlot = index;
        _selectedSlice = 0;
        _zoom = 1.0;
        _visibleStart = 0u;
        if (event.clickCount >= 2)
          _page = 1u;
        invalid();
        return;
      }
    }
    if (_page == 0u) {
      for (uint32_t index = 0u; index < s3g::breakbeat::kMaximumSampleSlots;
           ++index) {
        if (!pointInRect(point, overviewWaveformRect(index)))
          continue;
        _instance->selectedSlot = index;
        _selectedSlice = 0;
        _zoom = 1.0;
        _visibleStart = 0u;
        if (event.clickCount >= 2)
          _page = 1u;
        invalid();
        return;
      }
      return;
    }
    if (_page == 3u) {
      const uint32_t slotIndex = selectedSlot();
      const auto *bank = displayBank();
      if (!bank)
        return;
      const auto &slot = bank->slots[slotIndex];
      for (uint32_t index = 0u; index < s3g::breakbeat::kMaximumSampleSlots;
           ++index) {
        if (!pointInRect(point, mutateSlotRect(index)))
          continue;
        _instance->selectedSlot = index;
        _selectedSlice = 0;
        _zoom = 1.0;
        _visibleStart = 0u;
        _instance->status = bank->slots[index].asset
                                ? "BREAK SELECTED AS MUTATION SOURCE"
                                : _instance->pendingMutationSlots[index]
                                      ? "MUTATION SLOT IS BUILDING"
                                      : "EMPTY SLOT SELECTED";
        invalid();
        return;
      }
      if (pointInRect(point, mutateActionButtonRect(0u))) {
        if (!slot.asset) {
          _instance->status = "SELECT A LOADED BREAK BEFORE FILLING SLOTS";
        } else {
          const uint32_t count =
              fillEmptySlotsWithMutations(*_instance, slotIndex);
          if (count > 0u) {
            _instance->status =
                "BUILDING " +
                std::string(s3g::breakbeat::structuralMutationOperationName(
                    _instance->structuralMutationOperation)) +
                " MUTATIONS IN EMPTY SLOTS";
          } else {
            _instance->status =
                emptyMutationSlotCount(*_instance, slotIndex) > 0u
                    ? "OPERATION CANNOT FIT THE CURRENT SLICE MAP"
                    : "ALL OTHER BREAK SLOTS ARE LOCKED";
          }
        }
        invalid();
        return;
      }
      if (pointInRect(point, mutateActionButtonRect(1u))) {
        if (!slotHasCompleteMap(slot)) {
          _instance->status = "AUTO MAP THE BREAK BEFORE PLAY THROUGH";
        } else {
          _instance->pendingPlaythroughSlot.store(slotIndex + 1u,
                                                  std::memory_order_release);
          _instance->status = "PLAYING THROUGH MAPPED SLICES";
          requestProcess(*_instance);
        }
        invalid();
        return;
      }
      if (pointInRect(point, mutateActionButtonRect(2u))) {
        clearSelectedSlot();
        invalid();
        return;
      }
      if (pointInRect(point, mutateActionButtonRect(3u))) {
        exportCurrentBreak();
        return;
      }
      for (uint32_t index = 0u; index < 5u; ++index) {
        if (!pointInRect(point, mutateOperationRect(index)))
          continue;
        _instance->structuralMutationOperation =
            static_cast<StructuralMutationOperation>(index);
        markStateDirty(*_instance);
        _instance->status =
            std::string(s3g::breakbeat::structuralMutationOperationName(
                _instance->structuralMutationOperation)) +
            " MUTATE OPERATION SELECTED";
        invalid();
        return;
      }
      if (pointInRect(point, mutateOperationAlternateRect())) {
        if (_instance->structuralMutationOperation ==
            StructuralMutationOperation::Collage) {
          _instance->status = "USE REARRANGE AND REPEAT TO SHAPE COLLAGE";
        } else {
          _instance->structuralMutationAlternate =
              !_instance->structuralMutationAlternate;
          markStateDirty(*_instance);
          _instance->status = "MUTATE OPERATION MODE UPDATED";
        }
        invalid();
        return;
      }
      static constexpr std::array<uint8_t, 6u> useBits{{
          s3g::breakbeat::StructuralRearrange,
          s3g::breakbeat::StructuralRepeat,
          s3g::breakbeat::StructuralPitch,
          s3g::breakbeat::StructuralMixerFx,
          s3g::breakbeat::StructuralAuxBus,
          s3g::breakbeat::StructuralReverse,
      }};
      for (uint32_t index = 0u; index < useBits.size(); ++index) {
        if (!pointInRect(point, mutateUseRect(index)))
          continue;
        _instance->structuralMutationUses ^= useBits[index];
        markStateDirty(*_instance);
        _instance->status = "MUTATE OPTIONS UPDATED";
        invalid();
        return;
      }
      return;
    }
    const auto *detailSlot = selectedSampleSlot();
    if (!detailSlot)
      return;
    if (pointInRect(point, sampleLoadButtonRect())) {
      openSamples();
      return;
    }
    if (pointInRect(point, sampleClearButtonRect())) {
      clearSelectedSlot();
      return;
    }
    if (pointInRect(point, sliceActionButtonRect())) {
      applySelectedSliceMethod();
      invalid();
      return;
    }
    if (pointInRect(point, automapButtonRect())) {
      if (automapSlot(*_instance, selectedSlot()))
        _instance->status = "BREAK SLICES AUTO-MAPPED - SLICE EDITING ENABLED";
      else
        _instance->status = "LOAD OR REDUCE SLICES BEFORE AUTO MAP";
      invalid();
      return;
    }
    if (pointInRect(point, sliceMethodMenuRect())) {
      _detailMenuKind = kDetailMenuSliceMethod;
      _detailMenuHover = -1;
      _detailMenuSlot = this->selectedSlot();
      updateDetailNumericFields();
      invalid();
      return;
    }
    if (pointInRect(point, startNoteMenuRect())) {
      _detailMenuKind = kDetailMenuStartNote;
      _detailMenuHover = -1;
      _detailMenuSlot = this->selectedSlot();
      updateDetailNumericFields();
      invalid();
      return;
    }
    for (uint32_t index = 0u; index < 5u; ++index) {
      if (!pointInRect(point, playbackMenuRect(index)))
        continue;
      _detailMenuKind = kDetailMenuTrigger + static_cast<int64_t>(index);
      _detailMenuHover = -1;
      _detailMenuSlot = this->selectedSlot();
      updateDetailNumericFields();
      invalid();
      return;
    }
    for (uint32_t index = 0u; index < kDetailNumericCount; ++index) {
      if (!pointInRect(point, detailNumericHitRect(index)))
        continue;
      if (event.clickCount >= 2) {
        const std::array<double, kDetailNumericCount> defaults{{
            0.0,
            static_cast<double>(kDefaultMinimumTransientSliceMilliseconds) /
                kMaximumMinimumTransientSliceMilliseconds,
            (120.0 - 20.0) / 979.0,
            0.02 / 0.5,
            0.0,
            0.25,
            0.5,
            0.5,
        }};
        const CRect track = detailNumericTrackRect(index);
        updateDetailNumericControl(
            index, CPoint(minX(track) + rectWidth(track) * defaults[index],
                          midY(track)));
        markStateDirty(*_instance);
      } else {
        _detailNumericDragIndex = static_cast<int64_t>(index);
        updateDetailNumericControl(index, point);
      }
      return;
    }
    if (pointInRect(point, sliceReverseButtonRect())) {
      editSelectedSliceControl(6u);
      invalid();
      return;
    }
    if (pointInRect(point, sliceLaunchMenuRect())) {
      const auto *selectedSlot = selectedSampleSlot();
      if (!selectedSlot || !slotHasCompleteMap(*selectedSlot) ||
          selectedSlot->sliceCount == 0u)
        return;
      _detailMenuKind = kDetailMenuSliceLaunch;
      _detailMenuHover = -1;
      _detailMenuSlot = this->selectedSlot();
      updateDetailNumericFields();
      invalid();
      return;
    }
    if (pointInRect(point, sliceChokeMenuRect())) {
      const auto *selectedSlot = selectedSampleSlot();
      if (!selectedSlot || !slotHasCompleteMap(*selectedSlot) ||
          selectedSlot->sliceCount == 0u)
        return;
      _detailMenuKind = kDetailMenuSliceChoke;
      _detailMenuHover = -1;
      _detailMenuSlot = this->selectedSlot();
      updateDetailNumericFields();
      invalid();
      return;
    }
    for (uint32_t index = 0u; index < 4u; ++index) {
      if (!pointInRect(point, envelopeSliderHitRect(index)))
        continue;
      const auto *slot = selectedSampleSlot();
      if (!slot || !slot->asset || slot->sliceCount == 0u)
        return;
      _envelopeDragIndex = static_cast<int64_t>(index);
      _dragBank = editableBank(*_instance);
      updateEnvelopeDragAt(point);
      return;
    }
    if (pointInRect(point, keyboardRect())) {
      const auto *bank = displayBank();
      if (!bank)
        return;
      const auto &slot = bank->slots[selectedSlot()];
      constexpr uint32_t keyCount = 24u;
      const uint32_t index = std::min<uint32_t>(
          keyCount - 1u,
          static_cast<uint32_t>((point.x - keyboardRect().left) /
                                (keyboardRect().getWidth() / keyCount)));
      if (index < slot.mappedSliceCount &&
          slot.mappedRootNote == slot.rootNote) {
        _selectedSlice = static_cast<int64_t>(index);
        audition();
      } else {
        _instance->status = "KEY IS NOT MAPPED - PRESS AUTO MAP";
      }
      invalid();
      return;
    }
    if (pointInRect(point, waveformNavigatorRect())) {
      const auto *slot = selectedSampleSlot();
      if (!slot || !slot->asset)
        return;
      const uint32_t total = slot->asset->frameCount();
      const CRect handle = viewportHandleForTotal(total);
      _dragViewport = true;
      if (pointInRect(point, handle)) {
        _viewportGrabOffset = point.x - handle.left;
      } else {
        _viewportGrabOffset = handle.getWidth() * 0.5;
        setViewportHandleOrigin(point.x - _viewportGrabOffset, total);
      }
      invalid();
      return;
    }
    if (!pointInRect(point, waveformRect()))
      return;
    const auto *slot = selectedSampleSlot();
    if (!slot || !slot->asset)
      return;
    const int64_t loopHandle = loopHandleNearPoint(point);
    if (loopHandle >= 0) {
      _dragLoopHandle = loopHandle;
      _dragBank = editableBank(*_instance);
      _instance->status = loopHandle == 0 ? "DRAG LOOP START" : "DRAG LOOP END";
      return;
    }
    const int64_t marker = markerNearPoint(point);
    if (marker > 0) {
      _dragMarker = marker;
      _dragBank = editableBank(*_instance);
      return;
    }
    const uint32_t frame = frameForX(point.x, slot->asset->frameCount());
    if (event.clickCount >= 2) {
      auto bank = editableBank(*_instance);
      auto &edited = bank->slots[selectedSlot()];
      std::size_t count = edited.sliceCount;
      const uint32_t snapped = s3g::breakbeat::nearestZeroFrame(
          *edited.asset, frame,
          static_cast<uint32_t>(std::lround(edited.asset->sampleRate * 0.004)));
      const std::size_t maximum = std::min<std::size_t>(
          edited.slices.size(),
          s3g::breakbeat::maximumSlicesForStartNote(edited.rootNote));
      if (s3g::breakbeat::addSliceMarker(edited.slices.data(), count, maximum,
                                         snapped)) {
        edited.sliceCount = static_cast<uint16_t>(count);
        edited.mappedSliceCount = 0u;
        publishBank(*_instance, std::move(bank));
        _selectedSlice = sliceAtFrame(snapped);
        _instance->status = "MARKER ADDED - PRESS AUTO MAP";
      } else if (count >= maximum) {
        _instance->status = "START NOTE SLICE LIMIT REACHED";
      }
    } else {
      _selectedSlice = sliceAtFrame(frame);
    }
    invalid();
  }

  void mouseDragged(MouseMoveEvent &event) {
    if (_detailNumericDragIndex >= 0) {
      updateDetailNumericControl(static_cast<uint32_t>(_detailNumericDragIndex),
                                 event.mousePosition);
      return;
    }
    if (_envelopeDragIndex >= 0) {
      updateEnvelopeDragAt(event.mousePosition);
      return;
    }
    if (_mixerDragKind != 0) {
      updateMixerDragAt(event.mousePosition);
      return;
    }
    if (_dragViewport) {
      const auto *slot = selectedSampleSlot();
      if (!slot || !slot->asset)
        return;
      const CPoint point = event.mousePosition;
      setViewportHandleOrigin(point.x - _viewportGrabOffset,
                              slot->asset->frameCount());
      invalid();
      return;
    }
    if (_dragLoopHandle >= 0 && _dragBank) {
      const CPoint point = event.mousePosition;
      auto &slot = _dragBank->slots[selectedSlot()];
      if (!slot.asset || _selectedSlice < 0 ||
          static_cast<std::size_t>(_selectedSlice) >= slot.sliceCount)
        return;
      auto &slice = slot.slices[static_cast<std::size_t>(_selectedSlice)];
      uint32_t loopStart =
          slice.loopStartFrame == 0u && slice.loopEndFrame == 0u
              ? slice.startFrame
              : slice.loopStartFrame;
      uint32_t loopEnd = slice.loopStartFrame == 0u && slice.loopEndFrame == 0u
                             ? slice.endFrame
                             : slice.loopEndFrame;
      uint32_t frame = frameForX(point.x, slot.asset->frameCount());
      const double sliceEndX =
          xForFrame(slice.endFrame, slot.asset->frameCount());
      if (_dragLoopHandle == 1 && point.x >= sliceEndX - 2.0)
        frame = slice.endFrame;
      else
        frame = s3g::breakbeat::nearestZeroFrame(
            *slot.asset, frame,
            static_cast<uint32_t>(std::lround(slot.asset->sampleRate * 0.004)));
      if (_dragLoopHandle == 0)
        loopStart = std::clamp(frame, slice.startFrame, loopEnd - 1u);
      else
        loopEnd = std::clamp(frame, loopStart + 1u, slice.endFrame);
      slice.loopStartFrame = loopStart;
      slice.loopEndFrame = loopEnd;
      invalid();
      return;
    }
    if (_dragMarker <= 0 || !_dragBank)
      return;
    const CPoint point = event.mousePosition;
    auto &slot = _dragBank->slots[selectedSlot()];
    if (!slot.asset)
      return;
    uint32_t frame = frameForX(point.x, slot.asset->frameCount());
    frame = s3g::breakbeat::nearestZeroFrame(
        *slot.asset, frame,
        static_cast<uint32_t>(std::lround(slot.asset->sampleRate * 0.004)));
    s3g::breakbeat::moveSliceMarker(slot.slices.data(), slot.sliceCount,
                                    static_cast<std::size_t>(_dragMarker),
                                    frame);
    invalid();
  }

  void mouseUp() {
    if (_detailNumericDragIndex >= 0) {
      markStateDirty(*_instance);
      _detailNumericDragIndex = kDetailNumericNone;
      updateDetailNumericFields();
      invalid();
      return;
    }
    if (_envelopeDragIndex >= 0) {
      if (_dragBank && publishBank(*_instance, _dragBank))
        _instance->status = "BREAK ENVELOPE UPDATED";
      _dragBank.reset();
      _envelopeDragIndex = -1;
      invalid();
      return;
    }
    if (_mixerDragKind != 0) {
      if (_mixerDragKind == 3) {
        markStateDirty(*_instance);
        _instance->status = "MAIN OUTPUT GAIN UPDATED";
        paramEvent(s3g::clap_gui::ParamEventKind::GestureEnd);
      } else if (_dragBank && publishMixerBank(*_instance, _dragBank)) {
        _instance->status =
            _mixerDragKind >= 20
                ? "BREAK INSERT UPDATED"
                : _mixerDragKind >= 9
                      ? "AUX BUS PROCESSOR UPDATED"
                      : _mixerDragKind == 1
                            ? "BREAK LEVEL UPDATED"
                            : _mixerDragKind == 2
                                  ? "BREAK PAN UPDATED"
                                  : _mixerDragKind == 8
                                        ? "BREAK AUX SEND UPDATED"
                                        : "BREAK EQ UPDATED";
      }
      _dragBank.reset();
      _mixerDragKind = 0;
      invalid();
      return;
    }
    if (_dragLoopHandle >= 0) {
      if (_dragBank && publishBank(*_instance, _dragBank))
        _instance->status = "SLICE LOOP POINTS UPDATED";
      _dragBank.reset();
      _dragLoopHandle = -1;
      invalid();
      return;
    }
    if (_dragBank) {
      const auto &slot = _dragBank->slots[selectedSlot()];
      const bool mappingPreserved = slot.mappedSliceCount > 0u &&
                                    slot.mappedSliceCount == slot.sliceCount;
      if (publishBank(*_instance, _dragBank))
        _instance->status =
            mappingPreserved ? "MARKER MOVED - MAP PRESERVED" : "MARKER MOVED";
      _dragBank.reset();
    }
    _dragMarker = -1;
    _dragLoopHandle = -1;
    _dragViewport = false;
  }

  void rightMouseDown(MouseDownEvent &event) {
    const CPoint point = event.mousePosition;
    if (_page != 1u)
      return;
    if (!pointInRect(point, waveformRect()))
      return;
    const int64_t marker = markerNearPoint(point);
    if (marker <= 0)
      return;
    auto bank = editableBank(*_instance);
    auto &slot = bank->slots[selectedSlot()];
    std::size_t count = slot.sliceCount;
    if (s3g::breakbeat::deleteSliceMarker(slot.slices.data(), count,
                                          static_cast<std::size_t>(marker))) {
      slot.sliceCount = static_cast<uint16_t>(count);
      slot.mappedSliceCount = 0u;
      publishBank(*_instance, std::move(bank));
      _selectedSlice =
          std::min<int64_t>(_selectedSlice, static_cast<int64_t>(count) - 1);
      _instance->status = "MARKER DELETED - PRESS AUTO MAP";
      invalid();
    }
  }

  void scrollWheel(MouseWheelEvent &event) {
    if (_page != 1u)
      return;
    const auto *slot = selectedSampleSlot();
    if (!slot || !slot->asset)
      return;
    const CPoint point = event.mousePosition;
    if (!pointInRect(point, waveformRect()) &&
        !pointInRect(point, waveformNavigatorRect()))
      return;
    const uint32_t total = slot->asset->frameCount();
    const double deltaX = (event.deltaX * 10.0);
    const double deltaY = (event.deltaY * 10.0);
    const bool horizontal =
        std::abs(deltaX) > std::abs(deltaY) ||
        (event.modifiers.has(VSTGUI::ModifierKey::Shift)) != 0u;
    if (horizontal && _zoom > 1.0) {
      const uint32_t visible = visibleFrames(total);
      const double gesture = std::abs(deltaX) > 0.0 ? deltaX : deltaY;
      _horizontalPanRemainder += static_cast<double>(gesture) *
                                 static_cast<double>(visible) /
                                 waveformRect().getWidth() * 0.35;
      const int64_t frameDelta = static_cast<int64_t>(_horizontalPanRemainder);
      _horizontalPanRemainder -= static_cast<double>(frameDelta);
      const int64_t maximum = total > visible ? total - visible : 0u;
      _visibleStart = static_cast<uint32_t>(std::clamp<int64_t>(
          static_cast<int64_t>(_visibleStart) + frameDelta, 0, maximum));
      invalid();
      return;
    }
    if (deltaY == 0.0)
      return;
    const uint32_t anchor = frameForX(point.x, total);
    const double previousZoom = _zoom;
    _zoom *= deltaY > 0.0 ? 1.25 : 0.8;
    _zoom = std::clamp<double>(
        _zoom, 1.0, std::max<double>(1.0, static_cast<double>(total) / 32.0));
    if (_zoom != previousZoom) {
      const double fraction = std::clamp<double>(
          (point.x - waveformRect().left) / waveformRect().getWidth(), 0.0,
          1.0);
      const uint32_t visible = visibleFrames(total);
      const int64_t desired =
          static_cast<int64_t>(anchor) -
          static_cast<int64_t>(std::llround(fraction * visible));
      _visibleStart = static_cast<uint32_t>(std::clamp<int64_t>(
          desired, 0, total > visible ? total - visible : 0u));
    }
    invalid();
  }

  bool loadDocumentationBreaks() {
    constexpr const char *environmentNames[]{
        "S3G_GUI_DOCUMENTATION_SAMPLE_PATH",
        "S3G_GUI_DOCUMENTATION_SAMPLE_PATH_2",
        "S3G_GUI_DOCUMENTATION_SAMPLE_PATH_3",
        "S3G_GUI_DOCUMENTATION_SAMPLE_PATH_4",
    };
    for (uint32_t index = 0u; index < std::size(environmentNames); ++index) {
      const char *path = std::getenv(environmentNames[index]);
      if (!path || !path[0])
        return false;
      queueSampleLoad(*_instance, index, path);
    }

    bool loaded = false;
    for (uint32_t attempt = 0u; attempt < 1000u; ++attempt) {
      serviceSampleLoads(*_instance);
      loaded = _instance->controlBank != nullptr;
      for (uint32_t index = 0u; loaded && index < 4u; ++index) {
        loaded = _instance->controlBank->slots[index].asset != nullptr;
      }
      if (loaded)
        break;
      std::this_thread::sleep_for(std::chrono::duration<double>(0.002));
    }
    if (!loaded)
      return false;

    for (uint32_t index = 0u; index < 4u; ++index) {
      const auto &slot = _instance->controlBank->slots[index];
      const auto analysis = _instance->analyses[index];
      if (!slot.asset || !analysis)
        return false;
      const uint32_t zeroRadius =
          static_cast<uint32_t>(std::lround(slot.asset->sampleRate * 0.004));
      const auto slices = s3g::breakbeat::makeTransientSlices(
          *slot.asset, *analysis, 32u, zeroRadius,
          _instance->transientPreRollMicroseconds,
          static_cast<uint32_t>(std::lround(
              slot.asset->sampleRate *
              _instance->minimumTransientSliceMilliseconds * 0.001)));
      if (slices.empty() || !replaceSlotSlices(*_instance, index, slices) ||
          !automapSlot(*_instance, index))
        return false;
    }

    auto mixerBank = editableBank(*_instance);
    if (!mixerBank)
      return false;
    constexpr std::array<float, 4u> gains{{1.00f, 0.88f, 1.08f, 0.94f}};
    constexpr std::array<float, 4u> pans{{-0.42f, 0.36f, -0.18f, 0.24f}};
    constexpr std::array<float, 4u> sends{{0.46f, 0.31f, 0.58f, 0.39f}};
    constexpr std::array<s3g::breakbeat::InsertType, 4u> firstInserts{{
        s3g::breakbeat::InsertType::Filter,
        s3g::breakbeat::InsertType::Transient,
        s3g::breakbeat::InsertType::Degrade,
        s3g::breakbeat::InsertType::Resonator,
    }};
    constexpr std::array<s3g::breakbeat::InsertType, 4u> secondInserts{{
        s3g::breakbeat::InsertType::Wavefolder,
        s3g::breakbeat::InsertType::Off,
        s3g::breakbeat::InsertType::Repeater,
        s3g::breakbeat::InsertType::Erosion,
    }};
    for (uint32_t index = 0u; index < 4u; ++index) {
      auto &slot = mixerBank->slots[index];
      slot.mixerGain = gains[index];
      slot.mixerPan = pans[index];
      slot.mixerLowEqDb = index % 2u == 0u ? 1.8f : -1.2f;
      slot.mixerMidEqDb = index < 2u ? -1.5f : 1.1f;
      slot.mixerHighEqDb = index % 2u == 0u ? 0.9f : 1.6f;
      slot.mixerMidFrequencyHz = 520.0f + 410.0f * index;
      slot.mixerAuxSend = sends[index];
      slot.inserts[0] =
          s3g::breakbeat::defaultInsertSettings(firstInserts[index]);
      slot.inserts[1] =
          s3g::breakbeat::defaultInsertSettings(secondInserts[index]);
    }
    auto &documentedSlice = mixerBank->slots[0u].slices[0u];
    const auto &documentedAsset = *mixerBank->slots[0u].asset;
    const uint32_t zeroRadius =
        static_cast<uint32_t>(std::lround(documentedAsset.sampleRate * 0.004));
    const uint32_t documentedLoopStart = s3g::breakbeat::nearestZeroFrame(
        documentedAsset,
        documentedSlice.startFrame +
            (documentedSlice.endFrame - documentedSlice.startFrame) / 4u,
        zeroRadius);
    const uint32_t documentedLoopEnd = s3g::breakbeat::nearestZeroFrame(
        documentedAsset,
        documentedSlice.startFrame +
            (documentedSlice.endFrame - documentedSlice.startFrame) * 3u / 4u,
        zeroRadius);
    if (documentedLoopStart >= documentedSlice.startFrame &&
        documentedLoopStart < documentedLoopEnd &&
        documentedLoopEnd <= documentedSlice.endFrame) {
      documentedSlice.launchMode = s3g::breakbeat::LaunchMode::Loop;
      documentedSlice.loopStartFrame = documentedLoopStart;
      documentedSlice.loopEndFrame = documentedLoopEnd;
    }
    mixerBank->auxEnabled = true;
    mixerBank->auxPress = 0.52f;
    mixerBank->auxSnap = 0.24f;
    mixerBank->auxRecovery = 0.41f;
    mixerBank->auxSaturation = 0.28f;
    mixerBank->auxBite = 0.17f;
    mixerBank->auxClip = 0.08f;
    mixerBank->auxTilt = -0.12f;
    mixerBank->auxReturnDb = -7.5f;
    mixerBank->auxLinkMode = s3g::BreakBusLinkMode::Pair;
    if (!publishBank(*_instance, std::move(mixerBank), false))
      return false;
    constexpr std::array<float, 4u> peaks{{0.78f, 0.57f, 0.69f, 0.49f}};
    for (std::size_t index = 0u; index < peaks.size(); ++index)
      _instance->slotPeaks[index].store(peaks[index],
                                        std::memory_order_relaxed);
    _instance->auxActivity.store(0.61f, std::memory_order_relaxed);
    _instance->auxGainReductionDb.store(-3.8f, std::memory_order_relaxed);
    _instance->outputPeak.store(0.82f, std::memory_order_relaxed);
    _instance->selectedSlot = 0u;
    _selectedSlice = 0;
    _zoom = 1.0;
    _visibleStart = 0u;
    _page = 0u;
    _instance->status = "DOCUMENTATION BREAK BANK READY";
    invalid();
    invalid();
    return true;
  }

  void setDocumentationPage(size_t page) {
    closeDetailMenu();
    _page = static_cast<uint32_t>(std::min<size_t>(page, 3u));
    if (_page == 3u && _instance->controlBank &&
        (_instance->controlBank->slots[2u].asset ||
         _instance->controlBank->slots[3u].asset)) {
      auto buildBank = editableBank(*_instance);
      for (uint32_t index = 2u; index < 4u; ++index) {
        const uint8_t root = buildBank->slots[index].rootNote;
        const uint8_t midi = buildBank->slots[index].midiChannel;
        s3g::breakbeat::clearMappingsForSlot(*buildBank,
                                             static_cast<uint8_t>(index));
        buildBank->slots[index] = {};
        buildBank->slots[index].rootNote = root;
        buildBank->slots[index].mappedRootNote = root;
        buildBank->slots[index].midiChannel = midi;
        _instance->samplePaths[index].clear();
        _instance->sampleFileBytes[index] = 0u;
        _instance->projectFiles[index].clear();
        _instance->projectStoragePending[index] = false;
        _instance->analyses[index].reset();
        resetSlotVariations(*_instance, index);
      }
      (void)publishBank(*_instance, std::move(buildBank), false);
      _instance->selectedSlot = 0u;
      _instance->structuralMutationUses =
          s3g::breakbeat::kDefaultStructuralMutationUses;
      _instance->structuralMutationOperation =
          StructuralMutationOperation::Collage;
      _instance->structuralMutationAlternate = false;
    }
    updateDetailNumericFields();
    invalid();
    invalid();
  }

  bool runDocumentationMutationFill() {
    if (!_instance->controlBank || !_instance->controlBank->slots[0u].asset) {
      std::fprintf(stderr, "Slicer mutation fill: source unavailable\n");
      return false;
    }
    _instance->selectedSlot = 0u;
    const uint32_t queued = fillEmptySlotsWithMutations(*_instance, 0u);
    if (queued != 2u) {
      std::fprintf(stderr, "Slicer mutation fill: queued %u, expected 2\n",
                   queued);
      return false;
    }
    for (uint32_t attempt = 0u; attempt < 5000u; ++attempt) {
      serviceSampleLoads(*_instance);
      if (!_instance->pendingMutationSlots[2u] &&
          !_instance->pendingMutationSlots[3u])
        break;
      std::this_thread::sleep_for(std::chrono::duration<double>(0.002));
    }
    for (uint32_t index = 2u; index < 4u; ++index) {
      const auto &slot = _instance->controlBank->slots[index];
      if (_instance->pendingMutationSlots[index] || !slot.asset ||
          slot.sliceCount <= 1u || slot.mappedSliceCount != slot.sliceCount ||
          _instance->samplePaths[index].rfind("MUTATED BREAK ", 0u) != 0u) {
        std::fprintf(
            stderr,
            "Slicer mutation fill: slot %u pending=%d asset=%d slices=%u "
            "mapped=%u path='%s' status='%s'\n",
            index + 1u, _instance->pendingMutationSlots[index],
            slot.asset != nullptr, static_cast<unsigned>(slot.sliceCount),
            static_cast<unsigned>(slot.mappedSliceCount),
            _instance->samplePaths[index].c_str(), _instance->status.c_str());
        return false;
      }
    }
    return true;
  }

  void drawWaveformForSlot(const s3g::breakbeat::SampleSlot &slot) {
    const CRect rect = waveformRect();
    context->setFillColor(color(0x111111));
    fillRect(rect);
    context->setFrameColor(color(0x555555));
    frameRect(rect);
    if (!slot.asset) {
      drawAt("LOAD A SAMPLE INTO THE SELECTED BANK SLOT",
             CPoint(rect.left + 18.0, rect.top + 18.0), slicerSoftLabelAttrs());
      return;
    }

    const auto &asset = *slot.asset;
    const uint32_t total = asset.frameCount();
    const uint32_t visible = visibleFrames(total);
    const uint32_t start = clampedStart(total);
    const uint32_t end = std::min<uint32_t>(total, start + visible);
    const uint32_t pixels = std::max<uint32_t>(
        1u, static_cast<uint32_t>(std::floor(rect.getWidth() - 2.0)));
    if (_selectedSlice >= 0 &&
        static_cast<std::size_t>(_selectedSlice) < slot.sliceCount) {
      const auto &selected =
          slot.slices[static_cast<std::size_t>(_selectedSlice)];
      const double selectedX = xForFrame(selected.startFrame, total);
      const double selectedEndX = xForFrame(selected.endFrame, total);
      // Paint selection into the waveform background. Waveform traces are
      // drawn afterward and therefore remain fully legible.
      context->setFillColor(color(0x1b191c));
      fillRect(intersection(rect,
                            box(selectedX, rect.top + 1.0,
                                std::max<double>(1.0, selectedEndX - selectedX),
                                rect.getHeight() - 2.0)));
    }
    const auto drawChannel = [&](const std::vector<float> &source, double top,
                                 double height, CColor color) {
      DrawPath path = DrawPath{};
      path.lineWidth = 1.0;
      const double center = top + height * 0.5;
      if (visible <= pixels) {
        // At sample-level zoom, join the actual sample values. The
        // overview renderer below intentionally uses min/max columns,
        // but those columns look blocky once individual samples fit.
        const double drawableWidth = rect.getWidth() - 2.0;
        const double denominator =
            static_cast<double>(std::max<uint32_t>(1u, visible - 1u));
        bool began = false;
        for (uint32_t frame = start; frame < end; ++frame) {
          const double fraction =
              static_cast<double>(frame - start) / denominator;
          const double px = rect.left + 1.0 + drawableWidth * fraction;
          const double py = center - source[frame] * (height * 0.44);
          if (!began) {
            path.move(CPoint(px, py));
            began = true;
          } else {
            path.line(CPoint(px, py));
          }
        }
        context->setFrameColor(color);
        path.stroke(*context);
        return;
      }
      for (uint32_t x = 0u; x < pixels; ++x) {
        const uint32_t first =
            start +
            static_cast<uint32_t>(static_cast<uint64_t>(visible) * x / pixels);
        const uint32_t last = std::min<uint32_t>(
            end, start +
                     static_cast<uint32_t>(static_cast<uint64_t>(visible) *
                                           (x + 1u) / pixels) +
                     1u);
        float minimum = 1.0f;
        float maximum = -1.0f;
        const uint32_t stride =
            std::max<uint32_t>(1u, (last > first ? last - first : 1u) / 32u);
        for (uint32_t frame = first; frame < last; frame += stride) {
          minimum = std::min(minimum, source[frame]);
          maximum = std::max(maximum, source[frame]);
        }
        const double px = rect.left + 1.0 + x;
        const double y1 = center - maximum * (height * 0.44);
        const double y2 = center - minimum * (height * 0.44);
        path.move(CPoint(px, y1));
        path.line(CPoint(px, y2));
      }
      context->setFrameColor(color);
      path.stroke(*context);
    };
    const double usableHeight = rect.getHeight() - 4.0;
    const double laneHeight =
        usableHeight / static_cast<double>(asset.channelCount);
    for (uint32_t channel = 0u; channel < asset.channelCount; ++channel) {
      const double top =
          rect.top + 2.0 + laneHeight * static_cast<double>(channel);
      drawChannel(asset.channels[channel], top, laneHeight,
                  color(channel % 2u == 0u ? 0x747d78 : 0x68706c));
      if (channel != 0u) {
        context->setFillColor(color(0x333333));
        fillRect(box(rect.left + 1.0, top, rect.getWidth() - 2.0, 1.0));
      }
      if (asset.channelCount > 2u) {
        std::string channelLabel =
            fmt("%02u", static_cast<unsigned>(channel + 1u));
        drawAt(channelLabel, CPoint(rect.left + 5.0, top + 1.0),
               slicerTextAttrs(color(0x676767), 8.0));
      }
    }

    for (std::size_t index = 0u; index < slot.sliceCount; ++index) {
      const auto &slice = slot.slices[index];
      const double x = xForFrame(slice.startFrame, total);
      if (x < rect.left || x > maxX(rect))
        continue;
      context->setFrameColor(color(static_cast<int64_t>(index) == _selectedSlice
                                       ? 0x756875
                                       : index == 0u ? 0x8e824f : 0x805b5d));
      DrawPath marker = DrawPath{};
      marker.move(CPoint(x, rect.top + 1.0));
      marker.line(CPoint(x, maxY(rect) - 1.0));
      marker.stroke(*context);
    }

    if (slotHasCompleteMap(slot) && _selectedSlice >= 0 &&
        static_cast<std::size_t>(_selectedSlice) < slot.sliceCount) {
      const auto &selected =
          slot.slices[static_cast<std::size_t>(_selectedSlice)];
      if (selected.launchMode == s3g::breakbeat::LaunchMode::Loop ||
          selected.launchMode == s3g::breakbeat::LaunchMode::PingPong) {
        const uint32_t loopStart =
            selected.loopStartFrame == 0u && selected.loopEndFrame == 0u
                ? selected.startFrame
                : selected.loopStartFrame;
        const uint32_t loopEnd =
            selected.loopStartFrame == 0u && selected.loopEndFrame == 0u
                ? selected.endFrame
                : selected.loopEndFrame;
        const std::array<std::pair<uint32_t, std::string>, 2u> handles{{
            {loopStart, "LS"},
            {loopEnd, "LE"},
        }};
        for (const auto &handle : handles) {
          const double x = xForFrame(handle.first, total);
          if (x < rect.left || x > maxX(rect))
            continue;
          CColor handleColor = color(0xb5a56c);
          context->setFrameColor(handleColor);
          DrawPath line = DrawPath{};
          line.lineWidth = 1.5;
          line.move(CPoint(x, rect.top + 14.0));
          line.line(CPoint(x, maxY(rect) - 1.0));
          line.stroke(*context);
          TextStyle handleAttrs = slicerTextAttrs(color(0xe1d49c), 7.5, 0);
          const TextSize textSize = this->textSize(handle.second, handleAttrs);
          const CRect flagRect =
              box(std::clamp<double>(x - textSize.width * 0.5 - 3.0,
                                     rect.left + 1.0,
                                     maxX(rect) - textSize.width - 7.0),
                  rect.top + 2.0, textSize.width + 6.0, textSize.height + 2.0);
          context->setFillColor(color(0x2c291f));
          fillRect(flagRect);
          context->setFrameColor(handleColor);
          frameRect(flagRect);
          drawAt(handle.second, CPoint(flagRect.left + 3.0, flagRect.top),
                 handleAttrs);
        }
      }
    }

    for (std::size_t cursorIndex = 0u;
         cursorIndex < _instance->voicePlayheads.size(); ++cursorIndex) {
      if (_instance->voicePlayheadSlots[cursorIndex].load(
              std::memory_order_relaxed) != selectedSlot())
        continue;
      const float playhead = _instance->voicePlayheads[cursorIndex].load(
          std::memory_order_relaxed);
      if (playhead < 0.0f)
        continue;
      const uint32_t frame = std::min<uint32_t>(
          total - 1u,
          static_cast<uint32_t>(playhead * static_cast<float>(total)));
      const double x = xForFrame(frame, total);
      if (x >= rect.left && x <= maxX(rect)) {
        CColor cursorColor =
            color(cursorIndex % 2u == 0u ? 0xa9d18e : 0x8fb8cf);
        context->setFrameColor(cursorColor);
        DrawPath cursor = DrawPath{};
        cursor.lineWidth = 2.0;
        cursor.move(CPoint(x, rect.top + 1.0));
        cursor.line(CPoint(x, maxY(rect) - 1.0));
        cursor.stroke(*context);
        const uint8_t key = _instance->voicePlayheadKeys[cursorIndex].load(
            std::memory_order_relaxed);
        std::string flag =
            fmt("%s/%u", shortNoteText(key), static_cast<unsigned>(key));
        TextStyle flagAttrs = slicerTextAttrs(color(0xd0d8d3), 7.5, 0);
        const TextSize flagSize = textSize(flag, flagAttrs);
        const double flagX = std::clamp<double>(
            x + 3.0, rect.left + 2.0, maxX(rect) - flagSize.width - 8.0);
        const CRect flagRect = box(flagX, rect.top + 4.0, flagSize.width + 6.0,
                                   flagSize.height + 3.0);
        context->setFillColor(color(0x202522));
        fillRect(flagRect);
        context->setFrameColor(cursorColor);
        frameRect(flagRect);
        drawAt(flag, CPoint(flagX + 3.0, flagRect.top + 1.0), flagAttrs);
      }
    }

    const CRect navigator = waveformNavigatorRect();
    context->setFillColor(color(0x161616));
    fillRect(navigator);
    context->setFrameColor(color(0x444444));
    frameRect(navigator);
    const CRect handle = viewportHandleForTotal(total);
    context->setFillColor(color(_zoom > 1.0 ? 0x66616a : 0x3d3d3d));
    fillRect(handle);
    context->setFrameColor(color(_zoom > 1.0 ? 0x8b8490 : 0x515151));
    frameRect(handle);
  }

  void drawOverviewWaveformForSlot(const s3g::breakbeat::SampleSlot &slot,
                                   uint32_t slotIndex) {
    const CRect rect = overviewWaveformRect(slotIndex);
    const bool selected = slotIndex == selectedSlot();
    context->setFillColor(color(selected ? 0x171b19 : 0x111111));
    fillRect(rect);
    context->setFrameColor(color(selected ? 0x59645d : 0x3d3d3d));
    frameRect(rect);
    TextStyle label = slicerTextAttrs(color(0xa8ada9), 10.0);
    std::string heading =
        fmt("BREAK %u   %s", static_cast<unsigned>(slotIndex + 1u),
            portableSlotFilename(*_instance, slotIndex));
    drawAt(heading, CPoint(rect.left + 8.0, rect.top + 5.0), label);
    if (!slot.asset) {
      drawAt("DROP SAMPLE HERE", CPoint(rect.left + 8.0, rect.top + 42.0),
             slicerSoftLabelAttrs());
      return;
    }
    const auto &asset = *slot.asset;
    const uint32_t total = asset.frameCount();
    const double top = rect.top + 23.0;
    const double height = rect.getHeight() - 27.0;
    const double center = top + height * 0.5;
    const uint32_t pixels =
        std::max<uint32_t>(1u, static_cast<uint32_t>(rect.getWidth() - 2.0));
    DrawPath trace = DrawPath{};
    trace.lineWidth = 1.0;
    for (uint32_t x = 0u; x < pixels; ++x) {
      const uint32_t first =
          static_cast<uint32_t>(static_cast<uint64_t>(total) * x / pixels);
      const uint32_t last = std::min<uint32_t>(
          total, std::max(first + 1u,
                          static_cast<uint32_t>(static_cast<uint64_t>(total) *
                                                (x + 1u) / pixels)));
      float minimum = 1.0f;
      float maximum = -1.0f;
      const uint32_t stride = std::max<uint32_t>(1u, (last - first) / 24u);
      for (uint32_t channel = 0u; channel < asset.channelCount; ++channel) {
        const auto &source = asset.channels[channel];
        for (uint32_t frame = first; frame < last; frame += stride) {
          minimum = std::min(minimum, source[frame]);
          maximum = std::max(maximum, source[frame]);
        }
      }
      const double px = rect.left + 1.0 + x;
      trace.move(CPoint(px, center - maximum * height * 0.42));
      trace.line(CPoint(px, center - minimum * height * 0.42));
    }
    context->setFrameColor(color(0x77817b));
    trace.stroke(*context);
    for (uint16_t slice = 0u; slice < slot.sliceCount; ++slice) {
      const double x =
          rect.left + rect.getWidth() *
                          static_cast<double>(slot.slices[slice].startFrame) /
                          static_cast<double>(total);
      context->setFrameColor(color(slice == 0u ? 0x9b8d56 : 0x865f62));
      DrawPath marker = DrawPath{};
      marker.move(CPoint(x, top));
      marker.line(CPoint(x, maxY(rect) - 2.0));
      marker.stroke(*context);
    }
    for (std::size_t cursorIndex = 0u;
         cursorIndex < _instance->voicePlayheads.size(); ++cursorIndex) {
      if (_instance->voicePlayheadSlots[cursorIndex].load(
              std::memory_order_relaxed) != slotIndex)
        continue;
      const float playhead = _instance->voicePlayheads[cursorIndex].load(
          std::memory_order_relaxed);
      if (playhead < 0.0f)
        continue;
      const double x = rect.left + rect.getWidth() * playhead;
      context->setFrameColor(
          color(cursorIndex % 2u == 0u ? 0xa9d18e : 0x8fb8cf));
      DrawPath cursor = DrawPath{};
      cursor.lineWidth = 2.0;
      cursor.move(CPoint(x, top));
      cursor.line(CPoint(x, maxY(rect) - 2.0));
      cursor.stroke(*context);
    }
  }

  void drawMutateWaveformForSlot(const s3g::breakbeat::SampleSlot &slot) {
    const CRect rect = mutateWaveformRect();
    context->setFillColor(color(0x111111));
    fillRect(rect);
    context->setFrameColor(color(0x4f5853));
    frameRect(rect);
    TextStyle label = slicerTextAttrs(color(0xa8ada9), 10.0);
    TextStyle value = slicerTextAttrs(color(0xc6cbc8), 11.0, 0);
    const uint32_t slotIndex = selectedSlot();
    std::string heading = fmt("BREAK %u  //  MUTATION SOURCE  //  %s",
                              static_cast<unsigned>(slotIndex + 1u),
                              portableSlotFilename(*_instance, slotIndex));
    drawAt(heading, CPoint(rect.left + 12.0, rect.top + 9.0), value);
    if (!slot.asset) {
      drawAt("LOAD A BREAK TO USE IT AS A MUTATION SOURCE",
             CPoint(rect.left + 12.0, rect.top + 54.0), slicerSoftLabelAttrs());
      return;
    }
    const auto &asset = *slot.asset;
    const uint32_t total = asset.frameCount();
    const double top = rect.top + 36.0;
    const double height = rect.getHeight() - 62.0;
    const double center = top + height * 0.5;
    const uint32_t pixels =
        std::max<uint32_t>(1u, static_cast<uint32_t>(rect.getWidth() - 24.0));
    DrawPath trace = DrawPath{};
    trace.lineWidth = 1.0;
    for (uint32_t x = 0u; x < pixels; ++x) {
      const uint32_t first =
          static_cast<uint32_t>(static_cast<uint64_t>(total) * x / pixels);
      const uint32_t last = std::min<uint32_t>(
          total, std::max(first + 1u,
                          static_cast<uint32_t>(static_cast<uint64_t>(total) *
                                                (x + 1u) / pixels)));
      float minimum = 1.0f;
      float maximum = -1.0f;
      const uint32_t stride = std::max<uint32_t>(1u, (last - first) / 24u);
      for (uint32_t channel = 0u; channel < asset.channelCount; ++channel) {
        const auto &samples = asset.channels[channel];
        for (uint32_t frame = first; frame < last; frame += stride) {
          minimum = std::min(minimum, samples[frame]);
          maximum = std::max(maximum, samples[frame]);
        }
      }
      const double px = rect.left + 12.0 + x;
      trace.move(CPoint(px, center - maximum * height * 0.43));
      trace.line(CPoint(px, center - minimum * height * 0.43));
    }
    context->setFrameColor(color(0x7b8780));
    trace.stroke(*context);
    for (uint16_t index = 0u; index < slot.sliceCount; ++index) {
      const auto &slice = slot.slices[index];
      const double x = rect.left + 12.0 +
                       static_cast<double>(pixels) * slice.startFrame / total;
      context->setFrameColor(color(slice.reverse ? 0x9a6a72 : 0x8e824f));
      DrawPath marker = DrawPath{};
      marker.move(CPoint(x, top));
      marker.line(CPoint(x, top + height));
      marker.stroke(*context);
    }
    for (std::size_t cursorIndex = 0u;
         cursorIndex < _instance->voicePlayheads.size(); ++cursorIndex) {
      if (_instance->voicePlayheadSlots[cursorIndex].load(
              std::memory_order_relaxed) != slotIndex)
        continue;
      const float playhead = _instance->voicePlayheads[cursorIndex].load(
          std::memory_order_relaxed);
      if (playhead < 0.0f)
        continue;
      const double x =
          rect.left + 12.0 + static_cast<double>(pixels) * playhead;
      CColor cursorColor = color(cursorIndex % 2u == 0u ? 0xa9d18e : 0x8fb8cf);
      context->setFrameColor(cursorColor);
      DrawPath cursor = DrawPath{};
      cursor.lineWidth = 2.0;
      cursor.move(CPoint(x, top));
      cursor.line(CPoint(x, top + height));
      cursor.stroke(*context);
      const uint8_t key = _instance->voicePlayheadKeys[cursorIndex].load(
          std::memory_order_relaxed);
      std::string flag =
          fmt("%s/%u", shortNoteText(key), static_cast<unsigned>(key));
      TextStyle flagAttrs = slicerTextAttrs(color(0xd0d8d3), 7.5, 0);
      const TextSize flagSize = textSize(flag, flagAttrs);
      const double flagX = std::clamp<double>(
          x + 3.0, rect.left + 2.0, maxX(rect) - flagSize.width - 8.0);
      const CRect flagRect =
          box(flagX, top + 2.0, flagSize.width + 6.0, flagSize.height + 3.0);
      context->setFillColor(color(0x202522));
      fillRect(flagRect);
      context->setFrameColor(cursorColor);
      frameRect(flagRect);
      drawAt(flag, CPoint(flagX + 3.0, flagRect.top + 1.0), flagAttrs);
    }
    std::string details = fmt(
        "%u SLICES   %u CH   %.2f S   INSERTS %s / %s   AUX %.0f%%",
        static_cast<unsigned>(slot.sliceCount),
        static_cast<unsigned>(asset.channelCount),
        static_cast<double>(asset.frameCount()) / asset.sampleRate,
        insertTypeShortText(slot.inserts[0u].type),
        insertTypeShortText(slot.inserts[1u].type), slot.mixerAuxSend * 100.0f);
    drawAt(details, CPoint(rect.left + 12.0, maxY(rect) - 21.0), label);
  }

  void drawMutateForBank(const BankSnapshot &bank) {
    TextStyle label = slicerTextAttrs(color(0xa6aaa7), 10.0);
    TextStyle value = slicerTextAttrs(color(0xc6cbc8), 11.0, 0);
    const uint32_t slotIndex = selectedSlot();
    const auto &slot = bank.slots[slotIndex];
    drawMutateWaveformForSlot(slot);
    for (uint32_t index = 0u; index < s3g::breakbeat::kMaximumSampleSlots;
         ++index) {
      const CRect rect = mutateSlotRect(index);
      const bool active = index == slotIndex;
      const auto &candidate = bank.slots[index];
      const bool pending = _instance->pendingMutationSlots[index];
      context->setFillColor(color(active ? 0x292e2b : 0x171717));
      fillRect(rect);
      context->setFrameColor(color(active ? 0x6b7a70 : 0x444444));
      frameRect(rect, active ? 2.0 : 1.0);
      drawAt(fmt("%u", index + 1u),
             CPoint(minX(rect) + 12.0, minY(rect) + 10.0),
             slicerTextAttrs(color(active ? 0xc9d3cc : 0xa8aaa9), 16.0, 0));
      std::string slotState =
          pending
              ? "BUILDING"
              : candidate.asset ? (active ? "SOURCE" : "LOCKED") : "AVAILABLE";
      drawAt(slotState, CPoint(minX(rect) + 44.0, minY(rect) + 12.0), value);
      std::string filename =
          pending ? "RENDERING"
                  : candidate.asset ? portableSlotFilename(*_instance, index)
                                    : "EMPTY";
      drawCenteredTruncatedFilename(box(minX(rect) + 12.0, minY(rect) + 34.0,
                                        rectWidth(rect) - 24.0, 22.0),
                                    filename, label);
      if (active) {
        drawAt("SELECTED", CPoint(maxX(rect) - 68.0, minY(rect) + 12.0),
               slicerTextAttrs(color(0x9db1a4), 8.5, 0));
      }
    }

    const uint32_t emptyCount = emptyMutationSlotCount(*_instance, slotIndex);
    std::string operationName =
        str(s3g::breakbeat::structuralMutationOperationName(
            _instance->structuralMutationOperation));
    std::string fillTitle =
        emptyCount > 0u ? fmt("FILL %u WITH %s", emptyCount, operationName)
                        : "ALL OTHER SLOTS LOCKED";
    drawButton(mutateActionButtonRect(0u), fillTitle,
               slot.asset && emptyCount > 0u);
    drawButton(mutateActionButtonRect(1u), "PLAY THROUGH",
               _instance->playthroughSlotTelemetry.load(
                   std::memory_order_relaxed) == slotIndex + 1u);
    drawButton(mutateActionButtonRect(2u), "CLEAR",
               slot.asset || _instance->pendingMutationSlots[slotIndex]);
    drawButton(mutateActionButtonRect(3u), "EXPORT…", slot.asset != nullptr);

    const CRect panel = mutateUsesPanelRect();
    Style style;
    drawPanelFrame(minX(panel), minY(panel), rectWidth(panel),
                   rectHeight(panel), style);
    drawPanelHeader("COLLAGE + TREATMENTS", true, minX(panel), minY(panel),
                    rectWidth(panel), 24.0, label, style);
    static const std::vector<std::string> useNames{
        "REARRANGE", "REPEAT", "PITCH", "MIXER FX", "AUX BUS", "REVERSE"};
    static constexpr std::array<uint8_t, 6u> useBits{{
        s3g::breakbeat::StructuralRearrange,
        s3g::breakbeat::StructuralRepeat,
        s3g::breakbeat::StructuralPitch,
        s3g::breakbeat::StructuralMixerFx,
        s3g::breakbeat::StructuralAuxBus,
        s3g::breakbeat::StructuralReverse,
    }};
    for (uint32_t index = 0u; index < useBits.size(); ++index) {
      drawButton(mutateUseRect(index), useNames[index],
                 (_instance->structuralMutationUses & useBits[index]) != 0u);
    }
    drawAt(fmt("NEXT SEED %u", _instance->mutationSeed),
           CPoint(minX(panel) + 18.0, minY(panel) + 68.0), value);
    drawAt("FX + AUX: PER SLICE",
           CPoint(maxX(panel) - 160.0, minY(panel) + 69.0), label);

    const CRect operationPanel = mutateOperationPanelRect();
    drawPanelFrame(minX(operationPanel), minY(operationPanel),
                   rectWidth(operationPanel), rectHeight(operationPanel),
                   style);
    drawPanelHeader("MUTATE OPERATION", true, minX(operationPanel),
                    minY(operationPanel), rectWidth(operationPanel), 24.0,
                    label, style);
    static const std::vector<std::string> operationNames{
        "COLLAGE", "SORTER", "STUTTER", "SHRINK", "DOUBLETS"};
    for (uint32_t index = 0u; index < operationNames.size(); ++index) {
      drawButton(mutateOperationRect(index), operationNames[index],
                 static_cast<uint8_t>(_instance->structuralMutationOperation) ==
                     index);
    }

    std::string alternateTitle = "USES DEFINE COLLAGE";
    std::string operationHelp =
        "REARRANGE + REPEAT SHAPE COLLAGE; OTHER USES TREAT EVERY OPERATION";
    switch (_instance->structuralMutationOperation) {
    case StructuralMutationOperation::Sorter:
      alternateTitle = _instance->structuralMutationAlternate ? "KEY: DURATION"
                                                              : "KEY: LOUDNESS";
      operationHelp = "ORDERS EVERY SLICE FROM LOW TO HIGH BY THE SELECTED KEY";
      break;
    case StructuralMutationOperation::Stutter:
      alternateTitle = _instance->structuralMutationAlternate
                           ? "SOURCE: JOINED GROUPS"
                           : "SOURCE: BEGINNINGS";
      operationHelp = "RETRIGGERS SHORT HEADS OR CONTIGUOUS SLICE GROUPS";
      break;
    case StructuralMutationOperation::Shrink:
      alternateTitle = _instance->structuralMutationAlternate
                           ? "CURVE: EXPONENTIAL"
                           : "CURVE: LINEAR";
      operationHelp =
          "REPEATS ONE SEGMENT WHILE SHORTENING AND ACCELERATING IT";
      break;
    case StructuralMutationOperation::Doublets:
      alternateTitle = _instance->structuralMutationAlternate
                           ? "TIMING: SOURCE SYNC"
                           : "TIMING: EXPAND";
      operationHelp =
          _instance->structuralMutationAlternate
              ? "TWO PITCH-PRESERVED HALF-DURATION COPIES KEEP SOURCE TIME"
              : "TWO FULL-DURATION COPIES DOUBLE THE ARRANGEMENT TIME";
      break;
    case StructuralMutationOperation::Collage:
      break;
    }
    drawButton(mutateOperationAlternateRect(), alternateTitle,
               _instance->structuralMutationOperation ==
                       StructuralMutationOperation::Collage ||
                   _instance->structuralMutationAlternate);
    drawAt(operationHelp, CPoint(552.0, 543.0), label);
  }

  void drawKeyboardForSlot(const s3g::breakbeat::SampleSlot &slot) {
    const CRect keyboard = keyboardRect();
    constexpr uint32_t keyCount = 24u;
    const double width = keyboard.getWidth() / keyCount;
    TextStyle attrs = slicerTextAttrs(color(0xaeb3af), 8.5);
    for (uint32_t index = 0u; index < keyCount; ++index) {
      const uint32_t note = std::min<uint32_t>(
          127u, static_cast<uint32_t>(slot.rootNote) + index);
      const bool black = note % 12u == 1u || note % 12u == 3u ||
                         note % 12u == 6u || note % 12u == 8u ||
                         note % 12u == 10u;
      const bool mapped =
          index < slot.mappedSliceCount && slot.mappedRootNote == slot.rootNote;
      const uint8_t hitChannel =
          _instance->midiKeyChannels[note].load(std::memory_order_relaxed);
      const bool channelReceives = slot.midiChannel == 0u || hitChannel == 0u ||
                                   slot.midiChannel == hitChannel;
      bool voiceActive = false;
      for (std::size_t cursor = 0u; cursor < _instance->voicePlayheads.size();
           ++cursor) {
        if (_instance->voicePlayheadSlots[cursor].load(
                std::memory_order_relaxed) == selectedSlot() &&
            _instance->voicePlayheadKeys[cursor].load(
                std::memory_order_relaxed) == note &&
            _instance->voicePlayheads[cursor].load(std::memory_order_relaxed) >=
                0.0f) {
          voiceActive = true;
          break;
        }
      }
      const float hit =
          channelReceives
              ? _instance->midiKeyActivity[note].load(std::memory_order_relaxed)
              : 0.0f;
      const bool active = mapped && (voiceActive || hit > 0.06f);
      const CRect key = box(keyboard.left + index * width, keyboard.top, width,
                            keyboard.getHeight());
      context->setFillColor(color(
          active ? 0x526b59 : mapped ? 0x26352c : black ? 0x171717 : 0x292929));
      fillRect(key);
      context->setFrameColor(
          color(active ? 0xb6c9bb : mapped ? 0x59705f : 0x4a4a4a));
      frameRect(key, active ? 2.0 : 1.0);
      if (active) {
        context->setFillColor(color(0xb6c9bb));
        fillRect(box(minX(key) + 3.0, minY(key) + 2.0,
                     std::max<double>(2.0, (rectWidth(key) - 6.0) *
                                               std::max(hit, 0.35f)),
                     2.0));
      }
      drawCentered(box(key.left, key.top + 3.0, key.getWidth(), 17.0),
                   shortNoteText(static_cast<uint8_t>(note)), attrs);
      TextStyle numberAttrs = slicerTextAttrs(
          color(active ? 0xdce5df : mapped ? 0x9fb4a5 : 0x858585), 8.0, 0);
      drawCentered(box(key.left, key.top + 23.0, key.getWidth(), 17.0),
                   fmt("%u", note), numberAttrs);
    }
  }

  void drawInsertEditorForBank(const BankSnapshot &bank) {
    if (_insertEditorSlot < 0 ||
        static_cast<std::size_t>(_insertEditorSlot) >= bank.slots.size() ||
        _insertEditorIndex >= s3g::breakbeat::kInsertSlotsPerStrip)
      return;
    const uint32_t slotIndex = static_cast<uint32_t>(_insertEditorSlot);
    const auto &insert = bank.slots[slotIndex].inserts[_insertEditorIndex];
    const CRect panel = mixerAuxBusRect();
    Style style;
    TextStyle label = slicerTextAttrs(color(0x929994), 9.0, 0);
    TextStyle value = slicerTextAttrs(color(0xc2c7c3), 10.0, 0);
    std::string heading =
        fmt("BREAK %02u  //  INSERT %u  //  %s", slotIndex + 1u,
            _insertEditorIndex + 1u, insertTypeText(insert.type));
    drawPanelFrame(minX(panel), minY(panel), rectWidth(panel),
                   rectHeight(panel), style);
    drawPanelHeader(heading, true, minX(panel), minY(panel), rectWidth(panel),
                    24.0, label, style);

    static const std::vector<std::string> deviceLabels{
        "OFF",  "FLTR",  "DGRD", "TRNS", "RSNR",
        "ERSN", "SHIFT", "FOLD", "RPT",  "TIME"};
    for (uint32_t type = 0u; type < deviceLabels.size(); ++type)
      drawHeaderButton(mixerInsertTypeRect(type), panel, deviceLabels[type],
                       static_cast<uint8_t>(insert.type) == type, value, style);
    drawHeaderButton(mixerInsertBypassRect(), panel, "BYPASS", insert.bypassed,
                     value, style);
    drawHeaderButton(mixerInsertSwapRect(), panel, "SWAP I1/I2", false, value,
                     style);
    drawHeaderButton(mixerInsertCloseRect(), panel, "CLOSE", false, value,
                     style);
    std::string option =
        insert.type == InsertType::Filter
            ? fmt("MODE  %s", filterModeText(insert.mode))
            : insert.type == InsertType::Erosion
                  ? fmt("MODE  %s", insert.variant == 0u ? "SINE" : "NOISE")
                  : insert.type == InsertType::Shifter
                        ? fmt("MODE  %s",
                              insert.variant == 0u ? "FREQUENCY" : "RING")
                        : insert.type == InsertType::Wavefolder
                              ? fmt("MODE  %s",
                                    insert.variant == 0u ? "FOLD" : "CLIP")
                              : insert.type == InsertType::Repeater
                                    ? fmt("MODE  %s", insert.variant == 0u
                                                          ? "FORWARD"
                                                          : insert.variant == 1u
                                                                ? "REVERSE"
                                                                : "ALTERNATE")
                                    : insert.type == InsertType::TimeMangler
                                          ? fmt("MODE  %s",
                                                insert.variant == 0u
                                                    ? "REVERSE"
                                                    : insert.variant == 1u
                                                          ? "FREEZE"
                                                          : "TAPE")
                                          : insert.type == InsertType::Off
                                                ? "NO DEVICE ASSIGNED"
                                                : fmt("RESET  %s",
                                                      insertTypeText(
                                                          insert.type));
    drawHeaderButton(mixerInsertOptionRect(), panel, option,
                     insert.type == InsertType::Filter ||
                         insert.type == InsertType::Erosion ||
                         insert.type == InsertType::Shifter ||
                         insert.type == InsertType::Wavefolder ||
                         insert.type == InsertType::Repeater ||
                         insert.type == InsertType::TimeMangler,
                     value, style);

    if (insert.type != InsertType::Off) {
      for (uint32_t parameter = 0u; parameter < insert.values.size();
           ++parameter) {
        drawMixerInsertParameter(insertParameterLabel(insert, parameter),
                                 insertParameterValue(insert, parameter),
                                 insert.values[parameter], parameter, label,
                                 value, style);
      }
    } else {
      drawCentered(box(minX(panel) + 16.0, minY(panel) + 190.0,
                       rectWidth(panel) - 32.0, 24.0),
                   "CHOOSE A POST-PLAYBACK DEVICE ABOVE", label);
    }
    drawAt(insertSafetyText(insert.type),
           CPoint(minX(panel) + 16.0, minY(panel) + 465.0), label);
    drawAt("POST VOICES  →  I1  →  I2  →  EQ / PAN / FADER  →  AUX",
           CPoint(minX(panel) + 16.0, minY(panel) + 487.0), label);
  }

  void drawMixerForBank(const BankSnapshot &bank) {
    constexpr std::array<uint32_t, 4u> colors{{
        0x78918c,
        0x9a826c,
        0x817a99,
        0x956f73,
    }};
    Style style;
    TextStyle label = slicerTextAttrs(color(0x929994), 9.0, 0);
    TextStyle value = slicerTextAttrs(color(0xc2c7c3), 10.0, 0);
    static constexpr const char *dialLabels[]{
        "PAN", "LOW", "MID", "FREQ", "HIGH",
    };
    for (uint32_t index = 0u; index < bank.slots.size(); ++index) {
      const auto &slot = bank.slots[index];
      const CRect strip = mixerStripRect(index);
      const bool selected = index == selectedSlot();
      CColor accent = color(static_cast<int>(colors[index]));
      drawPanelFrame(minX(strip), minY(strip), rectWidth(strip),
                     rectHeight(strip), style);
      drawPanelHeader(fmt("%02u  BREAK", index + 1u), selected, minX(strip),
                      minY(strip), rectWidth(strip), 24.0, label, style);
      context->setFillColor(accent);
      fillRect(box(minX(strip), minY(strip), rectWidth(strip), 3.0));
      drawCenteredTruncatedFilename(box(minX(strip) + 8.0, minY(strip) + 30.0,
                                        rectWidth(strip) - 16.0, 16.0),
                                    portableSlotFilename(*_instance, index),
                                    value);
      std::string midiRoute =
          slot.midiChannel == 0u
              ? "OMNI"
              : fmt("CH%02u", static_cast<unsigned>(slot.midiChannel));
      std::string mapping =
          slot.mappedSliceCount == 0u
              ? "UNMAPPED"
              : fmt("%s+%u", shortNoteText(slot.mappedRootNote),
                    static_cast<unsigned>(slot.mappedSliceCount));
      std::string route = fmt("%s  //  %s", midiRoute, mapping);
      drawCentered(box(minX(strip) + 8.0, minY(strip) + 50.0,
                       rectWidth(strip) - 16.0, 15.0),
                   route, label);

      std::string panText =
          (!slot.asset || slot.asset->channelCount <= 2u)
              ? (std::abs(slot.mixerPan) < 0.01f
                     ? "C"
                     : fmt("%c%02d", slot.mixerPan < 0.0f ? 'L' : 'R',
                           static_cast<int>(
                               std::lround(std::abs(slot.mixerPan) * 100.0f))))
              : "LOCK";
      const std::array<float, 5u> dialValues{{
          slot.mixerPan,
          slot.mixerLowEqDb,
          slot.mixerMidEqDb,
          slot.mixerMidFrequencyHz,
          slot.mixerHighEqDb,
      }};
      const std::array<double, 5u> dialNorms{{
          static_cast<double>((slot.mixerPan + 1.0f) * 0.5f),
          static_cast<double>((slot.mixerLowEqDb + 12.0f) / 24.0f),
          static_cast<double>((slot.mixerMidEqDb + 12.0f) / 24.0f),
          static_cast<double>(std::log(slot.mixerMidFrequencyHz / 120.0f) /
                              std::log(8000.0f / 120.0f)),
          static_cast<double>((slot.mixerHighEqDb + 12.0f) / 24.0f),
      }};
      for (uint32_t dial = 0u; dial < 5u; ++dial) {
        std::string text;
        if (dial == 0u)
          text = panText;
        else if (dial == 3u)
          text = dialValues[dial] >= 1000.0f
                     ? fmt("%.1fk", dialValues[dial] / 1000.0f)
                     : fmt("%.0f", dialValues[dial]);
        else
          text = fmt("%+.1f", dialValues[dial]);
        drawDial(str(dialLabels[dial]), text, dialNorms[dial],
                 mixerDialRect(index, dial), label, value, style);
      }
      for (uint32_t insertIndex = 0u; insertIndex < slot.inserts.size();
           ++insertIndex) {
        const auto &insert = slot.inserts[insertIndex];
        std::string insertLabel =
            insert.bypassed && insert.type != InsertType::Off
                ? fmt("I%u BYP", insertIndex + 1u)
                : fmt("I%u %s", insertIndex + 1u,
                      insertTypeShortText(insert.type));
        const CRect rect = mixerInsertRect(index, insertIndex);
        drawHeaderButton(rect, strip, insertLabel,
                         insert.type != InsertType::Off && !insert.bypassed,
                         value, style);
        if (_insertEditorSlot == static_cast<int64_t>(index) &&
            _insertEditorIndex == insertIndex) {
          context->setFrameColor(accent);
          DrawPath selection = DrawPath::rectangle(inset(rect, -1.0, -1.0));
          selection.lineWidth = 2.0;
          selection.stroke(*context);
        }
      }

      drawMixerStripSlider("AUX", fmt("%.0f%%", slot.mixerAuxSend * 100.0f),
                           slot.mixerAuxSend, mixerAuxY(index), minX(strip),
                           rectWidth(strip), label, value, style);
      drawHeaderButton(mixerMuteRect(index), strip, "MUTE", slot.muted, value,
                       style);
      drawHeaderButton(mixerSoloRect(index), strip, "SOLO", slot.solo, value,
                       style);
      drawHeaderButton(mixerAuditionRect(index), strip, "AUDITION", false,
                       value, style);

      const CRect fader = mixerFaderRect(index);
      drawAt("LVL", CPoint(minX(strip) + 16.0, minY(fader) - 27.0), label);
      const double levelDb = gainDb(slot.mixerGain);
      const double levelNorm = std::clamp<double>(
          static_cast<double>((levelDb + 60.0) / 66.0), 0.0, 1.0);
      drawMixerFader(levelNorm, fader, style);
      drawCentered(box(minX(strip) + 10.0, maxY(fader) + 8.0,
                       rectWidth(strip) - 20.0, 16.0),
                   slot.mixerGain <= 0.001f ? "-INF dB"
                                            : fmt("%+.1f dB", levelDb),
                   value);
      const float peak =
          _instance->slotPeaks[index].load(std::memory_order_relaxed);
      const float peakDb = 20.0f * std::log10(std::max(peak, 0.000001f));
      drawVerticalVuMeter(
          std::clamp<double>((peakDb + 60.0f) / 60.0f, 0.0f, 1.0f),
          mixerPeakRect(index), style);
    }

    const CRect main = mixerMainRect();
    drawPanelFrame(minX(main), minY(main), rectWidth(main), rectHeight(main),
                   style);
    drawPanelHeader("OUTPUT", true, minX(main), minY(main), rectWidth(main),
                    24.0, label, style);
    const double outputDb =
        _instance->outputGainDb.load(std::memory_order_relaxed);
    drawMixerStripSlider("OUT", fmt("%+.1f dB", outputDb),
                         (outputDb + 60.0) / 72.0, mixerOutputSliderY(),
                         minX(main), rectWidth(main), label, value, style);
    drawAt(fmt("FIXED %u-CHANNEL PASS-THROUGH",
               static_cast<unsigned>(_instance->outputChannelCount)),
           CPoint(minX(main) + 16.0, minY(main) + 88.0), value);
    drawHeaderButton(mixerUnityRect(), main, "UNITY", std::abs(outputDb) < 0.01,
                     value, style);

    if (_insertEditorSlot >= 0 &&
        static_cast<std::size_t>(_insertEditorSlot) < bank.slots.size()) {
      drawInsertEditorForBank(bank);
      return;
    }

    const CRect bus = mixerAuxBusRect();
    drawPanelFrame(minX(bus), minY(bus), rectWidth(bus), rectHeight(bus),
                   style);
    drawPanelHeader("s3g BREAK BUS", true, minX(bus), minY(bus), rectWidth(bus),
                    24.0, label, style);
    drawHeaderButton(mixerBusEnableRect(), bus, "AUX BUS", bank.auxEnabled,
                     value, style);
    static constexpr const char *busLabels[]{
        "PRS", "SNP", "RCV", "SAT", "BIT", "CLP", "TLT", "RET",
    };
    const std::array<float, 8u> busValues{{
        bank.auxPress,
        bank.auxSnap,
        bank.auxRecovery,
        bank.auxSaturation,
        bank.auxBite,
        bank.auxClip,
        bank.auxTilt,
        bank.auxReturnDb,
    }};
    const std::array<double, 8u> busNorms{{
        bank.auxPress,
        (bank.auxSnap + 1.0f) * 0.5f,
        bank.auxRecovery,
        bank.auxSaturation,
        bank.auxBite,
        bank.auxClip,
        (bank.auxTilt + 1.0f) * 0.5f,
        (bank.auxReturnDb + 60.0f) / 72.0f,
    }};
    Style bypassStyle = style;
    bypassStyle.strip = color(0x101010);
    bypassStyle.grid = color(0x303030);
    bypassStyle.text = color(0x5d625f);
    bypassStyle.fill = color(0x343735);
    TextStyle bypassLabel = slicerTextAttrs(color(0x626662), 9.0, 0);
    TextStyle bypassValue = slicerTextAttrs(color(0x686c69), 10.0, 0);
    for (uint32_t row = 0u; row < busValues.size(); ++row) {
      const bool bypassed = bank.auxFieldSafe && row >= 3u && row <= 5u;
      std::string text =
          bypassed ? "BYP"
                   : row == 7u ? fmt("%+.1f", busValues[row])
                               : row == 1u || row == 6u
                                     ? fmt("%+.2f", busValues[row])
                                     : fmt("%.0f%%", busValues[row] * 100.0f);
      drawMixerStripSlider(
          str(busLabels[row]), text, busNorms[row], mixerBusSliderY(row),
          minX(bus), rectWidth(bus), bypassed ? bypassLabel : label,
          bypassed ? bypassValue : value, bypassed ? bypassStyle : style);
    }
    const float activity =
        _instance->auxActivity.load(std::memory_order_relaxed);
    const float reduction =
        _instance->auxGainReductionDb.load(std::memory_order_relaxed);
    drawAt(fmt("BUS %.0f%%  //  GR %+.1f dB", std::min(activity, 1.0f) * 100.0f,
               reduction),
           CPoint(minX(bus) + 16.0, minY(bus) + 438.0), value);
    drawAt("POST-FADER WET RETURN  //  NO LANE REORDERING",
           CPoint(minX(bus) + 16.0, minY(bus) + 458.0), label);
    std::string link = bank.auxLinkMode == s3g::BreakBusLinkMode::All
                           ? "LINK ALL"
                           : bank.auxLinkMode == s3g::BreakBusLinkMode::Pair
                                 ? "LINK PAIR"
                                 : "LINK FREE";
    drawHeaderButton(mixerBusLinkRect(), bus, link, true, value, style);
    drawHeaderButton(mixerBusFieldSafeRect(), bus, "FIELD SAFE",
                     bank.auxFieldSafe, value, style);
  }

  void paint() override {

    const Style style = softTextStyle();
    context->setFillColor(style.bg);
    fillRect(getViewSize());
    updateDetailNumericFields();
    foundation::drawPluginTitle(*context,
        fmt("s3g Sample Slicer %u", _instance->outputChannelCount),
        box(18, 10, kGuiWidth - 26, 15), titleFont);
    TextStyle label = softLabelAttrs();
    TextStyle value = softValueAttrs();
    std::string status = str(_instance->status.c_str());

    drawRightStatus(status, kGuiWidth, 22.0, label);
    std::string section = _page == 2u
                              ? "INTERNAL BREAK MIXER"
                              : _page == 3u ? "MUTATE" : "FOUR BREAK BANK";
    drawAt(section, CPoint(18.0, 51.0), label);
    TextStyle compact = textAttrs(color(0xb8b8b8), 8.5);
    const CRect navigationBand = box(282.0, 42.0, 780.0, 24.0);
    drawToolboxHeaderButton(editorTabRect(0u), navigationBand, "OVERVIEW",
                            _page == 0u, label, style);
    drawToolboxHeaderButton(editorTabRect(1u), navigationBand, "BREAK EDIT",
                            _page == 1u, label, style);
    drawToolboxHeaderButton(editorTabRect(2u), navigationBand, "MIXER",
                            _page == 2u, label, style);
    drawToolboxHeaderButton(editorTabRect(3u), navigationBand, "MUTATE",
                            _page == 3u, label, style);
    uint64_t projectAudioBytes = 0u;
    uint64_t sourceFileBytes = 0u;
    uint64_t safetyStateBytes = 0u;
    uint32_t sourceFileCount = 0u;
    if (_instance->controlBank) {
      for (std::size_t index = 0u; index < _instance->controlBank->slots.size();
           ++index) {
        const auto &slot = _instance->controlBank->slots[index];
        if (!slot.asset)
          continue;
        projectAudioBytes += static_cast<uint64_t>(slot.asset->channelCount) *
                             slot.asset->frameCount() * sizeof(float);
        std::error_code sourceError;
        if (generatedSampleLocator(_instance->samplePaths[index]) ||
            !std::filesystem::is_regular_file(
                std::filesystem::u8path(_instance->samplePaths[index]),
                sourceError) ||
            sourceError) {
          safetyStateBytes += static_cast<uint64_t>(slot.asset->channelCount) *
                              slot.asset->frameCount() * sizeof(float);
        }
        sourceFileBytes += _instance->sampleFileBytes[index];
        ++sourceFileCount;
      }
    }
    std::string projectAudioLabel;
    if (_instance->storageMode == s3g::sample_storage::StorageMode::Embed) {
      if (projectAudioBytes > kMaximumEmbeddedAudioBytes) {
        projectAudioLabel = "STORE EMBED · PARTIAL >1 GB";
      } else {
        const auto size =
            s3g::sample_storage::formatByteCount(projectAudioBytes);
        projectAudioLabel = fmt("STORE EMBED · STATE %s", size.c_str());
      }
    } else if (safetyStateBytes != 0u) {
      const auto size = s3g::sample_storage::formatByteCount(safetyStateBytes);
      projectAudioLabel =
          fmt("STORE %s · SAFETY STATE %s",
              s3g::sample_storage::storageModeName(_instance->storageMode),
              size.c_str());
    } else if (_instance->storageMode ==
                   s3g::sample_storage::StorageMode::Project &&
               std::any_of(_instance->projectStoragePending.begin(),
                           _instance->projectStoragePending.end(),
                           [](bool pending) { return pending; })) {
      projectAudioLabel = "STORE PROJECT · COPYING";
    } else {
      const auto size = s3g::sample_storage::formatByteCount(sourceFileBytes);
      projectAudioLabel =
          fmt("STORE %s · %uF · %s",
              s3g::sample_storage::storageModeName(_instance->storageMode),
              sourceFileCount, size.c_str());
    }
    drawToolboxHeaderButton(killAllButtonRect(), navigationBand, "KILL ALL",
                            false, compact, style);
    drawToolboxHeaderButton(
        embedAudioButtonRect(), navigationBand, projectAudioLabel,
        _instance->storageMode != s3g::sample_storage::StorageMode::Link,
        compact, style);

    const auto *bank = displayBank();
    if (!bank)
      return;
    if (_page != 2u) {
      const CRect bankPanel = bankPanelRect();
      drawPanelFrame(minX(bankPanel), minY(bankPanel), rectWidth(bankPanel),
                     rectHeight(bankPanel), style);
      drawPanelHeader(
          "BREAK BANK", true, minX(bankPanel), minY(bankPanel),
          rectWidth(bankPanel),
          static_cast<double>(s3g::gui_layout::kStandardMetrics.headerHeight),
          label, style);
    }
    if (_page != 2u)
      for (uint32_t index = 0u; index < s3g::breakbeat::kMaximumSampleSlots;
           ++index) {
        const CRect row = bankRowRect(index);
        const bool selected = index == selectedSlot();
        context->setFillColor(color(selected ? 0x29282b : 0x191919));
        fillRect(row);
        if (selected) {
          context->setFillColor(color(0x655a68));
          fillRect(box(row.left, row.top, 3.0, row.getHeight()));
        }
        context->setFrameColor(color(0x484848));
        frameRect(row);
        drawAt(fmt("BREAK %u", index + 1u),
               CPoint(row.left + 8.0, row.top + 7.0), value);
        std::string filename = portableSlotFilename(*_instance, index);
        CRect nameRect = box(row.left + 72.0, row.top + 6.0, 156.0, 17.0);
        drawText(filename, nameRect, label);
        const auto &slot = bank->slots[index];
        std::string meta =
            slot.asset
                ? fmt("%u SL  %uCH  START %s/%u  MAX %zu", slot.sliceCount,
                      static_cast<unsigned>(slot.asset->channelCount),
                      shortNoteText(slot.rootNote),
                      static_cast<unsigned>(slot.rootNote),
                      s3g::breakbeat::maximumSlicesForStartNote(slot.rootNote))
                : "";
        drawAt(meta, CPoint(row.left + 8.0, row.top + 33.0), compact);
        std::string channel =
            slot.midiChannel == 0u
                ? "MIDI OMNI"
                : fmt("MIDI CH %u", static_cast<unsigned>(slot.midiChannel));
        drawCompactMenu(slotChannelRect(index), channel, selected, value,
                        style);
        const bool mapped = slotHasCompleteMap(slot);
        std::string mapLabel =
            mapped ? fmt("MAPPED %s +%u", shortNoteText(slot.mappedRootNote),
                         static_cast<unsigned>(slot.mappedSliceCount - 1u))
                   : "AUTO MAP";
        drawHeaderButton(slotAutomapRect(index), row, mapLabel, mapped, compact,
                         style);
      }

    const auto &slot = bank->slots[selectedSlot()];
    if (_page == 2u) {
      drawMixerForBank(*bank);
    } else if (_page == 0u) {
      for (uint32_t index = 0u; index < s3g::breakbeat::kMaximumSampleSlots;
           ++index)
        drawOverviewWaveformForSlot(bank->slots[index], index);
      std::string overviewHelp =
          "CLICK A BREAK TO SELECT   DOUBLE-CLICK TO EDIT   DROP AUDIO ON ANY "
          "BREAK   AUTO MAP AFTER SLICE CHANGES";
      drawAt(overviewHelp, CPoint(292.0, 620.0), label);
    } else if (_page == 3u) {
      drawMutateForBank(*bank);
    } else {
      const std::array<std::pair<CRect, std::string>, 5u> toolboxes{{
          {sampleToolboxRect(), "SAMPLE / SLICE"},
          {sliceMapToolboxRect(), "SLICE / MAP"},
          {playbackToolboxRect(), "MAPPED SLICE PLAYBACK"},
          {selectedSliceToolboxRect(), "SELECTED SLICE"},
          {timingToolboxRect(), "TIMING"},
      }};
      for (const auto &toolbox : toolboxes) {
        drawPanelFrame(minX(toolbox.first), minY(toolbox.first),
                       rectWidth(toolbox.first), rectHeight(toolbox.first),
                       style);
        drawPanelHeader(
            toolbox.second, true, minX(toolbox.first), minY(toolbox.first),
            rectWidth(toolbox.first),
            static_cast<double>(s3g::gui_layout::kStandardMetrics.headerHeight),
            label, style);
      }
      drawToolboxHeaderButton(sampleLoadButtonRect(), sampleToolboxRect(),
                              "LOAD", false, label, style);
      drawToolboxHeaderButton(sampleClearButtonRect(), sampleToolboxRect(),
                              "CLEAR", false, label, style);
      drawToolboxHeaderButton(sliceActionButtonRect(), sliceMapToolboxRect(),
                              "SLICE", false, label, style);
      drawToolboxHeaderButton(automapButtonRect(), sliceMapToolboxRect(),
                              "AUTO MAP", slotHasCompleteMap(slot), label,
                              style);
      drawWaveformForSlot(slot);
      const bool mapped = slotHasCompleteMap(slot);
      static constexpr const char *slicingNames[]{
          "EQUAL 4", "EQUAL 8", "EQUAL 16", "EQUAL 32", "TRANSIENT",
      };
      const int64_t slicingIndex =
          std::clamp<int64_t>(_sliceModeSelection, 0, 4);
      drawProcessorMenu("METHOD", str(slicingNames[slicingIndex]), 448.0,
                        minX(sliceMapToolboxRect()),
                        rectWidth(sliceMapToolboxRect()), label, value, style);
      drawProcessorSlider(
          "PRE-ROLL US", fmt("%u", _instance->transientPreRollMicroseconds),
          static_cast<double>(_instance->transientPreRollMicroseconds) /
              static_cast<double>(kMaximumTransientPreRollMicroseconds),
          detailNumericY(kDetailNumericPreRoll), minX(sliceMapToolboxRect()),
          rectWidth(sliceMapToolboxRect()), label, value, style);
      drawProcessorSlider(
          "MIN SLICE MS",
          fmt("%u", _instance->minimumTransientSliceMilliseconds),
          static_cast<double>(_instance->minimumTransientSliceMilliseconds) /
              static_cast<double>(kMaximumMinimumTransientSliceMilliseconds),
          detailNumericY(kDetailNumericMinimumSlice),
          minX(sliceMapToolboxRect()), rectWidth(sliceMapToolboxRect()), label,
          value, style);
      drawProcessorMenu("START NOTE", noteText(slot.rootNote), 526.0,
                        minX(sliceMapToolboxRect()),
                        rectWidth(sliceMapToolboxRect()), label, value, style);

      drawProcessorMenu("TRIGGER", triggerModeText(slot.triggerMode), 448.0,
                        minX(playbackToolboxRect()),
                        rectWidth(playbackToolboxRect()), label, value, style);
      drawProcessorMenu("RETRIGGER", retriggerModeText(slot.retriggerMode),
                        474.0, minX(playbackToolboxRect()),
                        rectWidth(playbackToolboxRect()), label, value, style);
      drawProcessorMenu("VOICE MODE", voiceModeText(slot.voiceMode), 500.0,
                        minX(playbackToolboxRect()),
                        rectWidth(playbackToolboxRect()), label, value, style);
      drawProcessorMenu("PITCH MODE", pitchModeText(slot.pitchMode), 526.0,
                        minX(playbackToolboxRect()),
                        rectWidth(playbackToolboxRect()), label, value, style);
      drawProcessorMenu("TEMPO SYNC", syncModeText(slot.syncMode), 552.0,
                        minX(playbackToolboxRect()),
                        rectWidth(playbackToolboxRect()), label, value, style);

      drawProcessorSlider(
          "SAMPLE BPM", fmt("%.1f", slot.sourceTempoBpm),
          static_cast<double>((slot.sourceTempoBpm - 20.0f) / 979.0f),
          detailNumericY(kDetailNumericSourceTempo), minX(timingToolboxRect()),
          rectWidth(timingToolboxRect()), label, value, style);
      drawProcessorSlider("XFADE %", fmt("%.1f", slot.loopCrossfade * 100.0f),
                          static_cast<double>(slot.loopCrossfade / 0.5f),
                          detailNumericY(kDetailNumericLoopCrossfade),
                          minX(timingToolboxRect()),
                          rectWidth(timingToolboxRect()), label, value, style);
      drawProcessorSlider("GLIDE MS", fmt("%.1f", slot.glideSeconds * 1000.0f),
                          static_cast<double>(slot.glideSeconds / 2.0f),
                          detailNumericY(kDetailNumericGlide),
                          minX(timingToolboxRect()),
                          rectWidth(timingToolboxRect()), label, value, style);

      if (slot.asset && slot.sliceCount > 0u) {
        const double seconds =
            slot.asset->frameCount() / slot.asset->sampleRate;
        std::string details = fmt(
            "%u CH   %.2f S   %.0f HZ   %u SLICES   START %s   MAX %zu   ZOOM "
            "%.1fX",
            static_cast<unsigned>(slot.asset->channelCount), seconds,
            slot.asset->sampleRate, slot.sliceCount, noteText(slot.rootNote),
            s3g::breakbeat::maximumSlicesForStartNote(slot.rootNote), _zoom);
        drawAt(details, CPoint(294.0, 365.0), value);
        drawAt("DRAG MARKERS / LS / LE   SCROLL ZOOM   SHIFT-SCROLL PAN   "
               "DOUBLE-CLICK ADD",
               CPoint(294.0, 383.0), label);
        const std::size_t selected =
            static_cast<std::size_t>(std::clamp<int64_t>(
                _selectedSlice, 0, static_cast<int64_t>(slot.sliceCount) - 1));
        const auto &slice = slot.slices[selected];
        const CRect envelopePanel = envelopePanelRect();
        drawPanelFrame(minX(envelopePanel), minY(envelopePanel),
                       rectWidth(envelopePanel), rectHeight(envelopePanel),
                       style);
        drawPanelHeader(
            "AMP ENVELOPE", true, minX(envelopePanel), minY(envelopePanel),
            rectWidth(envelopePanel),
            static_cast<double>(s3g::gui_layout::kStandardMetrics.headerHeight),
            label, style);
        drawAt("ALL SLICES  //  A D R SCALE TO LENGTH",
               CPoint(minX(envelopePanel) + 16.0, minY(envelopePanel) + 31.0),
               slicerTextAttrs(color(0x888e8a), 8.0, 0));
        const std::array<float, 4u> envelopeValues{{
            slot.envelope.attackProportion,
            slot.envelope.decayProportion,
            slot.envelope.sustain,
            slot.envelope.releaseProportion,
        }};
        static constexpr const char *envelopeLabels[]{
            "A",
            "D",
            "S",
            "R",
        };
        for (uint32_t index = 0u; index < envelopeValues.size(); ++index) {
          drawMixerStripSlider(str(envelopeLabels[index]),
                               fmt("%.1f%%", envelopeValues[index] * 100.0f),
                               envelopeValues[index], envelopeSliderY(index),
                               minX(envelopePanel), rectWidth(envelopePanel),
                               label, value, style);
        }
        if (mapped) {
          std::string selectedStatus = fmt(
              "%03u / %s / %s", static_cast<unsigned>(selected),
              noteText(static_cast<uint8_t>(slot.mappedRootNote + selected)),
              launchModeText(slice.launchMode));
          drawBoundedRightText(selectedStatus, box(500.0, 594.0, 278.0, 14.0),
                               compact);
          static constexpr const char *propertyNames[]{
              "GAIN",
              "PITCH",
              "PAN",
          };
          const std::array<std::string, 3u> propertyValues{{
              fmt("%.3f", slice.gain),
              fmt("%+.2f", slice.transposeSemitones),
              slot.asset->channelCount > 2u ? "N/A" : fmt("%+.3f", slice.pan),
          }};
          const std::array<double, 3u> propertyNormalized{{
              static_cast<double>(slice.gain / 4.0f),
              static_cast<double>(std::clamp(
                  (slice.transposeSemitones + 24.0f) / 48.0f, 0.0f, 1.0f)),
              static_cast<double>((slice.pan + 1.0f) * 0.5f),
          }};
          for (uint32_t property = 0u; property < 3u; ++property) {
            drawMixerStripSlider(
                str(propertyNames[property]), propertyValues[property],
                propertyNormalized[property], kSlicePropertySliderY,
                slicePropertyGroupX(property), kSlicePropertyGroupWidth,
                compact, compact, style);
          }
          drawAt("LAUNCH", CPoint(296.0, 650.0), label);
          drawCompactMenu(sliceLaunchMenuRect(),
                          launchModeText(slice.launchMode), false, value,
                          style);
          drawHeaderButton(sliceReverseButtonRect(), selectedSliceToolboxRect(),
                           "REVERSE", slice.reverse, compact, style);
          drawAt("CHOKE", CPoint(620.0, 650.0), label);
          drawCompactMenu(
              sliceChokeMenuRect(),
              slice.chokeGroup == 0u
                  ? "OFF"
                  : fmt("GROUP %u", static_cast<unsigned>(slice.chokeGroup)),
              slice.chokeGroup != 0u, value, style);
        } else {
          drawLockedControlBar(box(294.0, 620.0, 486.0, 50.0),
                               "LOCKED — SET START, THEN PRESS AUTO MAP");
        }
        std::string help = mapped
                               ? "SELECT OR AUDITION A MAPPED NOTE"
                               : "WORKFLOW:  1 SLICE + EDIT MARKERS   →   2 "
                                 "SET START + AUTO MAP   →   3 EDIT / AUDITION";
        drawAt(help, CPoint(282.0, 690.0), label);
        drawKeyboardForSlot(slot);
      }
    }
    const float peak = _instance->outputPeak.load(std::memory_order_relaxed);
    std::string outputWidth =
        fmt("OUT %u CH FIXED",
            static_cast<unsigned>(_instance->outputChannelCount));
    std::string output = fmt(
        "%s / %+.1f DB   VEL %.0f%%   PER-BREAK MIDI   %s", outputWidth,
        _instance->outputGainDb.load(std::memory_order_relaxed),
        _instance->velocitySensitivity.load(std::memory_order_relaxed) * 100.0,
        peakDbText(peak));
    drawAt(output, CPoint(292.0, 775.0), label);
    numericFields();
  }

private:
  Plugin *_instance;
  bool paramEditing = false;
  int64_t _selectedSlice = 0, _dragMarker = -1, _dragLoopHandle = -1;
  std::shared_ptr<BankSnapshot> _dragBank;
  double _zoom = 1;
  uint32_t _visibleStart = 0, _page = 0;
  bool _dragViewport = false;
  double _viewportGrabOffset = 0, _horizontalPanRemainder = 0;
  int64_t _sliceModeSelection = 2, _envelopeDragIndex = -1, _mixerDragKind = 0;
  uint32_t _mixerDragSlot = 0;
  CPoint _mixerDragStartPoint;
  double _mixerDragStartValue = 0;
  int64_t _insertEditorSlot = -1;
  uint32_t _insertEditorIndex = 0;
  int64_t _detailMenuKind = kDetailMenuNone, _detailMenuHover = -1;
  uint32_t _detailMenuSlot = 0;
  int64_t _detailNumericDragIndex = kDetailNumericNone;
};
Editor *documentationEditor(const clap_plugin_t *plugin) {
  auto *host = self(plugin)->portableGuiEditor;
  return host ? static_cast<Editor *>(host->contentView()) : nullptr;
}
const s3g::gui_documentation::Extension documentation{
    [](const clap_plugin_t *plugin) {
      auto *v = documentationEditor(plugin);
      return v && v->loadDocumentationBreaks();
    },
    [](const clap_plugin_t *plugin, uint32_t page) {
      auto *v = documentationEditor(plugin);
      if (!v)
        return false;
      v->setDocumentationPage(page);
      return true;
    },
    [](const clap_plugin_t *plugin) {
      auto *v = documentationEditor(plugin);
      return v && v->runDocumentationMutationFill();
    }};
} // namespace slicer_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new slicer_canvas::Editor(p);
}
