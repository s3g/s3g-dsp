#include "s3g_psd_raw_field.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

int main()
{
    constexpr uint32_t frames = 4096u;
    s3g::PsdRawField field;
    s3g::PsdRawFieldParams params {};
    params.scanRate = 0.51f;
    params.texture = 0.78f;
    params.geometry = 0.76f;
    params.chaos = 0.82f;
    params.fold = 0.74f;
    params.codecRate = 0.45f;
    params.bitDepth = 6.0f;
    params.codecDamage = 0.64f;
    params.channelScheme = s3g::PsdRawFieldChannelScheme::Divergent;
    params.channelSpread = 0.82f;
    params.drive = 0.78f;
    params.shred = 0.66f;
    params.resonance = 0.28f;
    params.gainDb = -10.0f;

    std::array<std::array<float, frames>, s3g::kPsdRawFieldChannels> output {};
    std::array<float*, s3g::kPsdRawFieldChannels> pointers {};
    for (uint32_t ch = 0; ch < s3g::kPsdRawFieldChannels; ++ch) {
        pointers[ch] = output[ch].data();
    }

    for (uint32_t mode = 0; mode <= 5; ++mode) {
        params.codecMode = static_cast<s3g::PsdRawFieldCodecMode>(mode);
        field.prepare(48000.0);
        field.setParams(params);
        field.reset();
        if (mode == 5u) field.randomizeByteField(0x12345678u);
        field.process(pointers.data(), s3g::kPsdRawFieldChannels, frames);

        double totalEnergy = 0.0;
        double crossDelta = 0.0;
        float peak = 0.0f;
        for (uint32_t i = 0; i < frames; ++i) {
            const float a = output[0][i];
            const float b = output[1][i];
            if (!std::isfinite(a) || !std::isfinite(b)) {
                std::cerr << "PSD raw field produced non-finite samples in mode " << mode << "\n";
                return 1;
            }
            totalEnergy += static_cast<double>(a) * a;
            crossDelta += std::abs(static_cast<double>(a - b));
            peak = std::max(peak, std::abs(a));
        }

        if (totalEnergy <= 0.0001 || crossDelta <= 0.001 || peak > 1.0f) {
            std::cerr << "PSD raw field smoke failed in mode " << mode
                      << ": energy=" << totalEnergy
                      << " crossDelta=" << crossDelta
                      << " peak=" << peak << "\n";
            return 1;
        }
    }

    constexpr uint32_t morphChannels = s3g::kPsdRawFieldChannels + 2u;
    std::array<std::array<float, frames>, morphChannels> morphOutput {};
    std::array<float*, morphChannels> morphPointers {};
    for (uint32_t ch = 0; ch < morphChannels; ++ch) morphPointers[ch] = morphOutput[ch].data();

    params.codecMode = s3g::PsdRawFieldCodecMode::MuLaw;
    params.channelScheme = s3g::PsdRawFieldChannelScheme::Deinterleave;
    params.evolve = 0.0f;
    s3g::PsdRawFieldMorph morph;
    morph.prepare(48000.0);
    morph.setParams(params);
    morph.reset();
    morph.process(morphPointers.data(), morphChannels, frames);

    s3g::PsdRawField reference;
    reference.prepare(48000.0);
    reference.setParams(params);
    reference.reset();
    reference.process(pointers.data(), s3g::kPsdRawFieldChannels, frames);
    std::array<std::array<float, 1>, s3g::kPsdRawFieldChannels> referenceNext {};
    std::array<float*, s3g::kPsdRawFieldChannels> referenceNextPointers {};
    for (uint32_t ch = 0; ch < s3g::kPsdRawFieldChannels; ++ch) {
        referenceNextPointers[ch] = referenceNext[ch].data();
    }
    reference.process(referenceNextPointers.data(), s3g::kPsdRawFieldChannels, 1u);

    auto nextParams = params;
    nextParams.seed ^= 0xa511e9b3u;
    nextParams.codecMode = s3g::PsdRawFieldCodecMode::CelpScramble;
    nextParams.channelScheme = s3g::PsdRawFieldChannelScheme::Divergent;
    morph.transitionTo(nextParams, 0.02f);
    morph.process(morphPointers.data(), morphChannels, frames);

    if (std::abs(morphOutput[0][0] - referenceNext[0][0]) > 1.0e-6f || morph.transitioning()) {
        std::cerr << "PSD raw field morph did not begin continuously or finish on time\n";
        return 1;
    }
    double morphEnergy = 0.0;
    for (uint32_t ch = 0; ch < s3g::kPsdRawFieldChannels; ++ch) {
        for (uint32_t i = 0; i < frames; ++i) {
            const float value = morphOutput[ch][i];
            if (!std::isfinite(value) || std::abs(value) > 1.0f) {
                std::cerr << "PSD raw field morph produced an invalid sample\n";
                return 1;
            }
            morphEnergy += static_cast<double>(value) * value;
        }
    }
    for (uint32_t ch = s3g::kPsdRawFieldChannels; ch < morphChannels; ++ch) {
        for (float value : morphOutput[ch]) {
            if (value != 0.0f) {
                std::cerr << "PSD raw field morph did not clear surplus channels\n";
                return 1;
            }
        }
    }
    if (morphEnergy <= 0.0001) {
        std::cerr << "PSD raw field morph was silent\n";
        return 1;
    }

    return 0;
}
