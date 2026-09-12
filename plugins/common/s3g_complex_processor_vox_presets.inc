bool presetFile(const std::string &path, bool save) {
  namespace files = s3g::portable_gui::encoder_files;
  if (save) {
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> w(buffer);
    auto number = [&](const char *key, double value) {
      w.Key(key);
      w.Double(value);
    };
    auto string = [&](const char *key, const std::string &text) {
      w.Key(key);
      w.String(text.data(), static_cast<rapidjson::SizeType>(text.size()));
    };
    w.StartObject();
    string("format", "s3g-ambi-vox-preset");
    number("version", 1);
    string("plugin", "org.s3g.s3g-dsp.ambi-vox-encoder-64");
    string("name", foundation::pathToUtf8(
                       foundation::pathFromUtf8(path.c_str()).stem()));
    string("audio_source", "external-not-embedded");
    string("phrase", loadVoxPhrase(*_plugin));
    string("lyrics", loadVoxLyrics(*_plugin));
    w.Key("parameters");
    w.StartArray();
    for (const auto &def : kParams) {
      if (isLegacyHiddenParam(def.id))
        continue;
      double value = 0.;
      if (!paramsGetValue(&_plugin->plugin, def.id, &value))
        continue;
      w.StartObject();
      number("id", def.id);
      string("name", def.name);
      number("value", value);
      w.EndObject();
    }
    w.EndArray();
    const auto score = loadScore(*_plugin);
    w.Key("score");
    w.StartObject();
    number("sustain_start", score.sustainStart);
    number("sustain_end", score.sustainEnd);
    w.Key("nodes");
    w.StartArray();
    for (uint32_t n = 0; n < score.nodeCount; ++n) {
      w.StartObject();
      number("time", score.nodes[n].time);
      number("u", score.nodes[n].u);
      number("v", score.nodes[n].v);
      number("curve", static_cast<uint32_t>(score.nodes[n].curve));
      w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    w.Key("view");
    w.StartObject();
    number("mode", _viewMode);
    number("azimuth_degrees", _viewAzDeg);
    number("elevation_degrees", _viewElDeg);
    number("zoom", _viewZoom);
    w.EndObject();
    const auto &g = _plugin->generator;
    w.Key("generator");
    w.StartObject();
    number("form", static_cast<uint32_t>(g.form));
    number("color", static_cast<uint32_t>(g.color));
    number("groups", g.groups);
    number("cues_per_group", g.cuesPerGroup);
    number("syllables", g.syllables);
    number("seed", g.seed);
    number("generation", g.generation);
    number("variation", g.variation);
    number("hardness", g.hardness);
    number("rest", g.rest);
    w.EndObject();
    w.EndObject();
    return files::writeText(path,
                            std::string(buffer.GetString(), buffer.GetSize()));
  }
  std::string text;
  rapidjson::Document root;
  if (!files::readText(path, text) || !files::parse(text, root) ||
      files::string(root, "format") != "s3g-ambi-vox-preset" ||
      files::number(root, "version", 0) != 1)
    return false;
  const auto &params = files::member(root, "parameters");
  if (!params.IsArray())
    return false;
  auto candidate = makeVoxDefaultSnapshot();
  auto generator = _plugin->generator;
  std::array<bool, kPopFilterParamId + 1> ids{};
  uint32_t loaded = 0;
  for (const auto &param : params.GetArray()) {
    const auto &idValue = files::member(param, "id");
    const auto &val = files::member(param, "value");
    if (!idValue.IsNumber() || !val.IsNumber())
      continue;
    const auto idNumber = files::number(idValue, -1);
    if (idNumber < 0 || idNumber >= ids.size() ||
        idNumber != std::floor(idNumber))
      continue;
    const auto id = static_cast<clap_id>(idNumber);
    if (ids[id] ||
        !assignVoxPresetParameter(candidate, id, files::number(val, 0)))
      continue;
    ids[id] = true;
    ++loaded;
  }
  if (loaded < std::min(24u, publicPresetParameterCount()))
    return false;
  const auto &g = files::member(root, "generator");
  if (g.IsObject()) {
    auto u = [&](const char *key, uint32_t fallback) {
      return static_cast<uint32_t>(
          std::clamp(files::number(g, key, fallback), 0., double(UINT32_MAX)));
    };
    generator.form = static_cast<VoxPhraseForm>(
        u("form", static_cast<uint32_t>(generator.form)));
    generator.color = static_cast<VoxPhraseColor>(
        u("color", static_cast<uint32_t>(generator.color)));
    generator.groups = u("groups", generator.groups);
    generator.cuesPerGroup = u("cues_per_group", generator.cuesPerGroup);
    generator.syllables = u("syllables", generator.syllables);
    generator.seed = u("seed", generator.seed);
    generator.generation = u("generation", generator.generation);
    generator.variation = files::number(g, "variation", generator.variation);
    generator.hardness = files::number(g, "hardness", generator.hardness);
    generator.rest = files::number(g, "rest", generator.rest);
    clampVoxPhraseGeneratorRecipe(generator);
  }
  const auto &score = files::member(root, "score");
  const auto &nodes = files::member(score, "nodes");
  if (nodes.IsArray() && nodes.Size() >= 2u) {
    candidate.score = s3g::ambiVotDefaultScore();
    candidate.score.nodeCount =
        std::min(nodes.Size(), s3g::kAmbiVotMaxScoreNodes);
    for (uint32_t n = 0; n < candidate.score.nodeCount; ++n) {
      auto &node = candidate.score.nodes[n];
      node.time = files::number(nodes[n], "time", node.time);
      node.u = files::number(nodes[n], "u", node.u);
      node.v = files::number(nodes[n], "v", node.v);
      node.curve =
          static_cast<s3g::AmbiVotScoreCurve>(static_cast<uint32_t>(std::clamp(
              files::number(nodes[n], "curve", double(node.curve)), 0., 3.)));
    }
    candidate.score.sustainStart = static_cast<uint32_t>(std::clamp(
        files::number(score, "sustain_start", candidate.score.sustainStart), 0.,
        double(UINT32_MAX)));
    candidate.score.sustainEnd = static_cast<uint32_t>(std::clamp(
        files::number(score, "sustain_end", candidate.score.sustainEnd), 0.,
        double(UINT32_MAX)));
    s3g::ambiVotNormalizeScore(candidate.score);
  }
  candidate.params.outputGainDb = view.params.outputGainDb;
  if (!queueVoxPreset(*_plugin, candidate))
    return false;
  _plugin->generator = generator;
  std::string lyrics = files::string(root, "lyrics");
  if (lyrics.empty())
    lyrics = files::string(root, "phrase");
  if (files::member(root, "lyrics").IsString() ||
      files::member(root, "phrase").IsString()) {
    storeVoxLyricsText(*_plugin, lyrics.c_str());
    rebuildVoxLyricScore(*_plugin);
    _selectedLyricCue = 0;
    requestVoxLyricCue(*_plugin, 0);
    const auto first = voxLyricCueText(*_plugin, 0);
    storeVoxPhrase(*_plugin, first.c_str());
    setPhraseText(first);
    setLyricsText(lyrics);
  }
  const auto &camera = files::member(root, "view");
  if (camera.IsObject()) {
    _viewMode = static_cast<int>(
        std::clamp(files::number(camera, "mode", _viewMode), -1., 2.));
    _viewAzDeg = std::clamp(
        files::number(camera, "azimuth_degrees", _viewAzDeg), -180., 180.);
    _viewElDeg = std::clamp(
        files::number(camera, "elevation_degrees", _viewElDeg), -85., 85.);
    _viewZoom = std::clamp(files::number(camera, "zoom", _viewZoom), .55, 2.4);
    storeViewState();
  }
  auto name = files::string(root, "name");
  if (name.empty())
    name =
        foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem());
  name = s3g::portable_gui::canvas::upper(name);
  _plugin->factoryPresetIndex = -1;
  std::snprintf(_plugin->presetName, sizeof(_plugin->presetName), "%s",
                name.c_str());
  _selectedVoice =
      std::min(_selectedVoice, std::max(1u, candidate.params.voices) - 1);
  syncVisible();
  markStateDirty();
  return true;
}
void presetDialog(bool save) {
  finishGestures();
  foundation::FileDialogOptions options;
  options.save = save;
  options.title = save ? "Save Vox preset" : "Load Vox preset";
  options.extension = "s3gvox";
  options.extensions = save ? std::vector<std::string>{}
                            : std::vector<std::string>{"s3gvox", "json"};
  options.extensionDescription = "s3g Vox preset";
  options.initialDirectory = foundation::presetDirectory("Ambi Vox");
  options.defaultSaveName = "s3g-vox-preset.s3gvox";
  const auto path = foundation::runFileDialog(getFrame(), options);
  if (path.empty())
    return;
  if (!presetFile(path, save)) {
    error = "VOX PRESET COULD NOT BE READ / WRITTEN";
    invalid();
    return;
  }
  if (save) {
    const auto name = s3g::portable_gui::canvas::upper(
        foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem()));
    _plugin->factoryPresetIndex = -1;
    std::snprintf(_plugin->presetName, sizeof(_plugin->presetName), "%s",
                  name.c_str());
  }
  error.clear();
  markStateDirty();
  invalid();
}
void loadUserPreset() { presetDialog(false); }
void saveUserPreset() { presetDialog(true); }
