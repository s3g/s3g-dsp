#include "s3g_spectral_windows_fft.h"
#include <xmmintrin.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
int main() {
    const unsigned original = _mm_getcsr();
    unsigned checks = 0;
    for (unsigned mode : {original & ~0x8040u, original | 0x8040u}) {
        _mm_setcsr(mode);
        for (unsigned rank : {1u, 6u, 8u, 11u, 12u, 14u}) {
            const size_t n = size_t(1) << rank, half = n/2;
            auto old = s3g::ambi_effect_fft::vDSP_create_fftsetup(rank, 0);
            auto next = s3g::spectral_windows_fft::vDSP_create_fftsetup(rank, 0);
            if (!old || !next) return 1;
            for (size_t stride : {size_t(1), size_t(2)}) for (unsigned pattern = 0; pattern < 5; ++pattern) {
                std::vector<float> ar(n * stride), ai(n * stride), br(n * stride), bi(n * stride);
                for (size_t i = 0; i < half; ++i) {
                    ar[i*stride] = pattern == 0 ? 0.f : pattern == 1 ? float(i==0) : pattern == 2 ? 1.f : pattern == 3 ? std::sin(float(i)*0.731f) : float(int((i*1664525u+1013904223u)&65535)-32768)/32768.f;
                    ai[i*stride] = pattern == 2 ? -1.f : std::cos(float(i)*0.415f)*0.37f;
                }
                br=ar;bi=ai;
                s3g::ambi_effect_fft::DSPSplitComplex a{ar.data(),ai.data()},b{br.data(),bi.data()};
                for (int direction : {s3g::ambi_effect_fft::forward,s3g::ambi_effect_fft::inverse}) {
                    s3g::ambi_effect_fft::vDSP_fft_zrip(old,&a,stride,rank,direction);
                    s3g::spectral_windows_fft::vDSP_fft_zrip(next,&b,stride,rank,direction);
                    for (size_t i=0;i<half;++i) if (std::memcmp(&ar[i*stride],&br[i*stride],sizeof(float)) || std::memcmp(&ai[i*stride],&bi[i*stride],sizeof(float)) || !std::isfinite(br[i*stride]) || !std::isfinite(bi[i*stride])) {
                        std::fprintf(stderr,"Mismatch rank=%u stride=%zu pattern=%u direction=%d bin=%zu\n",rank,stride,pattern,direction,i);_mm_setcsr(original);return 1;
                    }
                    if ((_mm_getcsr() & ~0x3fu) != (mode & ~0x3fu)) return 1;
                    ++checks;
                }
            }
            s3g::ambi_effect_fft::vDSP_destroy_fftsetup(old);
            s3g::spectral_windows_fft::vDSP_destroy_fftsetup(next);
        }
    }
    _mm_setcsr(original);
    std::printf("Windows spectral butterflies: %u bit-identical packed/strided forward/inverse cases; controls preserved\n",checks);
}
