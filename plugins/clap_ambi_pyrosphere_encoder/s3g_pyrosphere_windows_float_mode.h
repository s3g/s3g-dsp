#pragma once

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
#include <xmmintrin.h>

namespace s3g::clap_detail {

// Windows x64 audio threads may arrive with gradual underflow enabled.
// Scope FTZ (bit 15) and DAZ (bit 6) to this DSP call; preserve the host's
// rounding, exception masks and status flags on return.
class ScopedPyrosphereWindowsFloatMode {
public:
    ScopedPyrosphereWindowsFloatMode() noexcept : saved_(_mm_getcsr())
    {
        _mm_setcsr(saved_ | 0x8040u);
    }
    ~ScopedPyrosphereWindowsFloatMode() noexcept { _mm_setcsr(saved_); }

    ScopedPyrosphereWindowsFloatMode(
        const ScopedPyrosphereWindowsFloatMode&) = delete;
    ScopedPyrosphereWindowsFloatMode& operator=(
        const ScopedPyrosphereWindowsFloatMode&) = delete;

private:
    unsigned int saved_;
};

} // namespace s3g::clap_detail
#endif
