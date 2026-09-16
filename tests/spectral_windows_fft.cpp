#if !defined(_WIN32)
#error "Windows spectral backend regression only"
#endif
#include "s3g_spectral_fft.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

int main()
{
    unsigned checks = 0;
    for (const uint32_t size : {64u, 256u, 2048u, 4096u, 16384u}) {
        for (const uint32_t overlap : {4u, 8u}) {
            for (const bool swapChannels : {false, true}) {
                s3g::SpectralFftProcessor fft;
                if (!fft.prepare(2, size, overlap)) {
                    std::cerr << "Windows spectral prepare failed: " << size << '/' << overlap << '\n';
                    return 1;
                }
                const unsigned total = size * 5;
                std::vector<float> a(total), b(total), x(total, -1), y(total, -1);
                for (unsigned i = 0; i < total; ++i) {
                    a[i] = float(.1 + .2 * std::sin(i * .0713) + (i % 2 ? .05 : -.05));
                    b[i] = i % 127 == 0 ? .4f : 0.f;
                }
                for (unsigned offset = 0; offset < total;) {
                    // Deliberately cross hop boundaries with irregular host blocks.
                    const unsigned frames = std::min(total - offset, 17u + offset % 251u);
                    const float* input[] = {a.data() + offset, b.data() + offset};
                    float* output[] = {x.data() + offset, y.data() + offset};
                    if (swapChannels) {
                        fft.processBlock(input, output, frames, [](s3g::SpectralFrameBlockView& block) {
                            for (unsigned bin = 0; bin < block.bins; ++bin) {
                                std::swap(block.real(0)[bin], block.real(1)[bin]);
                                std::swap(block.imag(0)[bin], block.imag(1)[bin]);
                            }
                        });
                    } else {
                        fft.process(input, output, frames, [](s3g::SpectralFrameView&) {});
                    }
                    offset += frames;
                }
                // Allow startup OLA to fill; the established engine latency is one FFT.
                for (unsigned i = size * 2; i < total; ++i) {
                    const auto expectedX = swapChannels ? b[i - size] : a[i - size];
                    const auto expectedY = swapChannels ? a[i - size] : b[i - size];
                    if (!std::isfinite(x[i]) || !std::isfinite(y[i])
                        || std::abs(x[i] - expectedX) > 3.e-6f
                        || std::abs(y[i] - expectedY) > 3.e-6f) {
                        std::cerr << "Spectral normalization/routing mismatch size=" << size
                                  << " overlap=" << overlap << " swap=" << swapChannels
                                  << " frame=" << i << " got=" << x[i] << ',' << y[i]
                                  << " expected=" << expectedX << ',' << expectedY << '\n';
                        return 1;
                    }
                    ++checks;
                }
                fft.reset();
                if (!fft.ready()) return 1;
            }
        }
    }
    s3g::SpectralFftProcessor invalid;
    if (invalid.prepare(0, 4096, 8) || invalid.prepare(2, 100, 8)
        || invalid.prepare(2, 4096, 3) || invalid.ready()) return 1;
    std::cout << "Passed " << checks << " Windows spectral reconstruction/routing samples\n";
}
