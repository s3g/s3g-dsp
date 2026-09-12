#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace s3g::energy {
constexpr uint32_t columns = 256, rows = 128, channels = 64, snapshotLimit = 16;
constexpr uint32_t pixels = columns * rows;
using Snapshots = std::array<float, channels * snapshotLimit>;
using Weights = std::array<float, channels * 2>;

// ABI shared with both shader languages. Five 16-byte constant-buffer rows.
struct Params {
    uint32_t width = columns, height = rows, snapshotCount = 0, activeChannels = channels;
    uint32_t bodyChannels = 9, mapMode = 0, resetHistory = 0, reserved = 0;
    float inverseFullRms = 1, inverseBodyRms = 1, activity = 0, directionFocus = 0;
    float motionX = 0, motionY = 0, bodyAttack = .22f, bodyRelease = .075f;
    float detailAttack = .58f, detailRelease = .20f, wakeDecay = .968f, wakeGain = 1.35f;
};
static_assert(sizeof(Params) == 80 && std::is_standard_layout_v<Params>);
static_assert(offsetof(Params, bodyChannels) == 16 && offsetof(Params, inverseFullRms) == 32);
static_assert(offsetof(Params, motionX) == 48 && offsetof(Params, detailAttack) == 64);

// Offscreen prototype only. Synchronous GPU readback is intentional for parity
// tests, never suitable for the audio callback or the final live GUI path.
struct Capture {
    std::vector<float> field; // RGBA16F decoded to float: body, detail, wake, activity
    std::vector<uint8_t> rgba; // RGBA8 UNORM, top row first, NOT sRGB
    uint32_t width = 0, height = 0;
};
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual std::string deviceName() const = 0;
    virtual Capture render(const Params&, const Snapshots&, uint32_t width, uint32_t height) = 0;
};
std::unique_ptr<Renderer> makeRenderer(const std::vector<float>& basis, const Weights&, bool software);
void validate(const Params&, const Snapshots&, uint32_t width, uint32_t height);
float fromHalf(uint16_t bits);
uint16_t toHalf(float value);
// Independent transcription of the Metal COMPUTE equations, not the Cocoa
// bitmap fallback. Used to detect field/coordinate/history regressions.
std::vector<float> referenceField(const Params&, const Snapshots&, const std::vector<float>& basis,
                                  const Weights&, const std::vector<float>& previous);
}
