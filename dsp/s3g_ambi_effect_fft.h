#pragma once
// The three capture/trace engines use this small packed-real FFT surface.
// macOS retains Accelerate verbatim. Other platforms use an allocation-free
// radix-2 transform after setup, with the same packing and normalization.
#if defined(__APPLE__) && !defined(S3G_FORCE_PORTABLE_AMBI_FFT)
#include <Accelerate/Accelerate.h>
namespace s3g::ambi_effect_fft {
using ::DSPComplex;
using ::DSPSplitComplex;
using ::FFTSetup;
using ::vDSP_create_fftsetup;
using ::vDSP_ctoz;
using ::vDSP_destroy_fftsetup;
using ::vDSP_fft_zrip;
using ::vDSP_ztoc;
using ::vDSP_zvma;
inline constexpr int radix2 = kFFTRadix2;
inline constexpr int forward = FFT_FORWARD;
inline constexpr int inverse = FFT_INVERSE;
} // namespace s3g::ambi_effect_fft
#else
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace s3g::ambi_effect_fft {
inline constexpr int radix2 = 0, forward = 1, inverse = -1;
struct DSPComplex {
  float real, imag;
};
struct DSPSplitComplex {
  float *realp, *imagp;
};
struct Setup {
  explicit Setup(unsigned rank)
      : n(size_t(1) << rank), values(n), roots(n / 2), reverse(n) {
    constexpr double pi = 3.1415926535897932384626433832795;
    for (size_t i = 0; i < n / 2; ++i)
      roots[i] = std::polar(1., -2. * pi * double(i) / double(n));
    for (size_t i = 0; i < n; ++i) {
      size_t x = i, r = 0;
      for (unsigned b = 0; b < rank; ++b) {
        r = (r << 1) | (x & 1);
        x >>= 1;
      }
      reverse[i] = r;
    }
  }
  void transform(bool backward) {
    for (size_t i = 0; i < n; ++i)
      if (i < reverse[i])
        std::swap(values[i], values[reverse[i]]);
    for (size_t width = 2; width <= n; width <<= 1) {
      const size_t half = width / 2, step = n / width;
      for (size_t base = 0; base < n; base += width)
        for (size_t j = 0; j < half; ++j) {
          const auto root =
              backward ? std::conj(roots[j * step]) : roots[j * step];
          const auto a = values[base + j], b = values[base + j + half] * root;
          values[base + j] = a + b;
          values[base + j + half] = a - b;
        }
    }
  }
  size_t n;
  std::vector<std::complex<double>> values, roots;
  std::vector<size_t> reverse;
};
using FFTSetup = Setup *;
inline FFTSetup vDSP_create_fftsetup(unsigned rank, int) {
  if (rank < 1 || rank > 18)
    return nullptr;
  try {
    return new Setup(rank);
  } catch (...) {
    return nullptr;
  }
}
inline void vDSP_destroy_fftsetup(FFTSetup setup) { delete setup; }
inline void vDSP_ctoz(const DSPComplex *input, ptrdiff_t inputStride,
                      DSPSplitComplex *output, ptrdiff_t outputStride,
                      size_t count) {
  const auto *samples = reinterpret_cast<const float *>(input);
  for (size_t i = 0; i < count; ++i) {
    output->realp[i * outputStride] = samples[i * inputStride];
    output->imagp[i * outputStride] = samples[i * inputStride + 1];
  }
}
inline void vDSP_ztoc(const DSPSplitComplex *input, ptrdiff_t inputStride,
                      DSPComplex *output, ptrdiff_t outputStride,
                      size_t count) {
  auto *samples = reinterpret_cast<float *>(output);
  for (size_t i = 0; i < count; ++i) {
    samples[i * outputStride] = input->realp[i * inputStride];
    samples[i * outputStride + 1] = input->imagp[i * inputStride];
  }
}
inline void vDSP_fft_zrip(FFTSetup setup, DSPSplitComplex *split,
                          ptrdiff_t stride, unsigned rank, int direction) {
  if (!setup || setup->n != (size_t(1) << rank))
    return;
  auto &v = setup->values;
  const size_t half = setup->n / 2;
  if (direction == forward) {
    for (size_t i = 0; i < half; ++i) {
      v[i * 2] = split->realp[i * stride];
      v[i * 2 + 1] = split->imagp[i * stride];
    }
    setup->transform(false);
    split->realp[0] = float(v[0].real() * 2.);
    split->imagp[0] = float(v[half].real() * 2.);
    for (size_t i = 1; i < half; ++i) {
      split->realp[i * stride] = float(v[i].real() * 2.);
      split->imagp[i * stride] = float(v[i].imag() * 2.);
    }
  } else {
    v[0] = split->realp[0];
    v[half] = split->imagp[0];
    for (size_t i = 1; i < half; ++i) {
      v[i] = {split->realp[i * stride], split->imagp[i * stride]};
      v[setup->n - i] = std::conj(v[i]);
    }
    setup->transform(true);
    for (size_t i = 0; i < half; ++i) {
      split->realp[i * stride] = float(v[i * 2].real());
      split->imagp[i * stride] = float(v[i * 2 + 1].real());
    }
  }
}
inline void vDSP_zvma(const DSPSplitComplex *x, ptrdiff_t xs,
                      const DSPSplitComplex *h, ptrdiff_t hs,
                      const DSPSplitComplex *a, ptrdiff_t as,
                      DSPSplitComplex *out, ptrdiff_t os, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    const float xr = x->realp[i * xs], xi = x->imagp[i * xs];
    const float hr = h->realp[i * hs], hi = h->imagp[i * hs];
    const float ar = a->realp[i * as], ai = a->imagp[i * as];
    out->realp[i * os] = ar + xr * hr - xi * hi;
    out->imagp[i * os] = ai + xr * hi + xi * hr;
  }
}
} // namespace s3g::ambi_effect_fft
#endif
