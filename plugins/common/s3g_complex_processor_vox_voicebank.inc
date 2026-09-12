std::string readVoiceText(const std::filesystem::path &file) {
  std::ifstream input(file, std::ios::binary | std::ios::ate);
  if (!input)
    return {};
  const auto length = input.tellg();
  if (length <= 0 || length > 64 * 1024 * 1024)
    return {};
  std::string bytes(static_cast<size_t>(length), '\0');
  input.seekg(0);
  if (!input.read(bytes.data(), length))
    return {};
#if defined(_WIN32)
  for (UINT encoding : std::array<UINT, 3>{CP_UTF8, 932u, 28591u}) {
    const DWORD flags = encoding == 28591u ? 0 : MB_ERR_INVALID_CHARS;
    const int count =
        MultiByteToWideChar(encoding, flags, bytes.data(),
                            static_cast<int>(bytes.size()), nullptr, 0);
    if (count <= 0)
      continue;
    std::wstring wide(static_cast<size_t>(count), L'\0');
    if (MultiByteToWideChar(encoding, flags, bytes.data(),
                            static_cast<int>(bytes.size()), wide.data(), count))
      return foundation::pathToUtf8(std::filesystem::path(wide));
  }
#elif defined(__APPLE__)
  for (CFStringEncoding encoding : std::array<CFStringEncoding, 3>{
           kCFStringEncodingUTF8, kCFStringEncodingShiftJIS,
           kCFStringEncodingISOLatin1}) {
    CFStringRef string = CFStringCreateWithBytes(
        kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(bytes.data()),
        bytes.size(), encoding, false);
    if (!string)
      continue;
    std::string text(static_cast<size_t>(CFStringGetMaximumSizeForEncoding(
                         CFStringGetLength(string), kCFStringEncodingUTF8)) +
                         1,
                     '\0');
    const bool ok = CFStringGetCString(string, text.data(), text.size(),
                                       kCFStringEncodingUTF8);
    CFRelease(string);
    if (ok) {
      text.resize(std::strlen(text.c_str()));
      return text;
    }
  }
#endif
  return {};
}
std::vector<std::string> splitVoiceText(const std::string &text,
                                        const std::string &separators) {
  std::vector<std::string> result;
  size_t first = 0;
  for (size_t i = 0; i <= text.size(); ++i)
    if (i == text.size() || separators.find(text[i]) != std::string::npos) {
      result.push_back(text.substr(first, i - first));
      first = i + 1;
    }
  return result;
}
std::shared_ptr<const VoxVoicebank>
loadVoicebankFolder(const std::filesystem::path &folder) {
  std::error_code ec;
  if (!std::filesystem::is_directory(folder, ec))
    return {};
  auto bank = std::make_shared<VoxVoicebank>();
  bank->name = foundation::pathToUtf8(folder.filename());
  for (const auto &raw :
       splitVoiceText(readVoiceText(folder / "character.txt"), "\r\n")) {
    auto line = trimVoxLyricLine(raw);
    const auto eq = line.find('=');
    if (eq == std::string::npos)
      continue;
    auto key = voxNormalizeToken(trimVoxLyricLine(line.substr(0, eq)));
    if (key == "name") {
      auto value = trimVoxLyricLine(line.substr(eq + 1));
      if (!value.empty())
        bank->name = value;
      break;
    }
  }
  for (const auto &raw : splitVoiceText(
           readVoiceText(folder / "s3g-pronunciations.txt"), "\r\n")) {
    auto line = trimVoxLyricLine(raw);
    if (line.empty() || line[0] == '#')
      continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos || !eq)
      continue;
    const auto word = voxNormalizeToken(line.substr(0, eq));
    std::vector<std::string> aliases;
    for (const auto &text : splitVoiceText(line.substr(eq + 1), " ,;|-\t")) {
      auto alias = voxNormalizeToken(text);
      if (!alias.empty())
        aliases.push_back(alias);
    }
    if (!word.empty() && !aliases.empty())
      bank->pronunciations[word] = std::move(aliases);
  }
  std::vector<std::filesystem::path> otos;
  const auto root = folder / "oto.ini";
  if (std::filesystem::exists(root, ec))
    otos.push_back(root);
  std::filesystem::recursive_directory_iterator it(
      folder, std::filesystem::directory_options::skip_permission_denied, ec),
      end;
  std::vector<std::filesystem::path> nested;
  while (!ec && it != end) {
    const auto path = it->path();
    const auto name = foundation::pathToUtf8(path.filename());
    if (!name.empty() && name[0] == '.') {
      if (it->is_directory(ec))
        it.disable_recursion_pending();
    } else {
      auto lower = name;
      std::transform(
          lower.begin(), lower.end(), lower.begin(),
          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (lower == "oto.ini" && path != root)
        nested.push_back(path);
    }
    it.increment(ec);
  }
  std::sort(nested.begin(), nested.end());
  otos.insert(otos.end(), nested.begin(), nested.end());
  std::unordered_map<std::string, std::shared_ptr<const VoxVoicebankAudio>>
      audioByFile;
  for (const auto &oto : otos)
    for (const auto &raw : splitVoiceText(readVoiceText(oto), "\r\n")) {
      const auto line = trimVoxLyricLine(raw);
      if (line.empty() || line[0] == '#')
        continue;
      const auto eq = line.find('=');
      if (eq == std::string::npos || !eq)
        continue;
      const auto filename = trimVoxLyricLine(line.substr(0, eq));
      const auto fields = splitVoiceText(line.substr(eq + 1), ",");
      if (fields.size() < 6)
        continue;
      const auto alias = trimVoxLyricLine(fields[0]);
      // UTAU paths use backslashes even in banks copied to a Mac.
      auto portableName = filename;
      std::replace(portableName.begin(), portableName.end(), '\\', '/');
      const auto wav =
          oto.parent_path() / foundation::pathFromUtf8(portableName.c_str());
      const auto audioKey = foundation::pathToUtf8(wav);
      std::shared_ptr<const VoxVoicebankAudio> audio;
      if (auto found = audioByFile.find(audioKey); found != audioByFile.end())
        audio = found->second;
#if defined(S3G_HAS_WORLD)
      if (!audio)
        audio = readVoxWorldCache(wav);
#endif
      if (!audio) {
        int sampleRate = 0;
        auto wave = readWavMono(wav, &sampleRate);
        if (wave.empty() || sampleRate <= 0)
          continue;
        auto prepared = std::make_shared<VoxVoicebankAudio>();
        prepared->sampleRate = sampleRate;
#if defined(S3G_HAS_WORLD)
        std::array<std::vector<float>, kVoxWorldPitchAnchorCount - 1u> variants;
        auto world = makeWorldSampleFromWave(
            wave, sampleRate, alias.empty() ? audioKey : alias, &variants);
        if (world && !world->samples.empty()) {
          prepared->samples = std::move(world->samples);
          prepared->worldPitchVariants = std::move(variants);
          prepared->worldF0 = std::move(world->f0);
          prepared->worldFrameEnergy = std::move(world->frameEnergy);
          prepared->worldParameters = std::move(world->parameters);
          prepared->worldFramePeriodMs = world->framePeriodMs;
          prepared->baseMidi = world->baseMidi;
          prepared->worldResynthesized = true;
          writeVoxWorldCache(wav, *prepared);
        }
#endif
        if (prepared->samples.empty())
          prepared->samples = std::move(wave);
        audio = std::move(prepared);
      }
      if (!audio || audio->samples.empty() || audio->sampleRate <= 0)
        continue;
      audioByFile[audioKey] = audio;
      const auto sampleRate = audio->sampleRate;
      auto number = [](const std::string &text) {
        const auto n = std::strtod(text.c_str(), nullptr);
        return std::isfinite(n) ? n : 0.;
      };
      auto msToSample = [&](const std::string &text) {
        return std::max(0., number(text) * sampleRate / 1000.);
      };
      VoxVoicebankEntry entry{};
      entry.alias = alias;
      entry.fileKey = voxNormalizeToken(foundation::pathToUtf8(
          foundation::pathFromUtf8(portableName.c_str()).stem()));
      entry.searchKey =
          !entry.fileKey.empty() ? entry.fileKey : voxNormalizeToken(alias);
      entry.audio = std::move(audio);
      const double sampleLength = entry.audio->samples.size();
      const bool hasOtoTiming = std::fabs(number(fields[1])) > .0001 ||
                                std::fabs(number(fields[2])) > .0001 ||
                                std::fabs(number(fields[3])) > .0001 ||
                                std::fabs(number(fields[4])) > .0001 ||
                                std::fabs(number(fields[5])) > .0001;
      const auto offset = msToSample(fields[1]), fixed = msToSample(fields[2]),
                 cutoffMs = number(fields[3]), preutter = msToSample(fields[4]),
                 overlap = msToSample(fields[5]);
      entry.startSample = std::clamp(offset, 0., sampleLength - 1.);
      entry.fixedSample = std::clamp(entry.startSample + fixed,
                                     entry.startSample, sampleLength);
      entry.preutterSample = std::clamp(entry.startSample + preutter,
                                        entry.startSample, sampleLength);
      entry.overlapSample = std::clamp(entry.startSample + overlap,
                                       entry.startSample, sampleLength);
      const auto cutoffSamples = std::fabs(cutoffMs) * sampleRate / 1000.;
      entry.endSample =
          std::clamp(cutoffMs < 0 ? entry.startSample + cutoffSamples
                                  : sampleLength - cutoffSamples,
                     entry.startSample + 1., sampleLength);
      if (entry.endSample <= entry.startSample + 8. &&
          sampleLength > entry.startSample + 8.)
        entry.endSample = sampleLength;
      const double usableSamples =
          std::max(1., entry.endSample - entry.startSample);
      if (!hasOtoTiming && usableSamples > 16.) {
        const auto &key =
            !entry.fileKey.empty() ? entry.fileKey : entry.searchKey;
        const bool vowel = key == "a" || key == "e" || key == "i" ||
                           key == "o" || key == "u" || key == "n";
        const auto onset = std::clamp((vowel ? 32. : 78.) * sampleRate * .001,
                                      usableSamples * .10, usableSamples * .42);
        entry.fixedSample = entry.startSample + onset;
        entry.preutterSample = entry.startSample + onset;
        entry.overlapSample = entry.startSample + onset * .35;
      }
      entry.loopStart = std::clamp(entry.fixedSample, entry.startSample,
                                   entry.endSample - 1.);
      entry.loopEnd = hasOtoTiming ? entry.endSample
                                   : entry.startSample + usableSamples * .90;
      if (entry.loopEnd <= entry.loopStart + 8. ||
          entry.loopEnd > sampleLength) {
        entry.loopStart = s3g::lerp(entry.startSample, entry.endSample, .45);
        entry.loopEnd = s3g::lerp(entry.startSample, entry.endSample, .88);
      }
      bank->entries.push_back(std::move(entry));
    }
  return bank->entries.empty() ? nullptr : bank;
}
