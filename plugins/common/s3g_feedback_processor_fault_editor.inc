void markHostStateDirty(FaultReadout&) { markStateDirty(); }
void applyParam(FaultReadout&, clap_id id, double v) {
  // Retain the original non-stepped random-action salts. CLAP's historical
  // STEPPED flag on these momentary actions does not describe their seed data.
  if (id == kRandomizeFieldParamId || id == kRandomizePatchParamId || id == kMutateParamId) {
    if (_plugin->guiParamEvents.available() < 8u) { beep(); return; }
    beginEdit(id);
    _plugin->guiParamEvents.push({s3g::clap_gui::ParamEventKind::Value, id, v});
    endEdit(id); request();
  } else editValue(id, v, _dragSlider > 0);
  refreshFaultReadout(*_plugin, _readout);
}
#include "s3g_feedback_processor_fault_normalized.inc"
void applyCuratedAlgorithm(FaultReadout&, uint32_t algorithm) {
  auto command = std::make_unique<PortableFaultCommand>();
  command->kind = PortableFaultCommand::Algorithm; command->algorithm = algorithm;
  if (!queuePortableFaultCommand(*_plugin, std::move(command))) beep();
}
bool importFaultSource(const std::string& path, bool decodeWave) {
  auto command = std::make_unique<PortableFaultCommand>();
  command->kind = PortableFaultCommand::Import; command->path = path;
  command->interpretation = decodeWave ? SourceInterpretation::Waveform : SourceInterpretation::RawBytes;
  bool loaded = readSource(path, command->interpretation, command->source, error);
  if (!loaded && decodeWave) {
    command->interpretation = SourceInterpretation::RawBytes;
    loaded = readSource(path, command->interpretation, command->source, error);
  }
  if (!loaded) { invalid(); return false; }
  if (!queuePortableFaultCommand(*_plugin, std::move(command))) { beep(); return false; }
  error.clear(); markStateDirty(); invalid(); return true;
}
void chooseRawFile() {
  // Cocoa's file-panel accessory checkbox becomes a custom s3g choice on both
  // platforms; automatic WAVE decoding remains the first/default action.
  openPopup(drawing::makeRect(1110., 49., 300., 18.),
      {"OPEN ANY — DECODE WAVE INTO EIGHT LANES", "OPEN ANY — INTERPRET AS RAW BYTES"}, 0,
      [this](int mode) {
        foundation::FileDialogOptions options;
        options.title = mode == 0 ? "Open Any Source — Decode WAVE" : "Open Any Source — Raw Bytes";
        const auto path = foundation::runFileDialog(getFrame(), options);
        if (!path.empty()) importFaultSource(path, mode == 0);
      });
}
