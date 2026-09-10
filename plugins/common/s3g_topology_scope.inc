void Editor::scope(CRect f) {
  const uint32_t write = p.scopeWrite.load(std::memory_order_relaxed);
#if defined(S3G_TOPOLOGY_SPECTRAL_TOPOLOGY_PORT)
  smallText("POST PROCESSING SONOGRAM", f.left + 10., f.top + 8., 350.);
  // Identical 96 x 24 analyzer, 48-sample Hann window and one-second history.
  // Cache only the invariant trigonometry, never the output samples.
  constexpr uint32_t timeBins = 96, freqBins = 24, windowSize = 48;
  constexpr double tau = 6.28318530717958647692;
  struct Basis {
    double window[windowSize]{}, cosine[freqBins][windowSize]{},
        sine[freqBins][windowSize]{};
    Basis() {
      for (uint32_t n = 0; n < windowSize; ++n) {
        window[n] =
            .5 - .5 * std::cos(tau * double(n) / double(windowSize - 1));
        for (uint32_t bin = 0; bin < freqBins; ++bin) {
          const double phase =
              tau * double(bin + 1) * double(n) / double(windowSize);
          cosine[bin][n] = std::cos(phase);
          sine[bin][n] = std::sin(phase);
        }
      }
    }
  };
  static const Basis basis;
  const uint32_t history = std::min<uint32_t>(
      kScopeFrames - windowSize,
      std::max<uint32_t>(windowSize, static_cast<uint32_t>(std::clamp(
                                         p.sampleRate, 8000.,
                                         double(kScopeFrames - windowSize)))));
  for (uint32_t lane = 0; lane < kChannelCount; ++lane) {
    const auto b = channelRect(f, lane);
    fill(b, color(0x0d0f11));
    std::array<float, timeBins * freqBins> mags{};
    float peak = .000001f;
    for (uint32_t t = 0; t < timeBins; ++t) {
      const uint32_t age = static_cast<uint32_t>(
          double(timeBins - 1 - t) / double(timeBins - 1) * double(history));
      const uint32_t center = (write + kScopeFrames - 1 - age) % kScopeFrames;
      // Each bin sees the same captured 48 samples; no cross-lane mix.
      std::array<double, windowSize> samples{};
      for (uint32_t n = 0; n < windowSize; ++n) {
        const uint32_t index =
            (center + kScopeFrames - (windowSize - 1 - n)) % kScopeFrames;
        samples[n] =
            double(p.scope[lane][index].load(std::memory_order_relaxed)) *
            basis.window[n];
      }
      for (uint32_t bin = 0; bin < freqBins; ++bin) {
        double re = 0., im = 0.;
        for (uint32_t n = 0; n < windowSize; ++n) {
          re += samples[n] * basis.cosine[bin][n];
          im -= samples[n] * basis.sine[bin][n];
        }
        const float mag =
            float(std::sqrt(re * re + im * im) / double(windowSize));
        mags[t * freqBins + bin] = mag;
        peak = std::max(peak, mag);
      }
    }
    const double w = b.getWidth() / timeBins, h = b.getHeight() / freqBins;
    for (uint32_t t = 0; t < timeBins; ++t)
      for (uint32_t bin = 0; bin < freqBins; ++bin) {
        const float raw = mags[t * freqBins + bin] / peak;
        const double norm = std::pow(std::clamp(double(raw), 0., 1.), .36);
        const uint8_t level =
            static_cast<uint8_t>(std::clamp(10. + norm * 220., 10., 230.));
        fill(rect(b.left + t * w, b.bottom - (bin + 1) * h, w + .20, h + .20),
             gray(level, .98));
      }
    smallText(format("L%u", lane + 1), b.left + 5., b.top + 4., 40.);
  }
#else
  smallText("POST PROCESSING OSCILLOSCOPE", f.left + 10., f.top + 8., 350.);
#if defined(S3G_TOPOLOGY_DELAY_PORT)
  constexpr uint32_t samples = 512;
  const uint32_t history = std::min<uint32_t>(
      kScopeFrames - 1,
      std::max<uint32_t>(samples,
                         static_cast<uint32_t>(std::clamp(
                             p.sampleRate, 8000., double(kScopeFrames - 1)))));
#else
  constexpr uint32_t samples = kScopeFrames;
#endif
  for (uint32_t lane = 0; lane < kChannelCount; ++lane) {
    const auto b = channelRect(f, lane);
    fill(b, gray(0x10));
    line({b.left, b.top + b.getHeight() * .5},
         {b.right, b.top + b.getHeight() * .5}, gray(0x2f, .8));
    std::array<float, samples> data{};
    float peak = .0001f;
    for (uint32_t i = 0; i < samples; ++i) {
#if defined(S3G_TOPOLOGY_DELAY_PORT)
      const uint32_t age = static_cast<uint32_t>(
          double(samples - 1 - i) / double(samples - 1) * double(history));
      const uint32_t index = (write + kScopeFrames - 1 - age) % kScopeFrames;
#else
      const uint32_t index = (write + i) % kScopeFrames;
#endif
      data[i] = p.scope[lane][index].load(std::memory_order_relaxed);
      peak = std::max(peak, std::abs(data[i]));
    }
    const float scale = std::min(4.f, .92f / peak);
    std::vector<CPoint> points;
    points.reserve(samples);
    for (uint32_t i = 0; i < samples; ++i)
      points.emplace_back(
          b.left + double(i) / double(samples - 1) * b.getWidth(),
          b.top + b.getHeight() * .5 -
              std::clamp(data[i] * scale, -1.f, 1.f) * b.getHeight() * .42);
    polygon(points, style.text, false);
    smallText(format("L%u", lane + 1), b.left + 5., b.top + 4., 40.);
  }
#endif
}
