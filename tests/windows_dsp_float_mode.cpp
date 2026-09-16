#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
#include "../plugins/common/s3g_windows_dsp_float_mode.h"
#include <cstdio>
#include <limits>
#include <initializer_list>
int main() {
    unsigned errors = 0, checks = 0;
    const unsigned saved = _mm_getcsr();
    auto check = [&](bool good) { ++checks; errors += !good; };
    for (unsigned flush : {0u, 0x40u, 0x8000u, 0x8040u}) {
        for (unsigned rounding : {0u, 0x2000u, 0x4000u, 0x6000u}) {
            const unsigned initial = 0x1f95u | flush | rounding;
            _mm_setcsr(initial);
            {
                s3g::clap_detail::ScopedWindowsDspFloatMode outer;
                check(_mm_getcsr() == (initial | 0x8040u));
                volatile float tiny = std::numeric_limits<float>::denorm_min();
                volatile float factor = 2.0f;
                const float result = tiny * factor;
                check(result == 0.0f);
                volatile double tiny64 = std::numeric_limits<double>::denorm_min();
                volatile double factor64 = 2.0;
                const double result64 = tiny64 * factor64;
                check(result64 == 0.0);
                const unsigned beforeNested = _mm_getcsr();
                {
                    s3g::clap_detail::ScopedWindowsDspFloatMode nested;
                    _mm_setcsr(_mm_getcsr() | 0x20u);
                }
                check(_mm_getcsr() == beforeNested);
            }
            check(_mm_getcsr() == initial);
        }
    }
    _mm_setcsr(0x1f80u);
    volatile float tiny = std::numeric_limits<float>::denorm_min();
    volatile float factor = 2.0f;
    check(tiny * factor != 0.0f); // Control: this machine really uses gradual underflow.
    _mm_setcsr(saved);
    std::printf("checks=%u failures=%u; float/double subnormals, all flush/rounding modes, nested and full caller-state restoration\n", checks, errors);
    return errors ? 1 : 0;
}
#else
int main() { return 77; }
#endif
