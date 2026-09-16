#include "../plugins/clap_no_input_mixer/s3g_nim_windows_bit_scan.h"
#include <cstdint>
#include <iostream>

int main()
{
    unsigned checks = 0;
    for (uint32_t first = 0; first < 64; ++first) {
        for (uint32_t second = first; second < 64; ++second) {
            const uint64_t mask = (uint64_t{1} << first) | (uint64_t{1} << second);
            if (s3g::nim_windows::trailingZeroCountNonzero(mask) != first) {
                std::cerr << "Incorrect lowest route: " << first << ',' << second << '\n';
                return 1;
            }
            ++checks;
        }
    }
    uint64_t remaining = ~uint64_t{0};
    for (uint32_t route = 0; route < 64; ++route) {
        const auto actual = s3g::nim_windows::trailingZeroCountNonzero(remaining);
        if (actual != route) return 1;
        remaining &= ~(uint64_t{1} << actual);
        ++checks;
    }
    if (remaining != 0) return 1;
    std::cout << "Passed " << checks << " Windows MIDI matrix bit-scan checks\n";
}
