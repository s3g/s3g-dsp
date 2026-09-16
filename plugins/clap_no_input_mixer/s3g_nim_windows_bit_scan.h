#pragma once

#if !defined(_WIN32) || !defined(_MSC_VER)
#error "This adapter is only for the native Windows compiler."
#endif
#include <cstdint>
#include <intrin.h>

namespace s3g::nim_windows {
// The matrix-mask loops call this only for nonzero masks. Preserve the GNU
// builtin's least-significant-bit ordering for all 64 routes.
inline uint32_t trailingZeroCountNonzero(uint64_t mask) noexcept
{
    unsigned long index = 0;
#if defined(_M_X64) || defined(_M_ARM64)
    _BitScanForward64(&index, mask);
#else
    const auto low = static_cast<unsigned long>(mask);
    if (low != 0) {
        _BitScanForward(&index, low);
    } else {
        _BitScanForward(&index, static_cast<unsigned long>(mask >> 32));
        index += 32;
    }
#endif
    return static_cast<uint32_t>(index);
}
} // namespace s3g::nim_windows
