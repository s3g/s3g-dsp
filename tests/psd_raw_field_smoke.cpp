#include "s3g_psd_raw_field.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

int main()
{
    static_assert(static_cast<uint32_t>(s3g::PsdRawFieldModTarget::Off) == 5u,
        "Fault must preserve legacy modulation target values");
    static_assert(static_cast<uint32_t>(s3g::PsdRawFieldModTarget::Body) == 6u);
    static_assert(static_cast<uint32_t>(s3g::PsdRawFieldModTarget::Ring) == 7u);
    static_assert(static_cast<uint32_t>(s3g::PsdRawFieldModTarget::Strike) == 8u);
    static_assert(static_cast<uint32_t>(s3g::PsdRawFieldModTarget::Fold) == 9u);
    static_assert(static_cast<uint32_t>(s3g::PsdRawFieldModTarget::Scan) == 10u);
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
    std::cout << "Fault fixed-seed field fingerprints:";
    for (const uint64_t fingerprint : profileHashes) {
        std::cout << " 0x" << std::hex << fingerprint;
    }
    std::cout << std::dec << "\n";
    constexpr std::array<uint64_t, s3g::kPsdRawFieldCodecModeCount>
        expectedProfileHashes {
            0x72d69f6c399b42f1ull, 0xea38da46ff3ed2ecull,
            0x6ad00aca1e65bd44ull, 0xa2f72e80bb53d2e2ull,
            0xc91ae8533ff95da1ull, 0x778d9a97c436ed4full,
            0x780bdde1e80a8bd3ull, 0xe1355ff603edeae5ull,
            0x31906d79b9b48374ull, 0x431964cece2d2255ull,
            0xaaddeb696812c088ull, 0xdffccffb1eb6a261ull,
            0x993304b6a38e5ba3ull, 0xea3b7db78bcf8283ull,
            0x91453c2fc26c5116ull, 0x3b5e25c79f205d48ull,
            0x28bd26560ef7d65bull, 0x2f06385984aad7f1ull,
            0x6255b2f9525bf30bull, 0xe584e6d2f03a358bull,
            0x127415931ce46e4dull, 0xc964445de3595383ull,
            0x6353232f8336eb8aull,
        };
    for (uint32_t mode = 0u; mode < profileHashes.size(); ++mode) {
        if (profileHashes[mode] != expectedProfileHashes[mode]) {
            std::cerr << "Fault fixed-seed field fingerprint changed for profile "
                      << mode << ": 0x" << std::hex << profileHashes[mode]
                      << " != 0x" << expectedProfileHashes[mode]
                      << std::dec << "\n";
            return 1;
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
    profileParams.fieldCodecMode = s3g::PsdRawFieldCodecMode::Apt;
    profileField.setParams(profileParams);
    profileField.reset();
    if (profileField.byteAt(0u) >= profileField.byteAt(2u)
        || profileField.byteAt(1040u) <= profileField.byteAt(1042u)
        || profileField.byteAt(0u) != profileField.byteAt(s3g::kPsdRawFieldAptLineWords)) {
        std::cerr << "Fault APT field did not retain its 1040/832 Hz sync-line grammar\n";
        return 1;
    }
    profileParams.codecMode = s3g::PsdRawFieldCodecMode::Apt;
    profileParams.codecRate = std::log2(48000.0f / 4160.0f) / 14.0f;
    profileParams.bitDepth = 8.0f;
    profileParams.codecDamage = 0.0f;
    profileParams.drive = 0.0f;
    profileParams.shred = 0.0f;
    profileParams.resonance = 0.0f;
    profileField.setParams(profileParams);
    profileField.reset();
    profileField.process(pointers.data(), s3g::kPsdRawFieldChannels, frames);
    auto toneEnergy = [&](float frequency) {
        double real = 0.0;
        double imaginary = 0.0;
        for (uint32_t i = 512u; i < frames; ++i) {
            const double phase = 2.0 * static_cast<double>(s3g::kPi)
                * static_cast<double>(frequency) * static_cast<double>(i) / 48000.0;
            real += static_cast<double>(output[0][i]) * std::cos(phase);
            imaginary -= static_cast<double>(output[0][i]) * std::sin(phase);
        }
        const double scale = 1.0 / static_cast<double>(frames - 512u);
        return (real * real + imaginary * imaginary) * scale * scale;
    };
    const double aptCarrierEnergy = toneEnergy(2400.0f);
    const double aptOffCarrierEnergy = std::max(toneEnergy(1800.0f), toneEnergy(3000.0f));
    if (aptCarrierEnergy < 1.0e-4 || aptCarrierEnergy < aptOffCarrierEnergy * 4.0) {
        std::cerr << "Fault APT codec did not retain its 2400 Hz AM subcarrier: carrier="
                  << aptCarrierEnergy << " off=" << aptOffCarrierEnergy << "\n";
        return 1;
    }
    profileParams.carrierTune = 12.0f;
    profileField.setParams(profileParams);
    profileField.reset();
    profileField.process(pointers.data(), s3g::kPsdRawFieldChannels, frames);
    const float raisedCarrierFrequency = 2.0f * (2400.0f
        - 3.5f * 2.4f * profileParams.channelSpread);
    const double raisedCarrierEnergy = toneEnergy(raisedCarrierFrequency);
    const double oldCarrierEnergy = toneEnergy(2400.0f);
    if (raisedCarrierEnergy < 1.0e-4 || raisedCarrierEnergy < oldCarrierEnergy * 4.0) {
        std::cerr << "Fault CARRIER did not transpose APT by one octave: raised="
                  << raisedCarrierEnergy << " old=" << oldCarrierEnergy << "\n";
        return 1;
    }

    // Every M1 source must create a deterministic, finite, audible departure
    // from the unmodulated codec in the backward-compatible Broadcast graph.
    auto modulationParams = profileParams;
    modulationParams.codecMode = s3g::PsdRawFieldCodecMode::HfFax;
    modulationParams.fieldCodecMode = s3g::PsdRawFieldCodecMode::HfFax;
    modulationParams.codecRate = 0.38f;
    modulationParams.codecDamage = 0.18f;
    modulationParams.carrierTune = 0.0f;
    modulationParams.channelSpread = 0.61f;
    modulationParams.modSource = s3g::PsdRawFieldModSource::Off;
    modulationParams.modTarget = s3g::PsdRawFieldModTarget::Data;
    modulationParams.modRate = std::log(73.0f / 0.05f) / std::log(160000.0f);
    modulationParams.modRatio = 1.0f;
    modulationParams.modIndex = 0.82f;
    modulationParams.modFeedback = 0.0f;
    modulationParams.modClockLock = 0u;
    auto renderModulation = [&](const s3g::PsdRawFieldParams& p) {
        field.prepare(48000.0);
        field.setParams(p);
        field.reset();
        field.process(pointers.data(), s3g::kPsdRawFieldChannels, frames);
        return output[0];
    };
    const auto unmodulated = renderModulation(modulationParams);
    for (uint32_t source = 1u; source < s3g::kPsdRawFieldModSourceCount; ++source) {
        auto modulatedParams = modulationParams;
        modulatedParams.modSource = static_cast<s3g::PsdRawFieldModSource>(source);
        if (modulatedParams.modSource == s3g::PsdRawFieldModSource::Feedback) {
            modulatedParams.modFeedback = 0.72f;
        }
        const auto first = renderModulation(modulatedParams);
        double difference = 0.0;
        for (uint32_t i = 0u; i < frames; ++i) {
            if (!std::isfinite(first[i])) {
                std::cerr << "Fault protocol modulator produced a non-finite sample for source "
                          << source << "\n";
                return 1;
            }
            difference += std::abs(static_cast<double>(first[i] - unmodulated[i]));
        }
        if (difference <= 0.01) {
            std::cerr << "Fault protocol modulator source was inaudible: " << source
                      << " difference=" << difference << "\n";
            return 1;
        }
        const auto second = renderModulation(modulatedParams);
        for (uint32_t i = 0u; i < frames; ++i) {
            if (first[i] != second[i]) {
                std::cerr << "Fault protocol modulator reset was not deterministic for source "
                          << source << " at sample " << i << "\n";
                return 1;
            }
        }
    }

    auto bypassParams = modulationParams;
    bypassParams.modSource = s3g::PsdRawFieldModSource::Sine;
    const auto modulationOn = renderModulation(bypassParams);
    bypassParams.modulationEnabled = 0u;
    const auto modulationOff = renderModulation(bypassParams);
    double bypassDifference = 0.0;
    for (uint32_t i = 0u; i < frames; ++i) {
        if (modulationOff[i] != unmodulated[i]) {
            std::cerr << "Fault MOD OFF did not fully bypass the operator graph\n";
            return 1;
        }
        bypassDifference += std::abs(static_cast<double>(modulationOn[i] - modulationOff[i]));
    }
    if (bypassDifference <= 0.01) {
        std::cerr << "Fault MOD ON and MOD OFF were indistinguishable\n";
        return 1;
    }

    // Each destination operates on a different layer of the transmission;
    // OFF must leave the carrier unchanged.
    for (uint32_t target = 0u; target < s3g::kPsdRawFieldModTargetCount; ++target) {
        auto targetedParams = modulationParams;
        targetedParams.modSource = s3g::PsdRawFieldModSource::Sine;
        targetedParams.modTarget = static_cast<s3g::PsdRawFieldModTarget>(target);
        targetedParams.modIndex = 0.86f;
        const auto targeted = renderModulation(targetedParams);
        double difference = 0.0;
        for (uint32_t i = 0u; i < frames; ++i) {
            difference += std::abs(static_cast<double>(targeted[i] - unmodulated[i]));
        }
        const bool isOff = targetedParams.modTarget == s3g::PsdRawFieldModTarget::Off;
        if ((!isOff && difference <= 0.01) || (isOff && difference != 0.0)) {
            std::cerr << "Fault protocol modulation target was inaudible: " << target
                      << " difference=" << difference << "\n";
            return 1;
        }
    }

    // The curated destination layer must turn a hard, stepped source toward a
    // low modal body or a bounded event contour instead of another broadband
    // codec overwrite.
    struct CharacterMetrics {
        double total = 0.0;
        double low = 0.0;
        double high = 0.0;
        double roughness = 0.0;
        double blockCrest = 0.0;
    };
    auto renderCharacter = [](const s3g::PsdRawFieldParams& characterParams) {
        constexpr uint32_t characterFrames = 48000u;
        constexpr uint32_t warmup = 6000u;
        std::array<std::vector<float>, s3g::kPsdRawFieldChannels> characterAudio;
        std::array<float*, s3g::kPsdRawFieldChannels> characterPointers {};
        for (uint32_t ch = 0u; ch < s3g::kPsdRawFieldChannels; ++ch) {
            characterAudio[ch].resize(characterFrames);
            characterPointers[ch] = characterAudio[ch].data();
        }
        s3g::PsdRawField characterField;
        characterField.prepare(48000.0);
        characterField.setParams(characterParams);
        characterField.reset();
        characterField.process(characterPointers.data(), s3g::kPsdRawFieldChannels,
            characterFrames);

        const double lowCoeff = 1.0 - std::exp(-2.0 * s3g::kPi * 250.0 / 48000.0);
        const double highCoeff = 1.0 - std::exp(-2.0 * s3g::kPi * 5000.0 / 48000.0);
        double lowpass = 0.0;
        double highLowpass = 0.0;
        double blockEnergy = 0.0;
        double blockRmsSum = 0.0;
        double blockRmsMaximum = 0.0;
        uint32_t blockSamples = 0u;
        uint32_t blocks = 0u;
        CharacterMetrics metrics;
        for (uint32_t i = warmup; i < characterFrames; ++i) {
            const double x = characterAudio[0][i];
            lowpass += (x - lowpass) * lowCoeff;
            highLowpass += (x - highLowpass) * highCoeff;
            const double high = x - highLowpass;
            metrics.total += x * x;
            metrics.low += lowpass * lowpass;
            metrics.high += high * high;
            if (i > warmup) {
                const double delta = x - characterAudio[0][i - 1u];
                metrics.roughness += delta * delta;
            }
            blockEnergy += x * x;
            if (++blockSamples == 256u) {
                const double rms = std::sqrt(blockEnergy / 256.0);
                blockRmsSum += rms;
                blockRmsMaximum = std::max(blockRmsMaximum, rms);
                blockEnergy = 0.0;
                blockSamples = 0u;
                ++blocks;
            }
        }
        metrics.blockCrest = blockRmsMaximum
            / std::max(1.0e-12, blockRmsSum / static_cast<double>(std::max(1u, blocks)));
        return metrics;
    };

    auto bodyCharacter = modulationParams;
    bodyCharacter.codecMode = s3g::PsdRawFieldCodecMode::HfFax;
    bodyCharacter.fieldCodecMode = s3g::PsdRawFieldCodecMode::HfFax;
    bodyCharacter.scanRate = 0.28f;
    bodyCharacter.texture = 0.28f;
    bodyCharacter.geometry = 0.55f;
    bodyCharacter.chaos = 0.24f;
    bodyCharacter.fold = 0.10f;
    bodyCharacter.codecRate = 0.34f;
    bodyCharacter.bitDepth = 9.0f;
    bodyCharacter.codecDamage = 0.05f;
    bodyCharacter.drive = 0.45f;
    bodyCharacter.shred = 0.06f;
    bodyCharacter.resonance = 0.78f;
    bodyCharacter.gainDb = -12.0f;
    bodyCharacter.modSource = s3g::PsdRawFieldModSource::Noise;
    bodyCharacter.modTarget = s3g::PsdRawFieldModTarget::Body;
    bodyCharacter.modRate = 0.60f;
    bodyCharacter.modIndex = 0.68f;
    bodyCharacter.modFeedback = 0.0f;
    const CharacterMetrics bodyMetrics = renderCharacter(bodyCharacter);
    auto carrierCharacter = bodyCharacter;
    carrierCharacter.modTarget = s3g::PsdRawFieldModTarget::Carrier;
    const CharacterMetrics carrierMetrics = renderCharacter(carrierCharacter);
    const double bodyLowShare = bodyMetrics.low / std::max(1.0e-12, bodyMetrics.total);
    const double carrierLowShare = carrierMetrics.low / std::max(1.0e-12, carrierMetrics.total);
    const double bodyHighShare = bodyMetrics.high / std::max(1.0e-12, bodyMetrics.total);
    const double carrierHighShare = carrierMetrics.high / std::max(1.0e-12, carrierMetrics.total);
    const double bodyRoughness = bodyMetrics.roughness / std::max(1.0e-12, bodyMetrics.total);
    const double carrierRoughness = carrierMetrics.roughness / std::max(1.0e-12, carrierMetrics.total);
    if (bodyLowShare < carrierLowShare * 5.0
        || bodyHighShare > carrierHighShare * 0.82
        || bodyRoughness > carrierRoughness * 0.85) {
        std::cerr << "Fault BODY did not turn stepped carrier modulation into a low resonance: low="
                  << bodyLowShare << "/" << carrierLowShare
                  << " high=" << bodyHighShare << "/" << carrierHighShare
                  << " rough=" << bodyRoughness << "/" << carrierRoughness << "\n";
        return 1;
    }

    auto strikeCharacter = bodyCharacter;
    strikeCharacter.codecMode = s3g::PsdRawFieldCodecMode::MuLaw;
    strikeCharacter.fieldCodecMode = s3g::PsdRawFieldCodecMode::MuLaw;
    strikeCharacter.resonance = 0.42f;
    strikeCharacter.modSource = s3g::PsdRawFieldModSource::Gate;
    strikeCharacter.modTarget = s3g::PsdRawFieldModTarget::Strike;
    strikeCharacter.modRate = 0.40f;
    const CharacterMetrics strikeMetrics = renderCharacter(strikeCharacter);
    strikeCharacter.modulationEnabled = 0u;
    const CharacterMetrics strikeOffMetrics = renderCharacter(strikeCharacter);
    const double strikeHighShare = strikeMetrics.high / std::max(1.0e-12, strikeMetrics.total);
    const double strikeOffHighShare = strikeOffMetrics.high / std::max(1.0e-12, strikeOffMetrics.total);
    if (strikeMetrics.blockCrest < strikeOffMetrics.blockCrest * 1.25
        || strikeHighShare > strikeOffHighShare * 1.15) {
        std::cerr << "Fault STRIKE did not create bounded percussive dynamics: crest="
                  << strikeMetrics.blockCrest << "/" << strikeOffMetrics.blockCrest
                  << " high=" << strikeHighShare << "/" << strikeOffHighShare << "\n";
        return 1;
    }

    auto modalStressParams = bodyCharacter;
    modalStressParams.modAlgorithm = s3g::PsdRawFieldModAlgorithm::Regenerator;
    modalStressParams.modSource = s3g::PsdRawFieldModSource::Noise;
    modalStressParams.modTarget = s3g::PsdRawFieldModTarget::Body;
    modalStressParams.modIndex = 1.0f;
    modalStressParams.modFeedback = 0.98f;
    modalStressParams.modSource2 = s3g::PsdRawFieldModSource::Feedback;
    modalStressParams.modTarget2 = s3g::PsdRawFieldModTarget::Ring;
    modalStressParams.modIndex2 = 1.0f;
    modalStressParams.modFeedback2 = 0.98f;
    modalStressParams.modSource3 = s3g::PsdRawFieldModSource::Sync;
    modalStressParams.modTarget3 = s3g::PsdRawFieldModTarget::Strike;
    modalStressParams.modIndex3 = 1.0f;
    modalStressParams.modFeedback3 = 0.98f;
    modalStressParams.drive = 1.0f;
    modalStressParams.shred = 1.0f;
    modalStressParams.resonance = 1.0f;
    modalStressParams.bassBody = 1.0f;
    modalStressParams.bassPunch = 1.0f;
    modalStressParams.bassTrace = 0.0f;
    modalStressParams.bassPitchTracking = s3g::PsdRawFieldPitchTracking::BodyAndScan;
    modalStressParams.bassGlide = 1.0f;
    modalStressParams.bassOctave = s3g::PsdRawFieldBassOctave::Unison;
    modalStressParams.bassLowWidth = 0.0f;
    modalStressParams.bassFuzz = 1.0f;
    modalStressParams.bassMetal = 1.0f;
    modalStressParams.bassFeedback = 1.0f;
    constexpr uint32_t modalStressFrames = 8192u;
    for (const double stressRate : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
        for (uint32_t receiver = 0u; receiver < s3g::kPsdRawFieldBassReceiverCount; ++receiver) {
            modalStressParams.bassReceiver = static_cast<s3g::PsdRawFieldBassReceiver>(receiver);
            std::array<std::array<float, modalStressFrames>, s3g::kPsdRawFieldChannels> stressAudio {};
            std::array<float*, s3g::kPsdRawFieldChannels> stressPointers {};
            for (uint32_t ch = 0u; ch < s3g::kPsdRawFieldChannels; ++ch) {
                stressPointers[ch] = stressAudio[ch].data();
            }
            s3g::PsdRawField modalStress;
            modalStress.prepare(stressRate);
            modalStress.setParams(modalStressParams);
            modalStress.setPitchRatio(64.0f);
            modalStress.reset();
            modalStress.triggerBassExcite(1.0f);
            modalStress.process(stressPointers.data(), s3g::kPsdRawFieldChannels,
                modalStressFrames);
            const auto reference = stressAudio[0];
            for (const auto& channel : stressAudio) {
                for (const float sample : channel) {
                    if (!std::isfinite(sample) || std::abs(sample) > 1.0f) {
                        std::cerr << "Fault bass/modal stress escaped its finite bounds at "
                                  << stressRate << " Hz in receiver " << receiver << "\n";
                        return 1;
                    }
                }
            }
            modalStress.reset();
            modalStress.triggerBassExcite(1.0f);
            modalStress.process(stressPointers.data(), s3g::kPsdRawFieldChannels,
                modalStressFrames);
            if (!std::equal(reference.begin(), reference.end(), stressAudio[0].begin())) {
                std::cerr << "Fault bass/modal reset was not deterministic at "
                          << stressRate << " Hz in receiver " << receiver << "\n";
                return 1;
            }
        }
    }

    // The bass core is an opt-in reconstruction layer. Its neutral settings,
    // and TRACE at full Fault, must preserve the pre-bass signal exactly.
    auto compatibilityParams = modulationParams;
    compatibilityParams.codecMode = s3g::PsdRawFieldCodecMode::HfFax;
    compatibilityParams.fieldCodecMode = s3g::PsdRawFieldCodecMode::HfFax;
    compatibilityParams.modSource = s3g::PsdRawFieldModSource::Off;
    compatibilityParams.modSource2 = s3g::PsdRawFieldModSource::Off;
    compatibilityParams.modSource3 = s3g::PsdRawFieldModSource::Off;
    compatibilityParams.modIndex = 0.0f;
    compatibilityParams.modIndex2 = 0.0f;
    compatibilityParams.modIndex3 = 0.0f;
    compatibilityParams.bassBody = 0.0f;
    compatibilityParams.bassPunch = 0.0f;
    compatibilityParams.bassTrace = 1.0f;
    compatibilityParams.bassLowWidth = 1.0f;
    const auto compatibilityReference = renderModulation(compatibilityParams);
    auto neutralCoreParams = compatibilityParams;
    neutralCoreParams.bassReceiver = s3g::PsdRawFieldBassReceiver::Error;
    neutralCoreParams.bassTrace = 0.0f;
    neutralCoreParams.bassPitchTracking = s3g::PsdRawFieldPitchTracking::BodyAndScan;
    neutralCoreParams.bassGlide = 1.0f;
    neutralCoreParams.bassOctave = s3g::PsdRawFieldBassOctave::Unison;
    neutralCoreParams.bassLowWidth = 0.0f;
    const auto neutralCore = renderModulation(neutralCoreParams);
    auto fullTraceParams = compatibilityParams;
    fullTraceParams.bassReceiver = s3g::PsdRawFieldBassReceiver::Divide;
    fullTraceParams.bassBody = 1.0f;
    fullTraceParams.bassTrace = 1.0f;
    fullTraceParams.bassPitchTracking = s3g::PsdRawFieldPitchTracking::Body;
    fullTraceParams.bassOctave = s3g::PsdRawFieldBassOctave::MinusTwo;
    fullTraceParams.bassFuzz = 1.0f;
    fullTraceParams.bassMetal = 1.0f;
    fullTraceParams.bassFeedback = 1.0f;
    const auto fullTrace = renderModulation(fullTraceParams);
    auto modulatedCompatibilityParams = modulationParams;
    modulatedCompatibilityParams.codecMode = s3g::PsdRawFieldCodecMode::HfFax;
    modulatedCompatibilityParams.fieldCodecMode = s3g::PsdRawFieldCodecMode::HfFax;
    modulatedCompatibilityParams.modSource = s3g::PsdRawFieldModSource::Sine;
    modulatedCompatibilityParams.modTarget = s3g::PsdRawFieldModTarget::Body;
    modulatedCompatibilityParams.modSource2 = s3g::PsdRawFieldModSource::Hellschreiber;
    modulatedCompatibilityParams.modTarget2 = s3g::PsdRawFieldModTarget::Ring;
    modulatedCompatibilityParams.modSource3 = s3g::PsdRawFieldModSource::Gate;
    modulatedCompatibilityParams.modTarget3 = s3g::PsdRawFieldModTarget::Strike;
    modulatedCompatibilityParams.bassBody = 0.0f;
    modulatedCompatibilityParams.bassPunch = 0.0f;
    modulatedCompatibilityParams.bassTrace = 1.0f;
    modulatedCompatibilityParams.bassLowWidth = 1.0f;
    const auto modulatedCompatibilityReference = renderModulation(
        modulatedCompatibilityParams);
    auto protectedTraceParams = modulatedCompatibilityParams;
    protectedTraceParams.bassReceiver = s3g::PsdRawFieldBassReceiver::Error;
    protectedTraceParams.bassBody = 1.0f;
    protectedTraceParams.bassPunch = 1.0f;
    protectedTraceParams.bassLowWidth = 0.0f;
    protectedTraceParams.bassPitchTracking = s3g::PsdRawFieldPitchTracking::BodyAndScan;
    protectedTraceParams.bassOctave = s3g::PsdRawFieldBassOctave::Unison;
    protectedTraceParams.bassFuzz = 1.0f;
    protectedTraceParams.bassMetal = 1.0f;
    protectedTraceParams.bassFeedback = 1.0f;
    const auto protectedTrace = renderModulation(protectedTraceParams);
    auto neutralHighGainParams = protectedTraceParams;
    neutralHighGainParams.bassTrace = 0.0f;
    neutralHighGainParams.bassFuzz = 0.0f;
    neutralHighGainParams.bassFeedback = 0.0f;
    neutralHighGainParams.bassMetal = 0.0f;
    const auto neutralHighGainDark = renderModulation(neutralHighGainParams);
    neutralHighGainParams.bassMetal = 1.0f;
    const auto neutralHighGainBright = renderModulation(neutralHighGainParams);
    for (uint32_t i = 0u; i < frames; ++i) {
        if (neutralCore[i] != compatibilityReference[i]
            || fullTrace[i] != compatibilityReference[i]
            || protectedTrace[i] != modulatedCompatibilityReference[i]
            || neutralHighGainDark[i] != neutralHighGainBright[i]) {
            std::cerr << "Fault bass compatibility path changed neutral or fully protected TRACE=1 output\n";
            return 1;
        }
    }

    struct BassRender {
        std::array<std::vector<float>, s3g::kPsdRawFieldChannels> audio;
        double cursorAdvance = 0.0;
    };
    auto renderBass = [](const s3g::PsdRawFieldParams& bassParams,
                         float pitchRatio, uint32_t sampleCount,
                         bool triggerPunch, bool suppressProtocolPunch) {
        BassRender result;
        std::array<float*, s3g::kPsdRawFieldChannels> bassPointers {};
        for (uint32_t ch = 0u; ch < s3g::kPsdRawFieldChannels; ++ch) {
            result.audio[ch].resize(sampleCount);
            bassPointers[ch] = result.audio[ch].data();
        }
        s3g::PsdRawField bassField;
        bassField.prepare(48000.0);
        bassField.setParams(bassParams);
        bassField.setPitchRatio(pitchRatio);
        bassField.reset();
        const double cursorStart = bassField.cursorPosition(0u);
        if (triggerPunch) bassField.triggerBassExcite(1.0f);
        std::vector<float> fixedEnvelope;
        if (suppressProtocolPunch) fixedEnvelope.assign(sampleCount, 1.0f);
        bassField.process(bassPointers.data(), s3g::kPsdRawFieldChannels,
            sampleCount, suppressProtocolPunch ? fixedEnvelope.data() : nullptr);
        result.cursorAdvance = bassField.cursorPosition(0u) - cursorStart;
        return result;
    };
    auto dominantBassFrequency = [](const BassRender& rendered, uint32_t warmup,
                                    float minimum, float maximum) {
        double bestEnergy = -1.0;
        float bestFrequency = minimum;
        for (float frequency = minimum; frequency <= maximum; frequency += 0.5f) {
            const double coefficient = 2.0 * std::cos(2.0 * s3g::kPi
                * static_cast<double>(frequency) / 48000.0);
            double q1 = 0.0;
            double q2 = 0.0;
            for (uint32_t i = warmup; i < rendered.audio[0].size(); ++i) {
                double mean = 0.0;
                for (uint32_t ch = 0u; ch < s3g::kPsdRawFieldChannels; ++ch) {
                    mean += rendered.audio[ch][i];
                }
                mean /= static_cast<double>(s3g::kPsdRawFieldChannels);
                const double q0 = mean + coefficient * q1 - q2;
                q2 = q1;
                q1 = q0;
            }
            const double energy = q1 * q1 + q2 * q2 - coefficient * q1 * q2;
            if (energy > bestEnergy) {
                bestEnergy = energy;
                bestFrequency = frequency;
            }
        }
        return bestFrequency;
    };

    auto bassParams = compatibilityParams;
    bassParams.scanRate = 0.22f;
    bassParams.texture = 0.26f;
    bassParams.geometry = 0.34f;
    bassParams.chaos = 0.12f;
    bassParams.fold = 0.0f;
    bassParams.codecMode = s3g::PsdRawFieldCodecMode::MuLaw;
    bassParams.fieldCodecMode = s3g::PsdRawFieldCodecMode::MuLaw;
    bassParams.codecRate = 0.30f;
    bassParams.codecDamage = 0.12f;
    bassParams.drive = 0.0f;
    bassParams.shred = 0.0f;
    bassParams.resonance = 0.76f;
    bassParams.channelSpread = 0.88f;
    bassParams.gainDb = -12.0f;
    bassParams.bassReceiver = s3g::PsdRawFieldBassReceiver::Direct;
    bassParams.bassBody = 1.0f;
    bassParams.bassPunch = 0.0f;
    bassParams.bassTrace = 0.0f;
    bassParams.bassGlide = 0.0f;
    bassParams.bassOctave = s3g::PsdRawFieldBassOctave::MinusTwo;
    bassParams.bassLowWidth = 0.0f;
    constexpr uint32_t bassFrames = 24576u;
    constexpr uint32_t bassWarmup = 4096u;

    bassParams.bassPitchTracking = s3g::PsdRawFieldPitchTracking::Scan;
    const BassRender scanOne = renderBass(bassParams, 1.0f, bassFrames, false, true);
    const BassRender scanTwo = renderBass(bassParams, 2.0f, bassFrames, false, true);
    bassParams.bassPitchTracking = s3g::PsdRawFieldPitchTracking::Body;
    const BassRender bodyOne = renderBass(bassParams, 1.0f, bassFrames, false, true);
    const BassRender bodyTwo = renderBass(bassParams, 2.0f, bassFrames, false, true);
    bassParams.bassPitchTracking = s3g::PsdRawFieldPitchTracking::BodyAndScan;
    const BassRender bodyScanTwo = renderBass(bassParams, 2.0f, bassFrames, false, true);
    const float scanFrequencyOne = dominantBassFrequency(scanOne, bassWarmup, 48.0f, 150.0f);
    const float scanFrequencyTwo = dominantBassFrequency(scanTwo, bassWarmup, 48.0f, 150.0f);
    const float bodyFrequencyOne = dominantBassFrequency(bodyOne, bassWarmup, 48.0f, 150.0f);
    const float bodyFrequencyTwo = dominantBassFrequency(bodyTwo, bassWarmup, 48.0f, 150.0f);
    const float bodyScanFrequencyTwo = dominantBassFrequency(bodyScanTwo, bassWarmup, 48.0f, 150.0f);
    const double scanPitchRatio = scanFrequencyTwo / std::max(1.0f, scanFrequencyOne);
    const double bodyPitchRatio = bodyFrequencyTwo / std::max(1.0f, bodyFrequencyOne);
    const double bodyScanPitchRatio = bodyScanFrequencyTwo / std::max(1.0f, bodyFrequencyOne);
    // BODY selects the receiver divider/filter target, but the result remains
    // clocked by codec crossings rather than an autonomous pitch-perfect sine.
    // SCAN may therefore pull the measured sub slightly as it changes the
    // material feeding the receiver, while BODY must retain the strong keying
    // relationship.
    if (scanPitchRatio < 0.75 || scanPitchRatio > 1.25
        || bodyPitchRatio < 1.65 || bodyPitchRatio > 2.60
        || bodyScanPitchRatio < 1.65 || bodyScanPitchRatio > 2.60
        || std::abs(bodyTwo.cursorAdvance - bodyOne.cursorAdvance) > 0.001
        || scanTwo.cursorAdvance < scanOne.cursorAdvance * 1.90
        || scanTwo.cursorAdvance > scanOne.cursorAdvance * 2.10
        || bodyScanTwo.cursorAdvance < bodyOne.cursorAdvance * 1.90
        || bodyScanTwo.cursorAdvance > bodyOne.cursorAdvance * 2.10) {
        std::cerr << "Fault receiver-derived bass tracking did not separate SCAN, BODY, and BODY + SCAN: "
                  << "frequency=" << scanPitchRatio << "/" << bodyPitchRatio
                  << "/" << bodyScanPitchRatio << " cursor="
                  << scanOne.cursorAdvance << "/" << scanTwo.cursorAdvance << "/"
                  << bodyOne.cursorAdvance << "/" << bodyTwo.cursorAdvance << "/"
                  << bodyScanTwo.cursorAdvance << "\n";
        return 1;
    }

    struct BassMetrics {
        double total = 0.0;
        double low = 0.0;
        double lowDivergence = 0.0;
        float peak = 0.0f;
    };
    auto bassMetrics = [](const BassRender& rendered, uint32_t warmup) {
        BassMetrics metrics;
        std::array<double, s3g::kPsdRawFieldChannels> lowState {};
        const double coefficient = 1.0 - std::exp(-2.0 * s3g::kPi * 90.0 / 48000.0);
        for (uint32_t i = warmup; i < rendered.audio[0].size(); ++i) {
            for (uint32_t ch = 0u; ch < s3g::kPsdRawFieldChannels; ++ch) {
                const double sample = rendered.audio[ch][i];
                metrics.total += sample * sample;
                metrics.peak = std::max(metrics.peak, std::abs(static_cast<float>(sample)));
                lowState[ch] += (sample - lowState[ch]) * coefficient;
                metrics.low += lowState[ch] * lowState[ch];
            }
            for (uint32_t ch = 1u; ch < s3g::kPsdRawFieldChannels; ++ch) {
                const double difference = lowState[ch] - lowState[0];
                metrics.lowDivergence += difference * difference;
            }
        }
        return metrics;
    };

    struct BassCharacterMetrics {
        double total = 0.0;
        double low = 0.0;
        double upper = 0.0;
    };
    auto bassCharacterMetrics = [](const BassRender& rendered, uint32_t warmup) {
        BassCharacterMetrics metrics;
        double lowState = 0.0;
        double upperState = 0.0;
        const double lowCoefficient = 1.0
            - std::exp(-2.0 * s3g::kPi * 180.0 / 48000.0);
        const double upperCoefficient = 1.0
            - std::exp(-2.0 * s3g::kPi * 1400.0 / 48000.0);
        for (uint32_t i = 0u; i < rendered.audio[0].size(); ++i) {
            double sample = 0.0;
            for (uint32_t ch = 0u; ch < s3g::kPsdRawFieldChannels; ++ch) {
                sample += rendered.audio[ch][i];
            }
            sample /= static_cast<double>(s3g::kPsdRawFieldChannels);
            lowState += (sample - lowState) * lowCoefficient;
            upperState += (sample - upperState) * upperCoefficient;
            if (i >= warmup) {
                const double upper = sample - upperState;
                metrics.total += sample * sample;
                metrics.low += lowState * lowState;
                metrics.upper += upper * upper;
            }
        }
        return metrics;
    };

    // The active bass path must remain exactly backward compatible while the
    // new controls are neutral. FUZZ then adds nonlinear upper-band energy,
    // while METAL must provide a genuinely different contour without moving
    // the protected fundamental.
    auto highGainParams = bassParams;
    highGainParams.bassPitchTracking = s3g::PsdRawFieldPitchTracking::Body;
    highGainParams.bassReceiver = s3g::PsdRawFieldBassReceiver::Direct;
    highGainParams.bassBody = 1.0f;
    highGainParams.bassTrace = 0.0f;
    highGainParams.bassLowWidth = 0.0f;
    highGainParams.bassFuzz = 0.0f;
    highGainParams.bassFeedback = 0.0f;
    highGainParams.bassMetal = 0.0f;
    const BassRender cleanBassDark = renderBass(
        highGainParams, 1.0f, bassFrames, false, true);
    highGainParams.bassMetal = 1.0f;
    const BassRender cleanBassBright = renderBass(
        highGainParams, 1.0f, bassFrames, false, true);
    for (uint32_t ch = 0u; ch < s3g::kPsdRawFieldChannels; ++ch) {
        if (!std::equal(cleanBassDark.audio[ch].begin(), cleanBassDark.audio[ch].end(),
                cleanBassBright.audio[ch].begin())) {
            std::cerr << "Fault METAL changed the active bass path while FUZZ and FEEDBACK were neutral\n";
            return 1;
        }
    }

    highGainParams.bassFuzz = 0.82f;
    highGainParams.bassMetal = 0.10f;
    const BassRender fuzzBass = renderBass(
        highGainParams, 1.0f, bassFrames, false, true);
    highGainParams.bassMetal = 0.90f;
    const BassRender metalBass = renderBass(
        highGainParams, 1.0f, bassFrames, false, true);
    for (const BassRender* rendered : { &cleanBassDark, &fuzzBass, &metalBass }) {
        for (const auto& channel : rendered->audio) {
            for (const float sample : channel) {
                if (!std::isfinite(sample) || std::abs(sample) > 1.0f) {
                    std::cerr << "Fault bass high-gain character escaped its finite bounds\n";
                    return 1;
                }
            }
        }
    }
    const BassCharacterMetrics cleanCharacter = bassCharacterMetrics(
        cleanBassDark, bassWarmup);
    const BassCharacterMetrics fuzzCharacter = bassCharacterMetrics(
        fuzzBass, bassWarmup);
    const BassCharacterMetrics metalCharacter = bassCharacterMetrics(
        metalBass, bassWarmup);
    double fuzzDifference = 0.0;
    double metalDifference = 0.0;
    for (uint32_t i = bassWarmup; i < bassFrames; ++i) {
        fuzzDifference += std::abs(static_cast<double>(
            fuzzBass.audio[0][i] - cleanBassDark.audio[0][i]));
        metalDifference += std::abs(static_cast<double>(
            metalBass.audio[0][i] - fuzzBass.audio[0][i]));
    }
    const float cleanFundamental = dominantBassFrequency(
        cleanBassDark, bassWarmup, 48.0f, 150.0f);
    const float fuzzFundamental = dominantBassFrequency(
        fuzzBass, bassWarmup, 48.0f, 150.0f);
    const float metalFundamental = dominantBassFrequency(
        metalBass, bassWarmup, 48.0f, 150.0f);
    const double cleanUpperShare = cleanCharacter.upper
        / std::max(1.0e-12, cleanCharacter.total);
    const double fuzzUpperShare = fuzzCharacter.upper
        / std::max(1.0e-12, fuzzCharacter.total);
    const double metalUpperShare = metalCharacter.upper
        / std::max(1.0e-12, metalCharacter.total);
    if (fuzzDifference <= 0.01 || metalDifference <= 0.01
        || fuzzUpperShare <= cleanUpperShare
        || metalUpperShare <= fuzzUpperShare
        || fuzzCharacter.low <= cleanCharacter.low * 0.05
        || metalCharacter.low <= cleanCharacter.low * 0.05
        || std::abs(fuzzFundamental - cleanFundamental) > 4.0f
        || std::abs(metalFundamental - cleanFundamental) > 4.0f) {
        std::cerr << "Fault bass high-gain controls did not create distinct, pitch-stable spectra: "
                  << "difference=" << fuzzDifference << "/" << metalDifference
                  << " upper=" << cleanUpperShare << "/" << fuzzUpperShare
                  << "/" << metalUpperShare << " low=" << cleanCharacter.low
                  << "/" << fuzzCharacter.low << "/" << metalCharacter.low
                  << " fundamental=" << cleanFundamental << "/" << fuzzFundamental
                  << "/" << metalFundamental << "\n";
        return 1;
    }

    // Exercise the regeneration cell in isolation with a finite burst. The
    // feedback setting must create a longer, still-decaying tail, stay bounded,
    // and reproduce its output exactly after reset.
    constexpr uint32_t fuzzCoreFrames = 24000u;
    constexpr uint32_t fuzzCoreExcitationFrames = 640u;
    auto renderFuzzCore = [](s3g::PsdRawFieldBassFuzzCore& core,
                             float fuzz, float metal, float feedback) {
        std::array<float, fuzzCoreFrames> rendered {};
        s3g::PsdRawFieldBassFuzzCoefficients coefficients;
        const auto onePole = [](float frequency) {
            return 1.0f - std::exp(-2.0f * s3g::kPi * frequency / 48000.0f);
        };
        coefficients.split = onePole(105.0f);
        coefficients.postLow = onePole(650.0f);
        coefficients.postHigh = onePole(3200.0f);
        coefficients.loopLowpass = onePole(7200.0f);
        coefficients.delaySamples = 48000.0f / 1180.0f;
        core.reset();
        for (uint32_t i = 0u; i < fuzzCoreFrames; ++i) {
            const float gate = i < fuzzCoreExcitationFrames
                ? 1.0f - static_cast<float>(i) / fuzzCoreExcitationFrames : 0.0f;
            const float input = gate * (0.32f * std::sin(2.0f * s3g::kPi
                    * 82.0f * static_cast<float>(i) / 48000.0f)
                + 0.18f * std::sin(2.0f * s3g::kPi
                    * 670.0f * static_cast<float>(i) / 48000.0f));
            rendered[i] = core.processSample(
                input, fuzz, metal, feedback, 0.72f, coefficients);
        }
        return rendered;
    };
    s3g::PsdRawFieldBassFuzzCore feedbackCore;
    feedbackCore.prepare(48000.0);
    const auto feedbackOffTail = renderFuzzCore(feedbackCore, 0.20f, 0.30f, 0.0f);
    const auto feedbackOnTail = renderFuzzCore(feedbackCore, 0.20f, 0.30f, 0.30f);
    const auto feedbackResetTail = renderFuzzCore(feedbackCore, 0.20f, 0.30f, 0.30f);
    double offTailEnergy = 0.0;
    double onTailEnergy = 0.0;
    double lateTailEnergy = 0.0;
    float feedbackPeak = 0.0f;
    for (uint32_t i = 0u; i < fuzzCoreFrames; ++i) {
        if (!std::isfinite(feedbackOnTail[i])) {
            std::cerr << "Fault bass feedback cell produced a non-finite sample\n";
            return 1;
        }
        feedbackPeak = std::max(feedbackPeak, std::abs(feedbackOnTail[i]));
        if (i >= 2048u && i < 8192u) {
            offTailEnergy += static_cast<double>(feedbackOffTail[i]) * feedbackOffTail[i];
            onTailEnergy += static_cast<double>(feedbackOnTail[i]) * feedbackOnTail[i];
        }
        if (i >= 18000u) {
            lateTailEnergy += static_cast<double>(feedbackOnTail[i]) * feedbackOnTail[i];
        }
    }
    if (feedbackOnTail != feedbackResetTail
        || onTailEnergy <= offTailEnergy * 1.20 + 1.0e-12
        || lateTailEnergy >= onTailEnergy * 0.10
        || feedbackPeak > 2.5f) {
        std::cerr << "Fault bass feedback tail was not longer, decaying, bounded, and reset-deterministic: "
                  << "energy=" << offTailEnergy << "/" << onTailEnergy
                  << "/" << lateTailEnergy << " peak=" << feedbackPeak << "\n";
        return 1;
    }

    auto receiverParams = bassParams;
    receiverParams.bassPitchTracking = s3g::PsdRawFieldPitchTracking::Body;
    receiverParams.bassBody = 0.86f;
    receiverParams.bassTrace = 0.18f;
    receiverParams.bassLowWidth = 0.22f;
    receiverParams.codecMode = s3g::PsdRawFieldCodecMode::HfFax;
    receiverParams.fieldCodecMode = s3g::PsdRawFieldCodecMode::HfFax;
    receiverParams.codecDamage = 0.46f;
    std::array<BassRender, s3g::kPsdRawFieldBassReceiverCount> receiverAudio;
    for (uint32_t receiver = 0u; receiver < s3g::kPsdRawFieldBassReceiverCount; ++receiver) {
        receiverParams.bassReceiver = static_cast<s3g::PsdRawFieldBassReceiver>(receiver);
        receiverAudio[receiver] = renderBass(receiverParams, 1.0f, 16384u, false, true);
        const BassMetrics metrics = bassMetrics(receiverAudio[receiver], 2048u);
        if (metrics.total <= 1.0e-5 || metrics.low <= 1.0e-6 || metrics.peak > 1.0f) {
            std::cerr << "Fault bass receiver was silent, non-bass, or unbounded: " << receiver
                      << " total=" << metrics.total << " low=" << metrics.low
                      << " peak=" << metrics.peak << "\n";
            return 1;
        }
        for (const auto& channel : receiverAudio[receiver].audio) {
            for (const float sample : channel) {
                if (!std::isfinite(sample)) {
                    std::cerr << "Fault bass receiver produced a non-finite sample: "
                              << receiver << "\n";
                    return 1;
                }
            }
        }
    }
    for (uint32_t a = 0u; a < s3g::kPsdRawFieldBassReceiverCount; ++a) {
        for (uint32_t b = a + 1u; b < s3g::kPsdRawFieldBassReceiverCount; ++b) {
            double difference = 0.0;
            for (uint32_t i = 2048u; i < receiverAudio[a].audio[0].size(); ++i) {
                difference += std::abs(static_cast<double>(
                    receiverAudio[a].audio[0][i] - receiverAudio[b].audio[0][i]));
            }
            if (difference <= 0.01) {
                std::cerr << "Fault bass receivers collapsed together: " << a << "/" << b
                          << " difference=" << difference << "\n";
                return 1;
            }
        }
    }

    auto punchParams = receiverParams;
    punchParams.bassReceiver = s3g::PsdRawFieldBassReceiver::Direct;
    punchParams.bassPunch = 0.92f;
    punchParams.bassTrace = 0.0f;
    const BassRender punchOff = renderBass(punchParams, 1.0f, 24000u, false, true);
    const BassRender punchOn = renderBass(punchParams, 1.0f, 24000u, true, true);
    double earlyPunchDifference = 0.0;
    double latePunchDifference = 0.0;
    for (uint32_t i = 0u; i < 4096u; ++i) {
        earlyPunchDifference += std::abs(static_cast<double>(
            punchOn.audio[0][i] - punchOff.audio[0][i]));
    }
    for (uint32_t i = 19904u; i < 24000u; ++i) {
        latePunchDifference += std::abs(static_cast<double>(
            punchOn.audio[0][i] - punchOff.audio[0][i]));
    }
    auto positiveCrossings = [](const BassRender& rendered, uint32_t begin, uint32_t end) {
        uint32_t crossings = 0u;
        double previous = 0.0;
        for (uint32_t ch = 0u; ch < s3g::kPsdRawFieldChannels; ++ch) {
            previous += rendered.audio[ch][begin];
        }
        for (uint32_t i = begin + 1u; i < end; ++i) {
            double current = 0.0;
            for (uint32_t ch = 0u; ch < s3g::kPsdRawFieldChannels; ++ch) {
                current += rendered.audio[ch][i];
            }
            if (previous <= 0.0 && current > 0.0) ++crossings;
            previous = current;
        }
        return crossings;
    };
    const uint32_t punchEarlyCrossings = positiveCrossings(punchOn, 0u, 4096u);
    const uint32_t punchLateCrossings = positiveCrossings(punchOn, 19904u, 24000u);
    const uint32_t neutralEarlyCrossings = positiveCrossings(punchOff, 0u, 4096u);
    const BassMetrics punchMetrics = bassMetrics(punchOn, 0u);
    if (earlyPunchDifference <= 0.01
        || latePunchDifference >= earlyPunchDifference * 0.25
        || static_cast<double>(punchEarlyCrossings)
            < static_cast<double>(neutralEarlyCrossings) * 0.75
        || static_cast<double>(punchEarlyCrossings)
            > static_cast<double>(neutralEarlyCrossings) * 1.25
        || punchMetrics.peak > 1.0f) {
        std::cerr << "Fault explicit EXCITE was not a bounded, pitch-neutral accent: early="
                  << earlyPunchDifference << " late=" << latePunchDifference
                  << " crossings=" << punchEarlyCrossings << "/" << punchLateCrossings
                  << "/" << neutralEarlyCrossings << " peak=" << punchMetrics.peak << "\n";
        return 1;
    }

    auto widthParams = receiverParams;
    widthParams.bassReceiver = s3g::PsdRawFieldBassReceiver::Direct;
    widthParams.bassBody = 1.0f;
    widthParams.bassTrace = 0.28f;
    widthParams.channelSpread = 1.0f;
    widthParams.bassFuzz = 0.82f;
    widthParams.bassMetal = 0.84f;
    widthParams.bassFeedback = 0.68f;
    widthParams.bassLowWidth = 1.0f;
    const BassMetrics wideMetrics = bassMetrics(
        renderBass(widthParams, 1.0f, 24000u, false, true), 4096u);
    widthParams.bassLowWidth = 0.0f;
    const BassMetrics narrowMetrics = bassMetrics(
        renderBass(widthParams, 1.0f, 24000u, false, true), 4096u);
    if (wideMetrics.lowDivergence <= 1.0e-8
        || narrowMetrics.lowDivergence >= wideMetrics.lowDivergence * 0.93) {
        std::cerr << "Fault LOW WIDTH did not create a more coherent bass field: narrow/wide="
                  << narrowMetrics.lowDivergence << "/" << wideMetrics.lowDivergence << "\n";
        return 1;
    }

    auto clockParams = modulationParams;
    clockParams.modSource = s3g::PsdRawFieldModSource::Sine;
    clockParams.modTarget = s3g::PsdRawFieldModTarget::Carrier;
    clockParams.modClockLock = 0u;
    const auto freeClock = renderModulation(clockParams);
    clockParams.modClockLock = 1u;
    const auto lockedClock = renderModulation(clockParams);
    double clockDifference = 0.0;
    for (uint32_t i = 0u; i < frames; ++i) {
        clockDifference += std::abs(static_cast<double>(freeClock[i] - lockedClock[i]));
    }
    if (clockDifference <= 0.01) {
        std::cerr << "Fault FREE and LOCK protocol clocks were indistinguishable\n";
        return 1;
    }

    // MIDI envelope following scales modulation depth before the operator graph.
    std::array<float, frames> closedEnvelope {};
    std::array<float, frames> openEnvelope {};
    openEnvelope.fill(1.0f);
    auto renderWithEnvelope = [&](const s3g::PsdRawFieldParams& p,
                                  const std::array<float, frames>& envelope) {
        field.prepare(48000.0);
        field.setParams(p);
        field.reset();
        field.process(pointers.data(), s3g::kPsdRawFieldChannels, frames, envelope.data());
        return output[0];
    };
    auto envelopeParams = modulationParams;
    envelopeParams.modSource3 = s3g::PsdRawFieldModSource::Hellschreiber;
    envelopeParams.modTarget3 = s3g::PsdRawFieldModTarget::Data;
    envelopeParams.modIndex3 = 0.72f;
    envelopeParams.modEnvelope3 = 1u;
    const auto envelopeClosed = renderWithEnvelope(envelopeParams, closedEnvelope);
    const auto envelopeOpen = renderWithEnvelope(envelopeParams, openEnvelope);
    double envelopeDifference = 0.0;
    for (uint32_t i = 0u; i < frames; ++i) {
        envelopeDifference += std::abs(static_cast<double>(envelopeOpen[i] - envelopeClosed[i]));
    }
    if (envelopeDifference <= 0.01) {
        std::cerr << "Fault ADSR-following operator did not open with the envelope\n";
        return 1;
    }
    envelopeParams.modEnvelope3 = 0u;
    const auto fixedClosed = renderWithEnvelope(envelopeParams, closedEnvelope);
    const auto fixedOpen = renderWithEnvelope(envelopeParams, openEnvelope);
    for (uint32_t i = 0u; i < frames; ++i) {
        if (fixedClosed[i] != fixedOpen[i]) {
            std::cerr << "Fault FIXED operator unexpectedly followed the MIDI envelope\n";
            return 1;
        }
    }

    // All six algorithms must stack three audible operators while preserving
    // each operator's independently selected destination.
    std::array<std::array<float, frames>, s3g::kPsdRawFieldModAlgorithmCount> algorithmAudio {};
    for (uint32_t algorithm = 0u; algorithm < s3g::kPsdRawFieldModAlgorithmCount; ++algorithm) {
        auto algorithmParams = modulationParams;
        algorithmParams.codecMode = s3g::PsdRawFieldCodecMode::HfFax;
        algorithmParams.fieldCodecMode = s3g::PsdRawFieldCodecMode::HfFax;
        algorithmParams.modAlgorithm = static_cast<s3g::PsdRawFieldModAlgorithm>(algorithm);
        algorithmParams.modSource = s3g::PsdRawFieldModSource::Sine;
        algorithmParams.modTarget = s3g::PsdRawFieldModTarget::Carrier;
        algorithmParams.modRate = 0.58f;
        algorithmParams.modRatio = 1.5f;
        algorithmParams.modIndex = 0.68f;
        algorithmParams.modFeedback = 0.16f;
        algorithmParams.modSource2 = s3g::PsdRawFieldModSource::Hellschreiber;
        algorithmParams.modTarget2 = s3g::PsdRawFieldModTarget::Clock;
        algorithmParams.modRate2 = 0.24f;
        algorithmParams.modRatio2 = 0.75f;
        algorithmParams.modIndex2 = 0.72f;
        algorithmParams.modFeedback2 = 0.18f;
        algorithmParams.modClockLock2 = 1u;
        algorithmParams.modSource3 = s3g::PsdRawFieldModSource::Noise;
        algorithmParams.modTarget3 = s3g::PsdRawFieldModTarget::Data;
        algorithmParams.modRate3 = 0.36f;
        algorithmParams.modRatio3 = 2.0f;
        algorithmParams.modIndex3 = 0.64f;
        algorithmParams.modFeedback3 = 0.14f;
        algorithmParams.modClockLock3 = 0u;
        if (algorithmParams.modAlgorithm == s3g::PsdRawFieldModAlgorithm::Regenerator) {
            algorithmParams.modSource2 = s3g::PsdRawFieldModSource::Feedback;
            algorithmParams.modTarget2 = s3g::PsdRawFieldModTarget::Damage;
            algorithmParams.modSource3 = s3g::PsdRawFieldModSource::Sync;
            algorithmParams.modTarget3 = s3g::PsdRawFieldModTarget::Clock;
            algorithmParams.modFeedback2 = 0.86f;
        } else if (algorithmParams.modAlgorithm == s3g::PsdRawFieldModAlgorithm::Transcode) {
            algorithmParams.modSource = s3g::PsdRawFieldModSource::Morse;
            algorithmParams.modSource2 = s3g::PsdRawFieldModSource::Sstv;
            algorithmParams.modSource3 = s3g::PsdRawFieldModSource::HfFax;
            algorithmParams.modTarget = s3g::PsdRawFieldModTarget::Off;
            algorithmParams.modTarget2 = s3g::PsdRawFieldModTarget::Off;
            algorithmParams.modTarget3 = s3g::PsdRawFieldModTarget::Data;
        }

        const auto withOperators = renderModulation(algorithmParams);
        algorithmAudio[algorithm] = withOperators;
        const auto deterministic = renderModulation(algorithmParams);
        auto withoutM2Params = algorithmParams;
        withoutM2Params.modIndex2 = 0.0f;
        const auto withoutM2 = renderModulation(withoutM2Params);
        auto withoutM3Params = algorithmParams;
        withoutM3Params.modIndex3 = 0.0f;
        const auto withoutM3 = renderModulation(withoutM3Params);
        double m2Difference = 0.0;
        double m3Difference = 0.0;
        for (uint32_t i = 0u; i < frames; ++i) {
            if (!std::isfinite(withOperators[i]) || withOperators[i] != deterministic[i]) {
                std::cerr << "Fault modulation algorithm was non-finite or non-deterministic: "
                          << algorithm << " at sample " << i << "\n";
                return 1;
            }
            m2Difference += std::abs(static_cast<double>(withOperators[i] - withoutM2[i]));
            m3Difference += std::abs(static_cast<double>(withOperators[i] - withoutM3[i]));
        }
        if (m2Difference <= 0.01 || m3Difference <= 0.01) {
            std::cerr << "Fault modulation algorithm did not route all three operators: "
                      << algorithm << " M2=" << m2Difference
                      << " M3=" << m3Difference << "\n";
            return 1;
        }
    }
    for (uint32_t algorithm = 1u; algorithm < s3g::kPsdRawFieldModAlgorithmCount; ++algorithm) {
        double topologyDifference = 0.0;
        for (uint32_t i = 0u; i < frames; ++i) {
            topologyDifference += std::abs(static_cast<double>(
                algorithmAudio[algorithm][i] - algorithmAudio[algorithm - 1u][i]));
        }
        if (topologyDifference <= 0.01) {
            std::cerr << "Fault adjacent modulation algorithms collapsed together: "
                      << algorithm - 1u << " and " << algorithm << "\n";
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

    constexpr std::array<s3g::PsdRawFieldCodecMode, 17> upgradedModes {
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
        s3g::PsdRawFieldCodecMode::Apt,
        s3g::PsdRawFieldCodecMode::HfFax,
        s3g::PsdRawFieldCodecMode::Hellschreiber,
        s3g::PsdRawFieldCodecMode::Morse,
        s3g::PsdRawFieldCodecMode::SparkCw,
        s3g::PsdRawFieldCodecMode::BaudotRtty,
        s3g::PsdRawFieldCodecMode::Sstv,
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
