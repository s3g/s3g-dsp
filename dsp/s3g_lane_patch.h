#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace s3g {

constexpr uint32_t kLanePatchMaxChannels = 64;

class LanePatch {
public:
    void setWidth(uint32_t width)
    {
        width_ = std::clamp(width, 1u, kLanePatchMaxChannels);
        for (uint32_t row = width_; row < kLanePatchMaxChannels; ++row) {
            rows_[row] = 0;
        }
        const uint64_t validMask = maskForWidth(width_);
        for (uint32_t row = 0; row < width_; ++row) {
            rows_[row] &= validMask;
        }
    }

    uint32_t width() const { return width_; }

    void setIdentity(uint32_t activeChannels)
    {
        const uint32_t active = std::min(activeChannels, width_);
        clear();
        for (uint32_t ch = 0; ch < active; ++ch) {
            rows_[ch] = uint64_t { 1 } << ch;
        }
    }

    void setWindow(uint32_t inputStart, uint32_t outputStart, uint32_t activeChannels)
    {
        clear();
        const uint32_t input = inputStart > 0 ? inputStart - 1 : 0;
        const uint32_t output = outputStart > 0 ? outputStart - 1 : 0;
        if (input >= width_ || output >= width_) {
            return;
        }

        const uint32_t capacity = std::min(width_ - input, width_ - output);
        const uint32_t active = std::min(activeChannels, capacity);
        for (uint32_t ch = 0; ch < active; ++ch) {
            rows_[input + ch] = uint64_t { 1 } << (output + ch);
        }
    }

    void clear()
    {
        rows_.fill(0);
    }

    bool connected(uint32_t input, uint32_t output) const
    {
        if (input >= width_ || output >= width_) {
            return false;
        }
        return (rows_[input] & (uint64_t { 1 } << output)) != 0;
    }

    void setConnected(uint32_t input, uint32_t output, bool connected)
    {
        if (input >= width_ || output >= width_) {
            return;
        }
        const uint64_t bit = uint64_t { 1 } << output;
        if (connected) {
            rows_[input] |= bit;
        } else {
            rows_[input] &= ~bit;
        }
    }

    void toggle(uint32_t input, uint32_t output)
    {
        setConnected(input, output, !connected(input, output));
    }

    uint64_t rowMask(uint32_t input) const
    {
        return input < width_ ? rows_[input] : 0;
    }

    void setRowMask(uint32_t input, uint64_t mask)
    {
        if (input >= width_) {
            return;
        }
        rows_[input] = mask & maskForWidth(width_);
    }

    const std::array<uint64_t, kLanePatchMaxChannels>& rows() const { return rows_; }

private:
    static uint64_t maskForWidth(uint32_t width)
    {
        if (width >= kLanePatchMaxChannels) {
            return ~uint64_t { 0 };
        }
        return (uint64_t { 1 } << width) - 1;
    }

    uint32_t width_ = 1;
    std::array<uint64_t, kLanePatchMaxChannels> rows_ {};
};

} // namespace s3g
