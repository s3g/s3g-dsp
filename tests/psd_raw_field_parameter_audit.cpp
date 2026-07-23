#include "s3g_psd_raw_field.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kFrames = 32768u;
constexpr uint32_t kWarmup = 2048u;
using Params = s3g::PsdRawFieldParams;
using Audio = std::array<std::array<float, kFrames>, s3g::kPsdRawFieldChannels>;

struct Render {
    Audio audio {};
    double rms = 0.0;
    double roughness = 0.0;
    double divergence = 0.0;
};

Render render(const Params& params)
{
    Render result;
    std::array<float*, s3g::kPsdRawFieldChannels> outputs {};
    for (uint32_t ch = 0; ch < s3g::kPsdRawFieldChannels; ++ch) {
        outputs[ch] = result.audio[ch].data();
    }

    s3g::PsdRawField field;
    field.prepare(48000.0);
    field.setParams(params);
    field.reset();
    field.process(outputs.data(), s3g::kPsdRawFieldChannels, kFrames);

    double energy = 0.0;
    double delta = 0.0;
    double spread = 0.0;
    uint64_t samples = 0u;
    for (uint32_t ch = 0; ch < s3g::kPsdRawFieldChannels; ++ch) {
        for (uint32_t i = kWarmup; i < kFrames; ++i) {
            const double x = result.audio[ch][i];
            energy += x * x;
            if (i > kWarmup) delta += std::abs(x - result.audio[ch][i - 1u]);
            ++samples;
        }
    }
    for (uint32_t ch = 1; ch < s3g::kPsdRawFieldChannels; ++ch) {
        for (uint32_t i = kWarmup; i < kFrames; ++i) {
            spread += std::abs(result.audio[ch][i] - result.audio[0][i]);
        }
    }
    result.rms = std::sqrt(energy / static_cast<double>(samples));
    result.roughness = delta / static_cast<double>(samples);
    result.divergence = spread / static_cast<double>((kFrames - kWarmup) * (s3g::kPsdRawFieldChannels - 1u));
    return result;
}

struct Difference {
    double normalized = 0.0;
    double correlation = 0.0;
};

Difference compare(const Render& a, const Render& b)
{
    double aa = 0.0;
    double bb = 0.0;
    double ab = 0.0;
    double diff = 0.0;
    for (uint32_t ch = 0; ch < s3g::kPsdRawFieldChannels; ++ch) {
        for (uint32_t i = kWarmup; i < kFrames; ++i) {
            const double x = a.audio[ch][i];
            const double y = b.audio[ch][i];
            aa += x * x;
            bb += y * y;
            ab += x * y;
            const double d = x - y;
            diff += d * d;
        }
    }
    Difference result;
    result.normalized = std::sqrt(diff / std::max(1.0e-20, 0.5 * (aa + bb)));
    result.correlation = ab / std::sqrt(std::max(1.0e-20, aa * bb));
    return result;
}

double dbRatio(double a, double b)
{
    return 20.0 * std::log10(std::max(a, 1.0e-12) / std::max(b, 1.0e-12));
}

struct FloatCase {
    const char* name;
    float Params::*member;
    float low;
    float high;
    std::function<void(Params&)> context;
};

void printCase(const FloatCase& test)
{
    Params low {};
    Params high {};
    if (test.context) {
        test.context(low);
        test.context(high);
    }
    low.*(test.member) = test.low;
    high.*(test.member) = test.high;
    const Render a = render(low);
    const Render b = render(high);
    const Difference d = compare(a, b);
    std::cout << std::left << std::setw(22) << test.name
              << std::right << std::setw(10) << std::fixed << std::setprecision(2) << d.normalized * 100.0
              << std::setw(10) << std::setprecision(3) << d.correlation
              << std::setw(11) << std::setprecision(2) << dbRatio(b.rms, a.rms)
              << std::setw(12) << dbRatio(b.roughness, a.roughness)
              << std::setw(12) << dbRatio(b.divergence, a.divergence) << '\n';
}

template <typename Enum>
void printEnumCase(const char* name, Enum Params::*member, uint32_t count)
{
    Params firstParams {};
    firstParams.*member = static_cast<Enum>(0u);
    const Render first = render(firstParams);
    for (uint32_t mode = 1u; mode < count; ++mode) {
        Params otherParams {};
        otherParams.*member = static_cast<Enum>(mode);
        const Render other = render(otherParams);
        const Difference d = compare(first, other);
        const std::string label = std::string(name) + " 0->" + std::to_string(mode);
        std::cout << std::left << std::setw(22) << label
                  << std::right << std::setw(10) << std::fixed << std::setprecision(2) << d.normalized * 100.0
                  << std::setw(10) << std::setprecision(3) << d.correlation
                  << std::setw(11) << std::setprecision(2) << dbRatio(other.rms, first.rms)
                  << std::setw(12) << dbRatio(other.roughness, first.roughness)
                  << std::setw(12) << dbRatio(other.divergence, first.divergence) << '\n';
    }
}

} // namespace

int main()
{
    const std::vector<FloatCase> cases {
        { "scanRate", &Params::scanRate, 0.0f, 1.0f, {} },
        { "texture", &Params::texture, 0.0f, 1.0f, {} },
        { "geometry", &Params::geometry, 0.0f, 1.0f, {} },
        { "chaos", &Params::chaos, 0.0f, 1.0f, {} },
        { "fold", &Params::fold, 0.0f, 1.0f, {} },
        { "evolve", &Params::evolve, 0.0f, 1.0f, {} },
        { "channelSpread", &Params::channelSpread, 0.0f, 1.0f, {} },
        { "codecRate", &Params::codecRate, 0.0f, 1.0f, {} },
        { "bitDepth", &Params::bitDepth, 2.0f, 16.0f, {} },
        { "codecDamage", &Params::codecDamage, 0.0f, 1.0f, {} },
        { "codecDamage ADPCM", &Params::codecDamage, 0.0f, 1.0f, [](Params& p) { p.codecMode = s3g::PsdRawFieldCodecMode::Adpcm; } },
        { "codecDamage MU-LAW", &Params::codecDamage, 0.0f, 1.0f, [](Params& p) { p.codecMode = s3g::PsdRawFieldCodecMode::MuLaw; } },
        { "codecDamage A-LAW", &Params::codecDamage, 0.0f, 1.0f, [](Params& p) { p.codecMode = s3g::PsdRawFieldCodecMode::ALaw; } },
        { "codecDamage CELP", &Params::codecDamage, 0.0f, 1.0f, [](Params& p) { p.codecMode = s3g::PsdRawFieldCodecMode::CelpScramble; } },
        { "codecDamage DISC", &Params::codecDamage, 0.0f, 1.0f, [](Params& p) { p.codecMode = s3g::PsdRawFieldCodecMode::DiscConceal; } },
        { "codecDamage CVSD", &Params::codecDamage, 0.0f, 1.0f, [](Params& p) { p.codecMode = s3g::PsdRawFieldCodecMode::Cvsd; } },
        { "codecDamage SUBBAND", &Params::codecDamage, 0.0f, 1.0f, [](Params& p) { p.codecMode = s3g::PsdRawFieldCodecMode::SubbandAdpcm; } },
        { "codecDamage TRANS", &Params::codecDamage, 0.0f, 1.0f, [](Params& p) { p.codecMode = s3g::PsdRawFieldCodecMode::BlockTransform; } },
        { "codecDamage FAX", &Params::codecDamage, 0.0f, 1.0f, [](Params& p) { p.codecMode = s3g::PsdRawFieldCodecMode::FaxQam; } },
        { "codecDamage SIGMA", &Params::codecDamage, 0.0f, 1.0f, [](Params& p) { p.codecMode = s3g::PsdRawFieldCodecMode::SigmaOneBit; } },
        { "codecRate CVSD", &Params::codecRate, 0.0f, 0.8f, [](Params& p) { p.codecMode = s3g::PsdRawFieldCodecMode::Cvsd; } },
        { "codecRate SUBBAND", &Params::codecRate, 0.0f, 0.8f, [](Params& p) { p.codecMode = s3g::PsdRawFieldCodecMode::SubbandAdpcm; } },
        { "codecRate FAX", &Params::codecRate, 0.0f, 0.8f, [](Params& p) { p.codecMode = s3g::PsdRawFieldCodecMode::FaxQam; } },
        { "codecRate SIGMA", &Params::codecRate, 0.0f, 0.8f, [](Params& p) { p.codecMode = s3g::PsdRawFieldCodecMode::SigmaOneBit; } },
        { "drive", &Params::drive, 0.0f, 1.0f, {} },
        { "shred", &Params::shred, 0.0f, 1.0f, {} },
        { "resonance", &Params::resonance, 0.0f, 1.0f, {} },
    };

    std::cout << std::left << std::setw(22) << "parameter"
              << std::right << std::setw(10) << "diff %"
              << std::setw(10) << "corr"
              << std::setw(11) << "rms dB"
              << std::setw(12) << "rough dB"
              << std::setw(12) << "space dB" << '\n';
    for (const auto& test : cases) printCase(test);

    printEnumCase("codecMode", &Params::codecMode, s3g::kPsdRawFieldCodecModeCount);
    printEnumCase("channelScheme", &Params::channelScheme, 5u);
    return 0;
}
