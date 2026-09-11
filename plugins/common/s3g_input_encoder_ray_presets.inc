bool canPublishStateEdits() {
  end();
  return p.guiParamEvents.available() >= infos.size() * 3;
}
void announceStateEdits() {
  for (const auto &i : infos) {
    p.guiParamEvents.push({Kind::GestureBegin, i.id, 0.});
    p.guiParamEvents.push({Kind::Value, i.id, value(i.id)});
    p.guiParamEvents.push({Kind::GestureEnd, i.id, 0.});
  }
  request();
  markStateDirty();
  service();
  invalid();
}
bool presetFile(const std::string &path, bool save) {
  if (save)
    return s3g::clap_gui::portable::saveStateFile(&p.plugin, stateExt,
                                                  path.c_str());
  if (!canPublishStateEdits())
    return false;
  auto candidate = std::make_unique<Plugin>();
  routingInitialize(*candidate);
  if (!s3g::clap_gui::portable::loadStateFile(&candidate->plugin, stateExt,
                                              path.c_str()))
    return false;
  auto params = snapshotEncoderParams(*candidate);
  params.outputGainDb = value(preservedOutput);
  if (!installPresetCandidate(*candidate, params))
    return false;
  presetName =
      foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem());
  announceStateEdits();
  return true;
}
void presetDialog(bool save) {
  foundation::FileDialogOptions o;
  o.save = save;
  o.extension = "s3gpreset";
  o.extensionDescription = "s3g Preset";
  o.title = std::string(save ? "Save " : "Load ") + pluginName + " Preset";
  o.defaultSaveName = "Preset.s3gpreset";
  o.initialDirectory = foundation::presetDirectory(pluginName);
  const auto path = foundation::runFileDialog(getFrame(), o);
  if (path.empty())
    return;
  const bool ok = presetFile(path, save);
  error = ok ? "" : "PRESET FILE ERROR";
  if (ok)
    presetName =
        foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem());
}
