// Speaker keeps its existing worker, snapshot guards and indexed mixer
// commands.
void resetDecoder() {
  if (batch({{kLayoutParamId, 7.},
             {kModeParamId, 1.},
             {kOrderParamId, 3.},
             {kWeightingParamId, 1.},
             {kCustomFieldParamId, 0.},
             {kActiveSpeakersParamId, 24.},
             {kWidthParamId, 1.}})) {
    presetName = "INIT";
    markStateDirty();
  }
}
void guiSpeakerGain(Plugin &, uint32_t index, double value) {
  if (index >= s3g::kAmbiSpeakerDecoderMaxSpeakers || !std::isfinite(value))
    return;
  const bool single = !pointerEditing;
  if (!begin(kSpeakerGainParamId) ||
      p.guiParamEvents.available() <= gestures.size())
    return;
  value = std::clamp(value, 0., 2.);
  setSpeakerGainNonRealtime(p, index, value);
  p.guiParamEvents.push({Kind::Value, kSpeakerGainParamId, value});
  request();
  if (single)
    end();
  markStateDirty();
  invalid();
}
void guiSpeakerEnabled(Plugin &, uint32_t index, bool enabled) {
  setSpeakerEnabledNonRealtime(p, index, enabled);
  markStateDirty();
  invalid();
}
void guiSpeakerSolo(Plugin &, uint32_t index, bool solo) {
  setSpeakerSoloNonRealtime(p, index, solo);
  markStateDirty();
  invalid();
}
bool presetFile(const std::string &path, bool save) {
  if (!save &&
      p.guiParamEvents.available() < infos.size() * 3 + gestures.size())
    return false;
  const auto output = value(preservedOutput);
  const bool result = save ? s3g::clap_gui::portable::saveStateFile(
                                 &p.plugin, stateExt, path.c_str())
                           : s3g::clap_gui::portable::loadStateFile(
                                 &p.plugin, stateExt, path.c_str());
  if (!result)
    return false;
  if (!save) {
    applyParam(p, preservedOutput, output);
    for (const auto &i : infos)
      if (i.id != preservedOutput) {
        p.guiParamEvents.push({Kind::GestureBegin, i.id, 0.});
        p.guiParamEvents.push({Kind::Value, i.id, value(i.id)});
        p.guiParamEvents.push({Kind::GestureEnd, i.id, 0.});
      }
    request();
    markStateDirty();
    service();
  }
  presetName =
      foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem());
  return true;
}
void presetDialog(bool save) {
  foundation::FileDialogOptions options;
  options.save = save;
  options.title =
      std::string(save ? "Save " : "Load ") + pluginName + " Preset";
  options.extension = "s3gpreset";
  options.extensionDescription = "s3g Preset";
  options.defaultSaveName = "Preset.s3gpreset";
  options.initialDirectory = foundation::presetDirectory(pluginName);
  const auto path = foundation::runFileDialog(getFrame(), options);
  if (!path.empty())
    error = presetFile(path, save) ? "" : "PRESET FILE ERROR";
}
void drawNumericFields() {
  auto snapshot = acquireDecoderSnapshot(p);
  const auto params =
      visibleDecoderParams(p, snapshot.decoder(), snapshot.runtimeMixer());
  const auto speakers =
      visibleDecoderSpeakers(p, snapshot.decoder(), snapshot.runtimeMixer());
  const clap_id ids[] = {kAzimuthParamId, kElevationParamId, kDistanceParamId};
  const double values[] = {params.selectedAzimuthDeg,
                           params.selectedElevationDeg,
                           params.selectedDistance};
  for (unsigned n = 0; n < 3; ++n) {
    const auto b = rect(812., 390. + 26. * n, 54., 16.);
    fill(b, style.cell);
    text(format(n == 2 ? "%.2f" : "%+.1f", values[n]), b, style.value,
         kCenterText);
    if (!_openMenu)
      number(b, values[n], [this, id = ids[n]](double v) {
        setEditorParam(id, v);
        _hasSpeakerSelection = true;
      });
  }
  if (_rightPage != 1)
    return;
  const auto mixer = drawing::makeRect(34., 76., 564., 506.);
  const unsigned count =
      std::min(params.activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
  for (unsigned slot = 0; slot < 16; ++slot) {
    const unsigned index = unsigned(_mixerPage) * 16 + slot;
    if (index >= count)
      break;
    const CRect b = mixerGainFieldRect_inRect(slot, mixer);
    fill(b, style.cell);
    text(format("%.2f", speakers[index].gain), b, style.value, kCenterText);
    if (!_openMenu)
      number(b, speakers[index].gain, [this, index](double v) {
        setEditorParam(kSelectedSpeakerParamId, index + 1u);
        guiSpeakerGain(p, index, v);
        _hasSpeakerSelection = true;
      });
  }
}
