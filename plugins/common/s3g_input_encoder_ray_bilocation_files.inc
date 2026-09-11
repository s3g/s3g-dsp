bool installPresetCandidate(const Plugin &candidate,
                            const EncoderParams &params) {
  return installFields(p, candidate.fieldA, candidate.fieldB,
                       candidate.pairName, candidate.selectedPair, error,
                       &params);
}
#include "s3g_input_encoder_ray_presets.inc"
bool readAtlasResource_name_field_error(const char *resource, const char *name,
                                        FieldState &field,
                                        std::string &failure) {
  const auto path = foundation::resourceDirectory() / "Ray Atlas" /
                    (std::string(resource) + ".s3gray");
  std::string data;
  if (!s3g::portable_gui::encoder_files::readText(foundation::pathToUtf8(path),
                                                  data)) {
    failure = "ATLAS RESOURCE NOT FOUND";
    return false;
  }
  if (!s3g::portable_gui::ray_field_files::parse(
          data, field.descriptor, field.visual, field.json, failure))
    return false;
  field.name = name;
  return true;
}
void loadPairAtIndex(unsigned index) {
  if (index >= kPairPresets.size() || !canPublishStateEdits())
    return;
  const auto &preset = kPairPresets[index];
  FieldState fieldA, fieldB;
  if (!readAtlasResource_name_field_error(preset.resourceA, preset.nameA,
                                          fieldA, error) ||
      !readAtlasResource_name_field_error(preset.resourceB, preset.nameB,
                                          fieldB, error)) {
    setStatus(error);
    invalid();
    return;
  }
  auto params = snapshotEncoderParams(p);
  params.permeability = preset.permeability;
  params.memorySeconds = preset.memory;
  params.separationDeg = preset.separation;
  params.mapMode = preset.mapMode;
  params.sizeA = preset.sizeA;
  params.sizeB = preset.sizeB;
  params.scatterA = preset.scatterA;
  params.scatterB = preset.scatterB;
  params.widthA = preset.widthA;
  params.widthB = preset.widthB;
  params.airA = preset.airA;
  params.airB = preset.airB;

  params = s3g::sanitizeAmbiRayBilocationParams(params);
  if (installFields(p, std::move(fieldA), std::move(fieldB), preset.title,
                    int(index), error, &params))
    announceStateEdits();
  setStatus(error.empty() ? "READY" : error);
  invalid();
}
bool loadFieldText(const std::string &data, const std::string &name,
                   int fieldIndex) {
  if (!canPublishStateEdits())
    return false;
  FieldState replacement;
  if (!s3g::portable_gui::ray_field_files::parse(data, replacement.descriptor,
                                                 replacement.visual,
                                                 replacement.json, error))
    return false;
  replacement.name = name;
  auto fieldA = p.fieldA, fieldB = p.fieldB;
  if (fieldIndex == 1)
    fieldA = std::move(replacement);
  else
    fieldB = std::move(replacement);
  if (!installFields(p, std::move(fieldA), std::move(fieldB), "CUSTOM PAIR", -1,
                     error))
    return false;
  announceStateEdits();
  return true;
}
void loadField(int fieldIndex) {
  foundation::FileDialogOptions o;
  o.title = fieldIndex == 1 ? "Load Ray Field A" : "Load Ray Field B";
  o.extension = "s3gray";
  o.extensionDescription = "s3g Ray Field";
  const auto path = foundation::runFileDialog(getFrame(), o);
  if (path.empty())
    return;
  std::string data;
  if (!s3g::portable_gui::encoder_files::readText(path, data))
    error = "RAY FILE READ ERROR";
  else
    loadFieldText(data,
                  foundation::pathToUtf8(
                      foundation::pathFromUtf8(path.c_str()).filename()),
                  fieldIndex);
  setStatus(error.empty() ? "READY" : error);
  invalid();
}
