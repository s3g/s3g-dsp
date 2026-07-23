#include "s3g_psd_raw_field.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

int main()
{
    constexpr uint32_t frames = 4096u;
    std::vector<uint8_t> rawBytes(100003u);
    for (std::size_t i = 0; i < rawBytes.size(); ++i) {
        rawBytes[i] = static_cast<uint8_t>((i * 73u + (i >> 5u) * 19u) & 0xffu);
    }
    const auto rawSource = s3g::makePsdRawFieldSource(rawBytes.data(), rawBytes.size());
    if (!rawSource || rawSource->loadedByteCount != rawBytes.size()
        || rawSource->originalByteCount != rawBytes.size() || rawSource->truncated
        || rawSource->bytes.size() != 131072u
        || !std::equal(rawBytes.begin(), rawBytes.end(), rawSource->bytes.begin())) {
        std::cerr << "Fault raw source did not preserve the input bytes\n";
        return 1;
    }
    for (std::size_t i = rawBytes.size(); i < rawSource->bytes.size(); ++i) {
        if (rawSource->bytes[i] != rawBytes[i % rawBytes.size()]) {
            std::cerr << "Fault raw source padding did not repeat the original bytes\n";
            return 1;
        }
    }

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

    std::array<uint64_t, s3g::kPsdRawFieldCodecModeCount> profileHashes {};
    std::array<double, s3g::kPsdRawFieldCodecModeCount> profileStepSizes {};
    auto hashGeneratedField = [&](s3g::PsdRawFieldCodecMode profile, s3g::PsdRawFieldCodecMode codec) {
        s3g::PsdRawFieldParams profileParams {};
        profileParams.seed = 0x31415926u;
        profileParams.codecMode = codec;
        profileParams.fieldCodecMode = profile;
        s3g::PsdRawField profileField;
        profileField.prepare(48000.0);
        profileField.setParams(profileParams);
        profileField.reset();
        uint64_t fingerprint = 1469598103934665603ull;
        double stepSize = 0.0;
        uint8_t previous = profileField.byteAt(0u);
        for (uint32_t i = 0u; i < profileField.tapeSize(); ++i) {
            const uint8_t byte = profileField.byteAt(i);
            fingerprint ^= byte;
            fingerprint *= 1099511628211ull;
            if (i > 0u) stepSize += std::abs(static_cast<int>(byte) - static_cast<int>(previous));
            previous = byte;
        }
        return std::pair<uint64_t, double> { fingerprint, stepSize / profileField.tapeSize() };
    };
    for (uint32_t mode = 0u; mode < s3g::kPsdRawFieldCodecModeCount; ++mode) {
        const auto profile = static_cast<s3g::PsdRawFieldCodecMode>(mode);
        const auto result = hashGeneratedField(profile, s3g::PsdRawFieldCodecMode::RawPcm);
        profileHashes[mode] = result.first;
        profileStepSizes[mode] = result.second;
        for (uint32_t previous = 0u; previous < mode; ++previous) {
            if (profileHashes[previous] == profileHashes[mode]) {
                std::cerr << "Fault codec-aware fields collided for profiles "
                          << previous << " and " << mode << "\n";
                return 1;
            }
        }
    }
    const double pcmStep = profileStepSizes[static_cast<uint32_t>(s3g::PsdRawFieldCodecMode::RawPcm)];
    constexpr s3g::PsdRawFieldCodecMode smoothProfiles[] {
        s3g::PsdRawFieldCodecMode::DeltaPcm,
        s3g::PsdRawFieldCodecMode::Adpcm,
        s3g::PsdRawFieldCodecMode::Cvsd,
        s3g::PsdRawFieldCodecMode::Predictive,
        s3g::PsdRawFieldCodecMode::SigmaOneBit,
        s3g::PsdRawFieldCodecMode::HybridPredictive,
    };
    for (const auto profile : smoothProfiles) {
        if (profileStepSizes[static_cast<uint32_t>(profile)] >= pcmStep * 0.72) {
            std::cerr << "Fault predictive field was not sufficiently continuous for profile "
                      << static_cast<uint32_t>(profile) << "\n";
            return 1;
        }
    }

    s3g::PsdRawField profileField;
    s3g::PsdRawFieldParams profileParams {};
    profileParams.seed = 0x31415926u;
    profileParams.codecMode = s3g::PsdRawFieldCodecMode::RawPcm;
    profileParams.fieldCodecMode = s3g::PsdRawFieldCodecMode::ModemFsk;
    profileField.prepare(48000.0);
    profileField.setParams(profileParams);
    profileField.reset();
    uint32_t modemPlateaus = 0u;
    for (uint32_t i = 1u; i < 4096u; ++i) {
        if (profileField.byteAt(i) == profileField.byteAt(i - 1u)) ++modemPlateaus;
    }
    if (modemPlateaus < 3800u) {
        std::cerr << "Fault modem field did not form stable symbol plateaus\n";
        return 1;
    }
    profileParams.codecMode = s3g::PsdRawFieldCodecMode::FaxQam;
    profileField.setParams(profileParams);
    profileField.reset();
    const auto sameProfile = hashGeneratedField(
        s3g::PsdRawFieldCodecMode::ModemFsk, s3g::PsdRawFieldCodecMode::FaxQam);
    if (sameProfile.first != profileHashes[static_cast<uint32_t>(s3g::PsdRawFieldCodecMode::ModemFsk)]) {
        std::cerr << "Fault changed its generated field when only the codec changed\n";
        return 1;
    }
    profileParams.fieldCodecMode = s3g::PsdRawFieldCodecMode::FaxQam;
    profileField.setParams(profileParams);
    profileField.reset();
    for (uint32_t x = 0u; x < 256u; ++x) {
        if (profileField.byteAt(x) != profileField.byteAt(256u + x)) {
            std::cerr << "Fault fax field did not retain repeated scanlines\n";
            return 1;
        }
    }

    s3g::PsdRawField rawField;
    rawField.setSource(rawSource);
    rawField.prepare(48000.0);
    rawField.setParams(params);
    rawField.reset();
    if (rawField.tapeSize() != rawSource->bytes.size()) {
        std::cerr << "Fault raw source tape size was not installed\n";
        return 1;
    }
    rawField.process(pointers.data(), s3g::kPsdRawFieldChannels, frames);
    double rawEnergy = 0.0;
    double rawDifference = 0.0;
    for (uint32_t i = 0; i < frames; ++i) {
        if (!std::isfinite(output[0][i])) {
            std::cerr << "Fault raw source produced non-finite samples\n";
            return 1;
        }
        rawEnergy += static_cast<double>(output[0][i]) * output[0][i];
        rawDifference += std::abs(static_cast<double>(output[0][i] - output[1][i]));
    }
    if (rawEnergy <= 0.0001 || rawDifference <= 0.001) {
        std::cerr << "Fault raw source was silent or collapsed across channels\n";
        return 1;
    }

    std::array<float, frames> pcmReference {};
    std::array<double, s3g::kPsdRawFieldCodecModeCount> modeMeanSquare {};
    for (uint32_t mode = 0; mode < s3g::kPsdRawFieldCodecModeCount; ++mode) {
        params.codecMode = static_cast<s3g::PsdRawFieldCodecMode>(mode);
        field.prepare(48000.0);
        field.setParams(params);
        field.reset();
        if (mode == 5u) field.randomizeByteField(0x12345678u);
        field.process(pointers.data(), s3g::kPsdRawFieldChannels, frames);

        double totalEnergy = 0.0;
        double crossDelta = 0.0;
        double pcmDelta = 0.0;
        float peak = 0.0f;
        for (uint32_t i = 0; i < frames; ++i) {
            for (uint32_t ch = 0; ch < s3g::kPsdRawFieldChannels; ++ch) {
                const float sample = output[ch][i];
                if (!std::isfinite(sample)) {
                    std::cerr << "PSD raw field produced non-finite samples in mode " << mode << "\n";
                    return 1;
                }
                totalEnergy += static_cast<double>(sample) * sample;
                peak = std::max(peak, std::abs(sample));
            }
            crossDelta += std::abs(static_cast<double>(output[0][i] - output[1][i]));
            if (mode > 0u) pcmDelta += std::abs(static_cast<double>(output[0][i] - pcmReference[i]));
        }
        if (mode == 0u) std::copy(output[0].begin(), output[0].end(), pcmReference.begin());
        modeMeanSquare[mode] = totalEnergy
            / static_cast<double>(frames * s3g::kPsdRawFieldChannels);

        if (totalEnergy <= 0.0001 || crossDelta <= 0.001 || peak > 1.0f
            || (mode > 0u && pcmDelta <= 0.05)) {
            std::cerr << "PSD raw field smoke failed in mode " << mode
                      << ": energy=" << totalEnergy
                      << " crossDelta=" << crossDelta
                      << " pcmDelta=" << pcmDelta
                      << " peak=" << peak << "\n";
            return 1;
        }
    }
    const auto modeEnergyBounds = std::minmax_element(
        modeMeanSquare.begin(), modeMeanSquare.end());
    if (*modeEnergyBounds.second > *modeEnergyBounds.first * 16.0) {
        std::cerr << "Fault codec level calibration exceeded a 12 dB cross-mode window: min="
                  << *modeEnergyBounds.first << " max=" << *modeEnergyBounds.second << "\n";
        return 1;
    }

    constexpr std::array<s3g::PsdRawFieldCodecMode, 10> upgradedModes {
        s3g::PsdRawFieldCodecMode::Adpcm,
        s3g::PsdRawFieldCodecMode::MuLaw,
        s3g::PsdRawFieldCodecMode::ALaw,
        s3g::PsdRawFieldCodecMode::CelpScramble,
        s3g::PsdRawFieldCodecMode::DiscConceal,
        s3g::PsdRawFieldCodecMode::Cvsd,
        s3g::PsdRawFieldCodecMode::SubbandAdpcm,
        s3g::PsdRawFieldCodecMode::BlockTransform,
        s3g::PsdRawFieldCodecMode::FaxQam,
        s3g::PsdRawFieldCodecMode::SigmaOneBit,
    };
    std::array<float, frames> lowDamageProfile {};
    std::array<float, frames> highDamageProfile {};
    auto upgradedParams = params;
    upgradedParams.codecRate = 0.42f;
    upgradedParams.bitDepth = 7.0f;
    upgradedParams.drive = 0.0f;
    upgradedParams.shred = 0.0f;
    upgradedParams.resonance = 0.0f;
    for (const auto mode : upgradedModes) {
        upgradedParams.codecMode = mode;
        upgradedParams.codecDamage = 0.0f;
        field.prepare(48000.0);
        field.setParams(upgradedParams);
        field.reset();
        field.process(pointers.data(), s3g::kPsdRawFieldChannels, frames);
        std::copy(output[0].begin(), output[0].end(), lowDamageProfile.begin());

        upgradedParams.codecDamage = 0.86f;
        field.setParams(upgradedParams);
        field.reset();
        field.process(pointers.data(), s3g::kPsdRawFieldChannels, frames);
        std::copy(output[0].begin(), output[0].end(), highDamageProfile.begin());

        double damageDifference = 0.0;
        for (uint32_t i = 0u; i < frames; ++i) {
            damageDifference += std::abs(
                static_cast<double>(highDamageProfile[i] - lowDamageProfile[i]));
        }
        if (damageDifference <= 0.01) {
            std::cerr << "Fault upgraded codec did not respond to DAMAGE in mode "
                      << static_cast<uint32_t>(mode) << "\n";
            return 1;
        }

        field.reset();
        field.process(pointers.data(), s3g::kPsdRawFieldChannels, frames);
        for (uint32_t i = 0u; i < frames; ++i) {
            if (output[0][i] != highDamageProfile[i]) {
                std::cerr << "Fault upgraded codec reset was not deterministic in mode "
                          << static_cast<uint32_t>(mode) << " at sample " << i << "\n";
                return 1;
            }
        }
    }

    auto lowRateParams = params;
    lowRateParams.codecRate = 1.0f;
    lowRateParams.bitDepth = 4.0f;
    lowRateParams.codecDamage = 0.72f;
    lowRateParams.drive = 0.0f;
    lowRateParams.shred = 0.0f;
    lowRateParams.resonance = 0.0f;
    constexpr uint32_t lowRateFrames = 512u;
    for (uint32_t mode = 0; mode < s3g::kPsdRawFieldCodecModeCount; ++mode) {
        lowRateParams.codecMode = static_cast<s3g::PsdRawFieldCodecMode>(mode);
        field.prepare(48000.0);
        field.setParams(lowRateParams);
        field.reset();
        field.process(pointers.data(), s3g::kPsdRawFieldChannels, lowRateFrames);
        double energy = 0.0;
        for (uint32_t ch = 0; ch < s3g::kPsdRawFieldChannels; ++ch) {
            for (uint32_t i = 0; i < lowRateFrames; ++i) {
                const float sample = output[ch][i];
                if (!std::isfinite(sample)) {
                    std::cerr << "Fault low-rate codec produced non-finite samples in mode " << mode << "\n";
                    return 1;
                }
                energy += static_cast<double>(sample) * sample;
            }
        }
        if (energy <= 1.0e-8) {
            std::cerr << "Fault low-rate codec had a silent startup in mode " << mode << "\n";
            return 1;
        }
    }

    constexpr uint32_t waveformFrames = 8192u;
    std::vector<uint8_t> waveformBytes(waveformFrames * s3g::kPsdRawFieldChannels);
    for (uint32_t frame = 0u; frame < waveformFrames; ++frame) {
        for (uint32_t ch = 0u; ch < s3g::kPsdRawFieldChannels; ++ch) {
            const float phase = 2.0f * s3g::kPi * 220.0f * static_cast<float>(frame) / 48000.0f
                + static_cast<float>(ch) * 0.04f;
            waveformBytes[frame * s3g::kPsdRawFieldChannels + ch] = static_cast<uint8_t>(
                std::round((std::sin(phase) * 0.42f + 0.5f) * 255.0f));
        }
    }
    s3g::PsdRawFieldWaveformInfo waveformInfo {};
    waveformInfo.sampleRate = 48000u;
    waveformInfo.channelCount = 2u;
    waveformInfo.bitsPerSample = 24u;
    waveformInfo.sourceFrameCount = waveformFrames;
    waveformInfo.loadedFrameCount = waveformFrames;
    waveformInfo.sourceDataByteCount = waveformFrames * 6u;
    const auto waveformSource = s3g::makePsdRawFieldSource(
        std::move(waveformBytes), waveformFrames * 6u + 128u, waveformInfo);
    if (!waveformSource || !waveformSource->waveform || waveformSource->sourceSampleRate != 48000u
        || waveformSource->sourceChannelCount != 2u || waveformSource->loadedFrameCount != waveformFrames) {
        std::cerr << "Fault waveform source metadata was not retained\n";
        return 1;
    }

    auto waveformParams = params;
    waveformParams.scanRate = 0.5f;
    waveformParams.texture = 0.90f;
    waveformParams.geometry = 0.0f;
    waveformParams.chaos = 0.0f;
    waveformParams.fold = 0.0f;
    waveformParams.evolve = 0.0f;
    waveformParams.channelScheme = s3g::PsdRawFieldChannelScheme::Deinterleave;
    waveformParams.channelSpread = 1.0f;
    waveformParams.codecMode = s3g::PsdRawFieldCodecMode::RawPcm;
    waveformParams.codecRate = 0.0f;
    waveformParams.bitDepth = 16.0f;
    waveformParams.codecDamage = 0.0f;
    waveformParams.drive = 0.0f;
    waveformParams.shred = 0.0f;
    waveformParams.resonance = 0.0f;
    waveformParams.gainDb = -6.0f;
    s3g::PsdRawField waveformField;
    waveformField.setSource(waveformSource);
    waveformField.prepare(48000.0);
    waveformField.setParams(waveformParams);
    waveformField.reset();
    waveformField.process(pointers.data(), s3g::kPsdRawFieldChannels, frames);
    if (std::abs(waveformField.cursorPosition(0u) - static_cast<float>(frames)) > 0.01f) {
        std::cerr << "Fault waveform source did not scan at its native rate\n";
        return 1;
    }
    constexpr uint32_t sinePeriod = 218u;
    double periodicNumerator = 0.0;
    double periodicA = 0.0;
    double periodicB = 0.0;
    for (uint32_t i = 1024u + sinePeriod; i < frames; ++i) {
        const double a = output[0][i];
        const double b = output[0][i - sinePeriod];
        periodicNumerator += a * b;
        periodicA += a * a;
        periodicB += b * b;
    }
    const double periodicCorrelation = periodicNumerator / std::sqrt(periodicA * periodicB + 1.0e-20);
    if (periodicCorrelation < 0.45) {
        std::cerr << "Fault waveform source lost its periodic origin: correlation="
                  << periodicCorrelation << "\n";
        return 1;
    }
    waveformField.setPitchRatio(2.0f);
    waveformField.reset();
    constexpr uint32_t pitchedFrames = 1024u;
    waveformField.process(pointers.data(), s3g::kPsdRawFieldChannels, pitchedFrames);
    if (std::abs(waveformField.cursorPosition(0u) - static_cast<float>(pitchedFrames * 2u)) > 0.01f) {
        std::cerr << "Fault MIDI pitch ratio did not double waveform traversal\n";
        return 1;
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
    morph.transitionToSource(rawSource, nextParams, 0.02f);
    morph.process(morphPointers.data(), morphChannels, frames);

    if (std::abs(morphOutput[0][0] - referenceNext[0][0]) > 1.0e-6f || morph.transitioning()
        || morph.source() != rawSource) {
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
