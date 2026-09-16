#pragma once
#if !defined(_WIN32)
#error "Windows spectral adapter only"
#endif
#include "s3g_ambi_effect_fft.h"
#include <cstddef>
#if defined(_M_X64) || defined(__x86_64__)
#include <emmintrin.h>
#endif

namespace s3g::spectral_windows_fft {
// Reuse the repository's packed-real radix-2 backend and its established
// Accelerate-compatible normalization. No allocation occurs after setup.
using ambi_effect_fft::DSPComplex;
using ambi_effect_fft::DSPSplitComplex;
using ambi_effect_fft::vDSP_ctoz;
using ambi_effect_fft::vDSP_ztoc;
// Windows spectral-only SIMD butterflies. Keep the portable Ambi backend and
// macOS Accelerate paths unchanged. SSE2 is part of the Windows x64 baseline.
#if defined(_M_X64) || defined(__x86_64__)
struct Setup : ambi_effect_fft::Setup {
    explicit Setup(unsigned rank) : ambi_effect_fft::Setup(rank) {}
    void transform(bool backward) {
        for (size_t i = 0; i < n; ++i)
            if (i < reverse[i]) std::swap(values[i], values[reverse[i]]);
        const __m128d conjugate = backward ? _mm_set_pd(-0.0, 0.0) : _mm_setzero_pd();
        const __m128d subtractReal = _mm_set_pd(0.0, -0.0);
        for (size_t width = 2; width <= n; width <<= 1) {
            const size_t half = width / 2, step = n / width;
            for (size_t base = 0; base < n; base += width)
                for (size_t j = 0; j < half; ++j) {
                    // std::complex<double> provides array-oriented access as
                    // consecutive real/imaginary doubles; no new alignment is required.
                    const auto root = _mm_xor_pd(_mm_loadu_pd(
                        reinterpret_cast<const double*>(&roots[j * step])), conjugate);
                    const auto a = _mm_loadu_pd(reinterpret_cast<const double*>(&values[base + j]));
                    const auto v = _mm_loadu_pd(reinterpret_cast<const double*>(&values[base + j + half]));
                    const auto first = _mm_mul_pd(v, _mm_unpacklo_pd(root, root));
                    const auto second = _mm_mul_pd(_mm_shuffle_pd(v, v, 1), _mm_unpackhi_pd(root, root));
                    const auto b = _mm_add_pd(first, _mm_xor_pd(second, subtractReal));
                    _mm_storeu_pd(reinterpret_cast<double*>(&values[base + j]), _mm_add_pd(a, b));
                    _mm_storeu_pd(reinterpret_cast<double*>(&values[base + j + half]), _mm_sub_pd(a, b));
                }
        }
    }
};
using FFTSetup = Setup*;
inline FFTSetup vDSP_create_fftsetup(unsigned rank, int) {
    if (rank < 1 || rank > 18) return nullptr;
    try { return new Setup(rank); } catch (...) { return nullptr; }
}
inline void vDSP_destroy_fftsetup(FFTSetup setup) { delete setup; }
inline void vDSP_fft_zrip(FFTSetup setup, DSPSplitComplex* split,
    std::ptrdiff_t stride, unsigned rank, int direction) {
    if (!setup || setup->n != (size_t(1) << rank)) return;
    auto& v = setup->values;
    const size_t half = setup->n / 2;
    if (direction == ambi_effect_fft::forward) {
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
        v[0] = split->realp[0]; v[half] = split->imagp[0];
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
#else
using ambi_effect_fft::FFTSetup;
using ambi_effect_fft::vDSP_create_fftsetup;
using ambi_effect_fft::vDSP_destroy_fftsetup;
using ambi_effect_fft::vDSP_fft_zrip;
#endif

inline constexpr int kFFTRadix2 = ambi_effect_fft::radix2;
inline constexpr int FFT_FORWARD = ambi_effect_fft::forward;
inline constexpr int FFT_INVERSE = ambi_effect_fft::inverse;
inline void vDSP_vsmul(const float* input, std::ptrdiff_t stride,
    const float* scale, float* output, std::ptrdiff_t outputStride, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) output[i * outputStride] = input[i * stride] * *scale;
}
inline void vDSP_vmul(const float* a, std::ptrdiff_t aStride,
    const float* b, std::ptrdiff_t bStride, float* output,
    std::ptrdiff_t outputStride, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) output[i * outputStride] = a[i * aStride] * b[i * bStride];
}
inline void vDSP_vadd(const float* a, std::ptrdiff_t aStride,
    const float* b, std::ptrdiff_t bStride, float* output,
    std::ptrdiff_t outputStride, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) output[i * outputStride] = a[i * aStride] + b[i * bStride];
}
} // namespace s3g::spectral_windows_fft
