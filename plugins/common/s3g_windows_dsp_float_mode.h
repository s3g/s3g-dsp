#pragma once

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
#include <xmmintrin.h>

namespace s3g::clap_detail {
// Some Windows hosts enter DSP with gradual underflow enabled. Use this only
// around DSP with a reproduced subnormal slowdown, never around host callbacks.
// Restore every MXCSR bit, including the caller's rounding and exception state.
class ScopedWindowsDspFloatMode {
public:
    ScopedWindowsDspFloatMode() noexcept : saved_(_mm_getcsr()) {
        _mm_setcsr(saved_ | 0x8040u);
    }
    ~ScopedWindowsDspFloatMode() noexcept { _mm_setcsr(saved_); }
    ScopedWindowsDspFloatMode(const ScopedWindowsDspFloatMode&) = delete;
    ScopedWindowsDspFloatMode& operator=(const ScopedWindowsDspFloatMode&) = delete;
private:
    unsigned saved_;
};
} // namespace s3g::clap_detail
#endif
