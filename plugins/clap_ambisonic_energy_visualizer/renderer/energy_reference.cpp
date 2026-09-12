#include "energy_gpu.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace s3g::energy {
float fromHalf(uint16_t bits) {
    const float sign = bits & 0x8000 ? -1.f : 1.f;
    const unsigned exponent = (bits >> 10) & 31, mantissa = bits & 1023;
    if (!exponent) return sign * std::ldexp(float(mantissa), -24);
    if (exponent == 31) return mantissa ? NAN : sign * INFINITY;
    return sign * std::ldexp(float(1024 + mantissa), int(exponent) - 25);
}
uint16_t toHalf(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint16_t sign = uint16_t((bits >> 16) & 0x8000);
    const int exponent = int((bits >> 23) & 255) - 127 + 15;
    const uint32_t mantissa = bits & 0x7fffff;
    if (((bits >> 23) & 255) == 255) return sign | (mantissa ? 0x7e00 : 0x7c00);
    if (exponent >= 31) return sign | 0x7c00;
    if (exponent < -10) return sign;
    const uint32_t significand = exponent <= 0 ? mantissa | 0x800000 : mantissa;
    const unsigned shift = exponent <= 0 ? unsigned(14 - exponent) : 13;
    uint32_t rounded = significand >> shift;
    const uint32_t remainder = significand & ((1u << shift) - 1);
    const uint32_t halfway = 1u << (shift - 1);
    if (remainder > halfway || (remainder == halfway && (rounded & 1))) ++rounded;
    return sign | uint16_t((exponent > 0 ? uint32_t(exponent) << 10 : 0) + rounded);
}

void validate(const Params& p, const Snapshots& samples, uint32_t width, uint32_t height) {
    if (p.width != columns || p.height != rows || p.snapshotCount > snapshotLimit ||
        p.activeChannels < 1 || p.activeChannels > channels || p.bodyChannels != 9 ||
        p.mapMode > 7 || p.resetHistory > 1 || p.reserved ||
        width < 1 || width > 4096 || height < 1 || height > 4096)
        throw std::runtime_error("Invalid energy renderer dimensions/counts");
    const float values[] = {p.inverseFullRms, p.inverseBodyRms, p.activity, p.directionFocus,
        p.motionX, p.motionY, p.bodyAttack, p.bodyRelease, p.detailAttack, p.detailRelease,
        p.wakeDecay, p.wakeGain};
    for (float v : values) if (!std::isfinite(v)) throw std::runtime_error("Non-finite shader parameter");
    for (float v : samples) if (!std::isfinite(v)) throw std::runtime_error("Non-finite snapshot");
    if (p.inverseFullRms <= 0 || p.inverseBodyRms <= 0 || p.activity < 0 || p.activity > 1 ||
        p.directionFocus < 0 || p.directionFocus > 1 || std::abs(p.motionX) > .04f ||
        std::abs(p.motionY) > .04f || p.bodyAttack < 0 || p.bodyAttack > 1 ||
        p.bodyRelease < 0 || p.bodyRelease > 1 || p.detailAttack < 0 || p.detailAttack > 1 ||
        p.detailRelease < 0 || p.detailRelease > 1 || p.wakeDecay < 0 || p.wakeDecay > 1 ||
        p.wakeGain < 0 || p.wakeGain > 4)
        throw std::runtime_error("Out-of-range shader parameter");
}

std::vector<float> referenceField(const Params& p, const Snapshots& samples,
    const std::vector<float>& basis, const Weights& weights, const std::vector<float>& previous) {
    validate(p, samples, columns, rows);
    if (basis.size() != pixels * channels || previous.size() != pixels * 4)
        throw std::runtime_error("Invalid reference buffers");
    std::vector<float> result(pixels * 4);
    const int shiftX = int(std::round(std::clamp(p.motionX * columns * .55f, -2.f, 2.f)));
    const int shiftY = int(std::round(std::clamp(p.motionY * rows * .55f, -2.f, 2.f)));
    for (uint32_t y = 0; y < rows; ++y) for (uint32_t x = 0; x < columns; ++x) {
        const uint32_t pixel = y * columns + x;
        float bodySq = 0, detailSq = 0;
        for (uint32_t s = 0; s < p.snapshotCount; ++s) {
            float body = 0, detail = 0;
            for (uint32_t ch = 0; ch < p.activeChannels; ++ch) {
                const float decoded = samples[s * channels + ch] * basis[pixel * channels + ch];
                detail += decoded * weights[ch];
                if (ch < p.bodyChannels) body += decoded * weights[channels + ch];
            }
            bodySq += body * body;
            detailSq += detail * detail;
        }
        const float count = float(std::max(1u, p.snapshotCount));
        const float body = std::sqrt(bodySq / count) * p.inverseBodyRms;
        const float detail = std::sqrt(detailSq / count) * p.inverseFullRms;
        const float bodyTarget = (1.f - std::exp(-body * .95f)) * p.activity;
        const float detailTarget = (1.f - std::exp(-detail * .72f)) * p.activity;
        const float oldBody = p.resetHistory ? 0 : previous[pixel * 4];
        const float oldDetail = p.resetHistory ? 0 : previous[pixel * 4 + 1];
        const int oldX = (int(x) - shiftX + int(columns)) % int(columns);
        const int oldY = std::clamp(int(y) - shiftY, 0, int(rows) - 1);
        const float wakeHistory = p.resetHistory ? 0 : previous[(oldY * columns + oldX) * 4 + 2];
        const float bodyMix = bodyTarget > oldBody ? p.bodyAttack : p.bodyRelease;
        const float detailMix = detailTarget > oldDetail ? p.detailAttack : p.detailRelease;
        const float departed = std::max(oldDetail - detailTarget, 0.f) +
                               std::max(oldBody - bodyTarget, 0.f) * .36f;
        const float values[4] = {oldBody + (bodyTarget - oldBody) * bodyMix,
            oldDetail + (detailTarget - oldDetail) * detailMix,
            std::clamp(std::max(wakeHistory * p.wakeDecay, departed * p.wakeGain), 0.f, 1.f),
            p.activity};
        for (unsigned c = 0; c < 4; ++c) result[pixel * 4 + c] = fromHalf(toHalf(values[c]));
    }
    return result;
}
}
