#define S3G_FORCE_PORTABLE_AMBI_FFT 1
#include "s3g_ambi_effect_fft.h"
#if defined(__APPLE__)
#include <Accelerate/Accelerate.h>
#endif
#include <iostream>
int main() {
  namespace P = s3g::ambi_effect_fft;
  for (unsigned rank : {11u, 12u}) {
    const size_t n = size_t(1) << rank;
    std::vector<float> input(n), real(n / 2), imag(n / 2), output(n);
    auto setup = P::vDSP_create_fftsetup(rank, P::radix2);
    if (!setup)
      return 1;
    for (int scene = 0; scene < 4; ++scene) {
      for (size_t i = 0; i < n; ++i)
        input[i] = scene == 0
                       ? (i == 0 ? 1.f : 0.f)
                       : scene == 1
                             ? .1f
                             : scene == 2 ? (i % 2 ? .2f : -.2f)
                                          : float(.2 * std::sin(i * .0743) +
                                                  .1 * std::cos(i * .4291));
      P::DSPSplitComplex s{real.data(), imag.data()};
      P::vDSP_ctoz(reinterpret_cast<const P::DSPComplex *>(input.data()), 2, &s,
                   1, n / 2);
      P::vDSP_fft_zrip(setup, &s, 1, rank, P::forward);
#if defined(__APPLE__)
      std::vector<float> ar(n / 2), ai(n / 2);
      DSPSplitComplex apple{ar.data(), ai.data()};
      auto native = vDSP_create_fftsetup(rank, kFFTRadix2);
      vDSP_ctoz(reinterpret_cast<const DSPComplex *>(input.data()), 2, &apple,
                1, n / 2);
      vDSP_fft_zrip(native, &apple, 1, rank, FFT_FORWARD);
      for (size_t i = 0; i < n / 2; ++i)
        // Accelerate uses float butterflies; the portable setup uses double
        // scratch. Allow two float ulps at large bins plus near-zero noise.
        if (std::abs(ar[i] - real[i]) > 2.e-4 + 2.4e-7 * std::abs(ar[i]) ||
            std::abs(ai[i] - imag[i]) > 2.e-4 + 2.4e-7 * std::abs(ai[i])) {
          std::cerr << "forward packing/scale mismatch " << rank << " " << scene
                    << " " << i << " " << ar[i] << " / " << real[i] << ", "
                    << ai[i] << " / " << imag[i] << " difference "
                    << ar[i] - real[i] << ", " << ai[i] - imag[i] << "\n";
          return 1;
        }
      vDSP_fft_zrip(native, &apple, 1, rank, FFT_INVERSE);
#endif
      P::vDSP_fft_zrip(setup, &s, 1, rank, P::inverse);
      P::vDSP_ztoc(&s, 1, reinterpret_cast<P::DSPComplex *>(output.data()), 2,
                   n / 2);
      for (size_t i = 0; i < n; ++i)
        if (std::abs(output[i] / float(2 * n) - input[i]) > 1.e-6) {
          std::cerr << "roundtrip scale mismatch\n";
          return 1;
        }
#if defined(__APPLE__)
      for (size_t i = 0; i < n / 2; ++i)
        if (std::abs(ar[i] - real[i]) / n > 1.e-6 ||
            std::abs(ai[i] - imag[i]) / n > 1.e-6)
          return 1;
      vDSP_destroy_fftsetup(native);
#endif
    }
    P::vDSP_destroy_fftsetup(setup);
  }
  std::cout << "Portable packed FFT / Accelerate equivalence passed\n";
}
