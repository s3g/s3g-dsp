bool installPresetCandidate(const Plugin &candidate,
                            const EncoderParams &params) {
  return installDescriptor(p, candidate.descriptor, candidate.guiGeometry,
                           candidate.rayJson, candidate.rayName, false, error,
                           &params);
}
#include "s3g_input_encoder_ray_presets.inc"
bool loadRayText(const std::string &data, const std::string &name) {
  if (!canPublishStateEdits())
    return false;
  s3g::AmbiRayDescriptor descriptor;
  GuiSpaceGeometry geometry;
  std::string canonical;
  if (!parseRayData(data, descriptor, geometry, canonical, error))
    return false;
  if (!installDescriptor(p, std::move(descriptor), std::move(geometry),
                         std::move(canonical), name, true, error))
    return false;
  announceStateEdits();
  return true;
}
bool loadRayPath(const std::string &path, const std::string &name) {
  std::string data;
  if (!s3g::portable_gui::encoder_files::readText(path, data)) {
    error = "RAY FILE READ ERROR";
    return false;
  }
  return loadRayText(data, name);
}
void loadRayField() {
  foundation::FileDialogOptions o;
  o.title = "Load Ray Field";
  o.extension = "s3gray";
  o.extensionDescription = "s3g Ray Field";
  const auto path = foundation::runFileDialog(getFrame(), o);
  if (path.empty())
    return;
  loadRayPath(path, foundation::pathToUtf8(
                        foundation::pathFromUtf8(path.c_str()).filename()));
  setStatus(error.empty() ? "READY" : error);
  invalid();
}
void loadAtlasAtIndex(unsigned index) {
  if (index >= kRayAtlas.size())
    return;
  const auto &entry = kRayAtlas[index];
  const auto path = foundation::resourceDirectory() / "Ray Atlas" /
                    (std::string(entry.resource) + ".s3gray");
  loadRayPath(foundation::pathToUtf8(path), entry.title);
  setStatus(error.empty() ? "READY" : error);
  invalid();
}
