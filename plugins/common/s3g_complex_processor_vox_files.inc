static std::vector<float> readWavMono(const std::filesystem::path &url,
                                      int *sampleRateOut = nullptr) {
  std::vector<float> result;
  if (sampleRateOut)
    *sampleRateOut = 0;
  std::ifstream input(url, std::ios::binary | std::ios::ate);
  if (!input)
    return result;
  const auto length = input.tellg();
  if (length < 44 || length > 320000000)
    return result;
  std::vector<uint8_t> data(static_cast<size_t>(length));
  input.seekg(0);
  if (!input.read(reinterpret_cast<char *>(data.data()), length))
    return result;
  const uint8_t *bytes = data.data();
  const size_t size = data.size();

  const auto u16 = [&](size_t offset) -> uint16_t {
    return offset + 1 < size
               ? static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8))
               : 0u;
  };
  const auto u32 = [&](size_t offset) -> uint32_t {
    return offset + 3 < size ? static_cast<uint32_t>(bytes[offset] |
                                                     (bytes[offset + 1] << 8) |
                                                     (bytes[offset + 2] << 16) |
                                                     (bytes[offset + 3] << 24))
                             : 0u;
  };
  if (std::memcmp(bytes, "RIFF", 4) != 0 ||
      std::memcmp(bytes + 8, "WAVE", 4) != 0)
    return result;
  uint16_t audioFormat = 0;
  uint16_t channels = 0;
  uint16_t bits = 0;
  uint32_t sampleRate = 0;
  size_t dataOffset = 0;
  uint32_t dataSize = 0;
  for (size_t offset = 12; offset + 8 <= size;) {
    const uint32_t chunkSize = u32(offset + 4);
    if (offset + 8 + chunkSize > size)
      break;
    if (std::memcmp(bytes + offset, "fmt ", 4) == 0 && chunkSize >= 16) {
      audioFormat = u16(offset + 8);
      channels = u16(offset + 10);
      sampleRate = u32(offset + 12);
      bits = u16(offset + 22);
    } else if (std::memcmp(bytes + offset, "data", 4) == 0) {
      dataOffset = offset + 8;
      dataSize = chunkSize;
    }
    offset += 8 + chunkSize + (chunkSize & 1u);
  }
  if (dataOffset == 0 || channels == 0)
    return result;
  if (sampleRateOut)
    *sampleRateOut = static_cast<int>(sampleRate);
  const uint32_t bytesPerSample = bits / 8u;
  if (bytesPerSample == 0)
    return result;
  const uint32_t frameBytes = bytesPerSample * channels;
  const uint32_t frames = dataSize / std::max<uint32_t>(1u, frameBytes);
  result.reserve(frames);
  for (uint32_t frame = 0; frame < frames; ++frame) {
    const size_t offset = dataOffset + static_cast<size_t>(frame) * frameBytes;
    float sum = 0.0f;
    for (uint32_t channel = 0; channel < channels; ++channel) {
      const size_t p = offset + channel * bytesPerSample;
      float value = 0.0f;
      if (audioFormat == 3 && bits == 32 && p + 3 < size) {
        std::memcpy(&value, bytes + p, sizeof(float));
      } else if (audioFormat == 1 && bits == 16 && p + 1 < size) {
        const int16_t sample =
            static_cast<int16_t>(bytes[p] | (bytes[p + 1] << 8));
        value = static_cast<float>(sample) / 32768.0f;
      } else if (audioFormat == 1 && bits == 24 && p + 2 < size) {
        int32_t sample = static_cast<int32_t>(bytes[p] | (bytes[p + 1] << 8) |
                                              (bytes[p + 2] << 16));
        if ((sample & 0x800000) != 0)
          sample |= ~0xffffff;
        value = static_cast<float>(sample) / 8388608.0f;
      } else if (audioFormat == 1 && bits == 32 && p + 3 < size) {
        int32_t sample = 0;
        std::memcpy(&sample, bytes + p, sizeof(int32_t));
        value = static_cast<float>(sample) / 2147483648.0f;
      }
      sum += value;
    }
    result.push_back(sum / static_cast<float>(channels));
  }
  return result;
}

#if defined(S3G_HAS_WORLD)
static VoxWorldParameterData makeWorldParameterData(
    const std::vector<std::vector<double>> &spectrogramStorage,
    const std::vector<std::vector<double>> &aperiodicityStorage, int sampleRate,
    int fftSize, double framePeriodMs) {
  VoxWorldParameterData result{};
  if (spectrogramStorage.empty() ||
      aperiodicityStorage.size() != spectrogramStorage.size() ||
      sampleRate <= 0 || fftSize <= 0)
    return result;
  result.sampleRate = sampleRate;
  result.fftSize = fftSize;
  result.framePeriodMs = framePeriodMs;
  result.frameCount = static_cast<uint32_t>(spectrogramStorage.size());
  result.spectralDimensions = std::min<uint32_t>(
      kVoxWorldSpectralDimensions, static_cast<uint32_t>(fftSize / 2));
  result.aperiodicityDimensions =
      static_cast<uint32_t>(std::max(0, GetNumberOfAperiodicities(sampleRate)));

  std::vector<double *> spectrogram(result.frameCount);
  std::vector<double *> aperiodicity(result.frameCount);
  for (uint32_t frame = 0u; frame < result.frameCount; ++frame) {
    spectrogram[frame] = const_cast<double *>(spectrogramStorage[frame].data());
    aperiodicity[frame] =
        const_cast<double *>(aperiodicityStorage[frame].data());
  }

  if (result.spectralDimensions > 0u) {
    std::vector<std::vector<double>> codedStorage(
        result.frameCount, std::vector<double>(result.spectralDimensions));
    std::vector<double *> coded(result.frameCount);
    for (uint32_t frame = 0u; frame < result.frameCount; ++frame) {
      coded[frame] = codedStorage[frame].data();
    }
    CodeSpectralEnvelope(
        spectrogram.data(), static_cast<int>(result.frameCount), sampleRate,
        fftSize, static_cast<int>(result.spectralDimensions), coded.data());
    result.codedSpectralEnvelope.resize(static_cast<size_t>(result.frameCount) *
                                        result.spectralDimensions);
    for (uint32_t frame = 0u; frame < result.frameCount; ++frame) {
      for (uint32_t dimension = 0u; dimension < result.spectralDimensions;
           ++dimension) {
        result.codedSpectralEnvelope[static_cast<size_t>(frame) *
                                         result.spectralDimensions +
                                     dimension] =
            static_cast<float>(codedStorage[frame][dimension]);
      }
    }
  }

  if (result.aperiodicityDimensions > 0u) {
    std::vector<std::vector<double>> codedStorage(
        result.frameCount, std::vector<double>(result.aperiodicityDimensions));
    std::vector<double *> coded(result.frameCount);
    for (uint32_t frame = 0u; frame < result.frameCount; ++frame) {
      coded[frame] = codedStorage[frame].data();
    }
    CodeAperiodicity(aperiodicity.data(), static_cast<int>(result.frameCount),
                     sampleRate, fftSize, coded.data());
    result.codedAperiodicity.resize(static_cast<size_t>(result.frameCount) *
                                    result.aperiodicityDimensions);
    for (uint32_t frame = 0u; frame < result.frameCount; ++frame) {
      for (uint32_t dimension = 0u; dimension < result.aperiodicityDimensions;
           ++dimension) {
        result.codedAperiodicity[static_cast<size_t>(frame) *
                                     result.aperiodicityDimensions +
                                 dimension] =
            static_cast<float>(codedStorage[frame][dimension]);
      }
    }
  }

  const uint32_t sourceBins = static_cast<uint32_t>(fftSize / 2 + 1);
  result.runtimeLogEnvelope.resize(static_cast<size_t>(result.frameCount) *
                                   kVoxWorldRuntimeEnvelopeBins);
  for (uint32_t frame = 0u; frame < result.frameCount; ++frame) {
    for (uint32_t bin = 0u; bin < kVoxWorldRuntimeEnvelopeBins; ++bin) {
      const float sourcePosition =
          static_cast<float>(bin) * static_cast<float>(sourceBins - 1u) /
          static_cast<float>(kVoxWorldRuntimeEnvelopeBins - 1u);
      const uint32_t source0 = static_cast<uint32_t>(sourcePosition);
      const uint32_t source1 =
          std::min<uint32_t>(source0 + 1u, sourceBins - 1u);
      const double power =
          s3g::lerp(spectrogramStorage[frame][source0],
                    spectrogramStorage[frame][source1],
                    sourcePosition - static_cast<float>(source0));
      result.runtimeLogEnvelope[static_cast<size_t>(frame) *
                                    kVoxWorldRuntimeEnvelopeBins +
                                bin] =
          static_cast<float>(std::log(std::max(1.0e-12, power)));
    }
  }
  return result;
}

static bool rebuildWorldRuntimeEnvelope(VoxWorldParameterData &parameters) {
  if (parameters.frameCount == 0u || parameters.sampleRate <= 0 ||
      parameters.fftSize <= 0 || parameters.spectralDimensions == 0u ||
      parameters.codedSpectralEnvelope.size() !=
          static_cast<size_t>(parameters.frameCount) *
              parameters.spectralDimensions)
    return false;
  std::vector<std::vector<double>> codedStorage(
      parameters.frameCount,
      std::vector<double>(parameters.spectralDimensions));
  std::vector<double *> coded(parameters.frameCount);
  std::vector<std::vector<double>> decodedStorage(
      parameters.frameCount,
      std::vector<double>(static_cast<size_t>(parameters.fftSize / 2 + 1)));
  std::vector<double *> decoded(parameters.frameCount);
  for (uint32_t frame = 0u; frame < parameters.frameCount; ++frame) {
    for (uint32_t dimension = 0u; dimension < parameters.spectralDimensions;
         ++dimension) {
      codedStorage[frame][dimension] =
          parameters.codedSpectralEnvelope[static_cast<size_t>(frame) *
                                               parameters.spectralDimensions +
                                           dimension];
    }
    coded[frame] = codedStorage[frame].data();
    decoded[frame] = decodedStorage[frame].data();
  }
  DecodeSpectralEnvelope(coded.data(), static_cast<int>(parameters.frameCount),
                         parameters.sampleRate, parameters.fftSize,
                         static_cast<int>(parameters.spectralDimensions),
                         decoded.data());
  const uint32_t sourceBins = static_cast<uint32_t>(parameters.fftSize / 2 + 1);
  parameters.runtimeLogEnvelope.resize(
      static_cast<size_t>(parameters.frameCount) *
      kVoxWorldRuntimeEnvelopeBins);
  for (uint32_t frame = 0u; frame < parameters.frameCount; ++frame) {
    for (uint32_t bin = 0u; bin < kVoxWorldRuntimeEnvelopeBins; ++bin) {
      const float sourcePosition =
          static_cast<float>(bin) * static_cast<float>(sourceBins - 1u) /
          static_cast<float>(kVoxWorldRuntimeEnvelopeBins - 1u);
      const uint32_t source0 = static_cast<uint32_t>(sourcePosition);
      const uint32_t source1 =
          std::min<uint32_t>(source0 + 1u, sourceBins - 1u);
      const double power = s3g::lerp(
          decodedStorage[frame][source0], decodedStorage[frame][source1],
          sourcePosition - static_cast<float>(source0));
      parameters.runtimeLogEnvelope[static_cast<size_t>(frame) *
                                        kVoxWorldRuntimeEnvelopeBins +
                                    bin] =
          static_cast<float>(std::log(std::max(1.0e-12, power)));
    }
  }
  return true;
}

static std::shared_ptr<VoxWorldSample> makeWorldSampleFromWave(
    const std::vector<float> &wave, int sampleRate, const std::string &name,
    std::array<std::vector<float>, kVoxWorldPitchAnchorCount - 1u>
        *pitchVariants = nullptr) {
  if (wave.size() < 256u || sampleRate <= 0)
    return nullptr;
  std::vector<double> x(wave.size());
  for (size_t i = 0; i < wave.size(); ++i)
    x[i] = static_cast<double>(std::clamp(wave[i], -1.0f, 1.0f));

  DioOption dio{};
  InitializeDioOption(&dio);
  dio.frame_period = 5.0;
  const int f0Length = GetSamplesForDIO(sampleRate, static_cast<int>(x.size()),
                                        dio.frame_period);
  if (f0Length <= 0)
    return nullptr;
  std::vector<double> temporalPositions(static_cast<size_t>(f0Length));
  std::vector<double> f0(static_cast<size_t>(f0Length));
  std::vector<double> refinedF0(static_cast<size_t>(f0Length));
  Dio(x.data(), static_cast<int>(x.size()), sampleRate, &dio,
      temporalPositions.data(), f0.data());
  StoneMask(x.data(), static_cast<int>(x.size()), sampleRate,
            temporalPositions.data(), f0.data(), f0Length, refinedF0.data());

  // File and voicebank analysis is offline, so Harvest's stronger voiced
  // continuity is worth its additional analysis cost. DIO/StoneMask remains
  // a per-frame fallback for unusual material.
  HarvestOption harvest{};
  InitializeHarvestOption(&harvest);
  harvest.frame_period = dio.frame_period;
  harvest.f0_floor = dio.f0_floor;
  harvest.f0_ceil = dio.f0_ceil;
  const int harvestLength = GetSamplesForHarvest(
      sampleRate, static_cast<int>(x.size()), harvest.frame_period);
  if (harvestLength == f0Length) {
    std::vector<double> harvestPositions(static_cast<size_t>(harvestLength));
    std::vector<double> harvestF0(static_cast<size_t>(harvestLength));
    Harvest(x.data(), static_cast<int>(x.size()), sampleRate, &harvest,
            harvestPositions.data(), harvestF0.data());
    for (int frame = 0; frame < f0Length; ++frame) {
      if (harvestF0[static_cast<size_t>(frame)] > 0.0) {
        refinedF0[static_cast<size_t>(frame)] =
            harvestF0[static_cast<size_t>(frame)];
      }
    }
  }

  CheapTrickOption cheapTrick{};
  InitializeCheapTrickOption(sampleRate, &cheapTrick);
  cheapTrick.fft_size = GetFFTSizeForCheapTrick(sampleRate, &cheapTrick);
  const int bins = cheapTrick.fft_size / 2 + 1;
  std::vector<std::vector<double>> spectrogramStorage(
      static_cast<size_t>(f0Length),
      std::vector<double>(static_cast<size_t>(bins)));
  std::vector<double *> spectrogram(static_cast<size_t>(f0Length));
  for (int i = 0; i < f0Length; ++i)
    spectrogram[static_cast<size_t>(i)] =
        spectrogramStorage[static_cast<size_t>(i)].data();
  CheapTrick(x.data(), static_cast<int>(x.size()), sampleRate,
             temporalPositions.data(), refinedF0.data(), f0Length, &cheapTrick,
             spectrogram.data());

  D4COption d4c{};
  InitializeD4COption(&d4c);
  std::vector<std::vector<double>> aperiodicityStorage(
      static_cast<size_t>(f0Length),
      std::vector<double>(static_cast<size_t>(bins)));
  std::vector<double *> aperiodicity(static_cast<size_t>(f0Length));
  for (int i = 0; i < f0Length; ++i)
    aperiodicity[static_cast<size_t>(i)] =
        aperiodicityStorage[static_cast<size_t>(i)].data();
  D4C(x.data(), static_cast<int>(x.size()), sampleRate,
      temporalPositions.data(), refinedF0.data(), f0Length, cheapTrick.fft_size,
      &d4c, aperiodicity.data());

  auto result = std::make_shared<VoxWorldSample>();
  result->sampleRate = sampleRate;
  result->framePeriodMs = dio.frame_period;
  result->name = name;
  result->parameters =
      makeWorldParameterData(spectrogramStorage, aperiodicityStorage,
                             sampleRate, cheapTrick.fft_size, dio.frame_period);
  result->f0.resize(static_cast<size_t>(f0Length));
  result->frameEnergy.resize(static_cast<size_t>(f0Length));
  std::vector<float> voicedF0;
  voicedF0.reserve(refinedF0.size());
  for (size_t frame = 0u; frame < refinedF0.size(); ++frame) {
    const float frequency = static_cast<float>(std::max(0.0, refinedF0[frame]));
    result->f0[frame] = frequency;
    if (frequency >= 40.0f && frequency <= 1600.0f)
      voicedF0.push_back(frequency);
  }
  if (!voicedF0.empty()) {
    const auto middle =
        voicedF0.begin() + static_cast<std::ptrdiff_t>(voicedF0.size() / 2u);
    std::nth_element(voicedF0.begin(), middle, voicedF0.end());
    result->baseMidi = std::clamp(
        static_cast<int>(std::lround(
            69.0 + 12.0 * std::log2(static_cast<double>(*middle) / 440.0))),
        0, 127);
  }

  const auto renderPitchAnchor = [&](int semitoneOffset,
                                     std::vector<float> &destination) {
    std::vector<double> synthesisF0 = refinedF0;
    const double ratio =
        std::pow(2.0, static_cast<double>(semitoneOffset) / 12.0);
    if (semitoneOffset != 0) {
      for (double &frequency : synthesisF0) {
        if (frequency > 0.0)
          frequency = std::clamp(frequency * ratio, 20.0, 2400.0);
      }
    }
    std::vector<double> y(x.size());
    Synthesis(synthesisF0.data(), f0Length, spectrogram.data(),
              aperiodicity.data(), cheapTrick.fft_size, dio.frame_period,
              sampleRate, static_cast<int>(y.size()), y.data());
    destination.resize(y.size());
    float peak = 0.0f;
    for (size_t i = 0u; i < y.size(); ++i) {
      const float value = std::isfinite(y[i])
                              ? static_cast<float>(std::clamp(y[i], -1.0, 1.0))
                              : 0.0f;
      destination[i] = value;
      peak = std::max(peak, std::fabs(value));
    }
    if (peak > 0.86f) {
      const float gain = 0.86f / peak;
      for (float &value : destination)
        value *= gain;
    }
  };

  renderPitchAnchor(0, result->samples);
  if (pitchVariants) {
    for (uint32_t anchor = 0u; anchor < kVoxWorldPitchAnchorCount; ++anchor) {
      if (anchor == kVoxWorldPitchAnchorCenter)
        continue;
      const uint32_t variant =
          anchor < kVoxWorldPitchAnchorCenter ? anchor : anchor - 1u;
      renderPitchAnchor(kVoxWorldPitchAnchorSemitones[anchor],
                        (*pitchVariants)[variant]);
    }
  }

  const int frameRadius =
      std::max(1, static_cast<int>(0.5 * dio.frame_period *
                                   static_cast<double>(sampleRate) / 1000.0));
  for (int frame = 0; frame < f0Length; ++frame) {
    const int center = static_cast<int>(
        std::round(temporalPositions[static_cast<size_t>(frame)] *
                   static_cast<double>(sampleRate)));
    double sum = 0.0;
    int count = 0;
    for (int offset = -frameRadius; offset <= frameRadius; ++offset) {
      const int sampleIndex = center + offset;
      if (sampleIndex < 0 ||
          sampleIndex >= static_cast<int>(result->samples.size()))
        continue;
      const double value = result->samples[static_cast<size_t>(sampleIndex)];
      sum += value * value;
      ++count;
    }
    result->frameEnergy[static_cast<size_t>(frame)] =
        count > 0
            ? static_cast<float>(std::sqrt(sum / static_cast<double>(count)))
            : 0.0f;
  }
  return result;
}

struct VoxWorldCacheHeader {
  std::array<char, 8> magic{'S', '3', 'G', 'V', 'W', 'L', 'D', '4'};
  uint32_t version = 4u;
  int32_t sampleRate = 0;
  int32_t baseMidi = 60;
  int32_t fftSize = 0;
  uint32_t frameCount = 0u;
  uint32_t spectralDimensions = 0u;
  uint32_t aperiodicityDimensions = 0u;
  double framePeriodMs = 5.0;
  uint64_t sampleCount = 0u;
  uint64_t f0Count = 0u;
  uint64_t energyCount = 0u;
  uint64_t spectralCount = 0u;
  uint64_t aperiodicityCount = 0u;
  std::array<uint64_t, kVoxWorldPitchAnchorCount - 1u> variantCount{};
};

static uint64_t voxCacheHash(const std::string &text) {
  uint64_t hash = 1469598103934665603ull;
  for (const unsigned char byte : text) {
    hash ^= static_cast<uint64_t>(byte);
    hash *= 1099511628211ull;
  }
  return hash;
}

#include "s3g_complex_processor_vox_cache.inc"

template <typename T>
static bool voxReadCacheVector(std::ifstream &stream, std::vector<T> &values,
                               uint64_t count, uint64_t maximum) {
  if (count > maximum)
    return false;
  values.resize(static_cast<size_t>(count));
  if (values.empty())
    return true;
  return static_cast<bool>(
      stream.read(reinterpret_cast<char *>(values.data()),
                  static_cast<std::streamsize>(values.size() * sizeof(T))));
}

template <typename T>
static bool voxWriteCacheVector(std::ofstream &stream,
                                const std::vector<T> &values) {
  if (values.empty())
    return true;
  stream.write(reinterpret_cast<const char *>(values.data()),
               static_cast<std::streamsize>(values.size() * sizeof(T)));
  return static_cast<bool>(stream);
}

static std::shared_ptr<const VoxVoicebankAudio>
readVoxWorldCache(const std::filesystem::path &wavURL) {
  const std::string path = voxWorldCachePath(wavURL);
  if (path.empty())
    return nullptr;
  std::ifstream stream(foundation::pathFromUtf8(path.c_str()),
                       std::ios::binary);
  if (!stream)
    return nullptr;
  VoxWorldCacheHeader header{};
  if (!stream.read(reinterpret_cast<char *>(&header), sizeof(header)))
    return nullptr;
  const std::array<char, 8> expected{'S', '3', 'G', 'V', 'W', 'L', 'D', '4'};
  if (header.magic != expected || header.version != 4u ||
      header.sampleRate < 8000 || header.sampleRate > 384000 ||
      header.fftSize < 256 || header.fftSize > 32768 ||
      header.frameCount == 0u || header.frameCount > 2000000u ||
      header.spectralDimensions == 0u || header.spectralDimensions > 256u ||
      header.aperiodicityDimensions == 0u ||
      header.aperiodicityDimensions > 16u || header.sampleCount < 32u ||
      header.sampleCount > 40000000u)
    return nullptr;
  auto audio = std::make_shared<VoxVoicebankAudio>();
  audio->sampleRate = header.sampleRate;
  audio->baseMidi = std::clamp(header.baseMidi, 0, 127);
  audio->worldFramePeriodMs = std::clamp(header.framePeriodMs, 1.0, 20.0);
  if (!voxReadCacheVector(stream, audio->samples, header.sampleCount,
                          40000000u))
    return nullptr;
  for (uint32_t variant = 0u; variant < audio->worldPitchVariants.size();
       ++variant) {
    if (!voxReadCacheVector(stream, audio->worldPitchVariants[variant],
                            header.variantCount[variant], 40000000u))
      return nullptr;
    if (audio->worldPitchVariants[variant].size() != audio->samples.size())
      return nullptr;
  }
  if (!voxReadCacheVector(stream, audio->worldF0, header.f0Count, 2000000u) ||
      !voxReadCacheVector(stream, audio->worldFrameEnergy, header.energyCount,
                          2000000u))
    return nullptr;
  if (audio->worldF0.size() != header.frameCount ||
      audio->worldFrameEnergy.size() != header.frameCount)
    return nullptr;
  auto &parameters = audio->worldParameters;
  parameters.sampleRate = header.sampleRate;
  parameters.fftSize = header.fftSize;
  parameters.framePeriodMs = audio->worldFramePeriodMs;
  parameters.frameCount = header.frameCount;
  parameters.spectralDimensions = header.spectralDimensions;
  parameters.aperiodicityDimensions = header.aperiodicityDimensions;
  if (header.spectralCount != static_cast<uint64_t>(header.frameCount) *
                                  header.spectralDimensions ||
      header.aperiodicityCount != static_cast<uint64_t>(header.frameCount) *
                                      header.aperiodicityDimensions ||
      !voxReadCacheVector(stream, parameters.codedSpectralEnvelope,
                          header.spectralCount, 100000000u) ||
      !voxReadCacheVector(stream, parameters.codedAperiodicity,
                          header.aperiodicityCount, 32000000u) ||
      !rebuildWorldRuntimeEnvelope(parameters))
    return nullptr;
  audio->worldResynthesized = true;
  return audio;
}

static void writeVoxWorldCache(const std::filesystem::path &wavURL,
                               const VoxVoicebankAudio &audio) {
  if (!audio.worldResynthesized || audio.samples.empty() ||
      !audio.worldParameters.ready() ||
      audio.worldF0.size() != audio.worldParameters.frameCount ||
      audio.worldFrameEnergy.size() != audio.worldParameters.frameCount)
    return;
  const std::string path = voxWorldCachePath(wavURL);
  if (path.empty())
    return;
  const std::string temporary = path + ".tmp";
  std::ofstream stream(foundation::pathFromUtf8(temporary.c_str()),
                       std::ios::binary | std::ios::trunc);
  if (!stream)
    return;
  VoxWorldCacheHeader header{};
  header.sampleRate = audio.sampleRate;
  header.baseMidi = audio.baseMidi;
  header.fftSize = audio.worldParameters.fftSize;
  header.frameCount = audio.worldParameters.frameCount;
  header.spectralDimensions = audio.worldParameters.spectralDimensions;
  header.aperiodicityDimensions = audio.worldParameters.aperiodicityDimensions;
  header.framePeriodMs = audio.worldFramePeriodMs;
  header.sampleCount = audio.samples.size();
  header.f0Count = audio.worldF0.size();
  header.energyCount = audio.worldFrameEnergy.size();
  header.spectralCount = audio.worldParameters.codedSpectralEnvelope.size();
  header.aperiodicityCount = audio.worldParameters.codedAperiodicity.size();
  for (uint32_t variant = 0u; variant < audio.worldPitchVariants.size();
       ++variant) {
    header.variantCount[variant] = audio.worldPitchVariants[variant].size();
  }
  stream.write(reinterpret_cast<const char *>(&header), sizeof(header));
  bool ok =
      static_cast<bool>(stream) && voxWriteCacheVector(stream, audio.samples);
  for (const auto &variant : audio.worldPitchVariants)
    ok = ok && voxWriteCacheVector(stream, variant);
  ok = ok && voxWriteCacheVector(stream, audio.worldF0) &&
       voxWriteCacheVector(stream, audio.worldFrameEnergy) &&
       voxWriteCacheVector(stream,
                           audio.worldParameters.codedSpectralEnvelope) &&
       voxWriteCacheVector(stream, audio.worldParameters.codedAperiodicity);
  stream.close();
  if (ok) {
    std::error_code ec;
    std::filesystem::rename(foundation::pathFromUtf8(temporary.c_str()),
                            foundation::pathFromUtf8(path.c_str()), ec);
  } else {
    std::error_code ec;
    std::filesystem::remove(foundation::pathFromUtf8(temporary.c_str()), ec);
  }
}
#endif
