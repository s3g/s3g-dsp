#include "s3g_processor_conduit.h"
#include "s3g_processor_conduit_presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kFrames = 96000u;

struct Response {
    float peak = 0.0f;
    double energy = 0.0;
    double earlyEnergy = 0.0;
    double lateEnergy = 0.0;
    double chamberEnergy = 0.0;
    double difference = 0.0;
    double stereoDifference = 0.0;
};

Response render(s3g::ProcessorConduitMaterial material, float size,
    float feedback,
    s3g::ProcessorConduitPedal pedal = s3g::ProcessorConduitPedal::Shred,
    float pedalDrive = 0.36f, float octaveDown = 0.0f,
    float chamber = 0.62f, float micMotion = 0.32f,
    float paDrive = 0.46f, float width = 0.68f,
    float damping = 0.38f,
    s3g::ProcessorConduitPedalPosition pedalPosition =
        s3g::ProcessorConduitPedalPosition::PreDriver,
    float pedalMix = 0.60f)
{
    s3g::ProcessorConduit processor;
    processor.prepare(kSampleRate);
    s3g::ProcessorConduitParams params;
    params.material = material;
    params.size = size;
    params.feedback = feedback;
    params.pedal = pedal;
    params.pedalDrive = pedalDrive;
    params.pedalPosition = pedalPosition;
    params.pedalMix = pedalMix;
    params.octaveDown = octaveDown;
    params.chamber = chamber;
    params.micMotion = micMotion;
    params.paDrive = paDrive;
    params.stereoWidth = width;
    params.damping = damping;
    params.mix = 1.0f;
    params.outputGainDb = -3.0f;
    processor.setParams(params);
    processor.reset();

    Response response;
    float previous = 0.0f;
    for (uint32_t i = 0u; i < kFrames; ++i) {
        const float input = i < 128u
            ? std::sin(static_cast<float>(i) * 0.41f)
                * (1.0f - static_cast<float>(i) / 128.0f)
            : 0.0f;
        float left = 0.0f;
        float right = 0.0f;
        processor.processFrame(input, left, right);
        const float output = (left + right) * 0.5f;
        if (!std::isfinite(output)) {
            std::cerr << "Processor Conduit produced non-finite output\n";
            return {};
        }
        response.peak = std::max(response.peak, std::abs(output));
        const double square = static_cast<double>(output) * output;
        response.energy += square;
        if (i < 12000u) response.earlyEnergy += square;
        if (i >= 12000u && i < 36000u) response.chamberEnergy += square;
        if (i >= 48000u) response.lateEnergy += square;
        response.difference += std::abs(static_cast<double>(output - previous));
        response.stereoDifference += std::abs(
            static_cast<double>(left - right));
        previous = output;
    }
    return response;
}

} // namespace

int main()
{
    s3g::ProcessorConduitOctaveDown octave;
    octave.prepare(kSampleRate);
    double downReal = 0.0;
    double downImag = 0.0;
    double sourceReal = 0.0;
    double sourceImag = 0.0;
    for (uint32_t i = 0u; i < kFrames; ++i) {
        const float phase = 2.0f * s3g::kPi * 440.0f
            * static_cast<float>(i) / static_cast<float>(kSampleRate);
        const float output = octave.processSample(std::sin(phase), 1.0f, 0.65f);
        if (i < 24000u) continue;
        const double downPhase = 2.0 * static_cast<double>(s3g::kPi)
            * 220.0 * static_cast<double>(i) / kSampleRate;
        const double sourcePhase = 2.0 * static_cast<double>(s3g::kPi)
            * 440.0 * static_cast<double>(i) / kSampleRate;
        downReal += output * std::cos(downPhase);
        downImag += output * std::sin(downPhase);
        sourceReal += output * std::cos(sourcePhase);
        sourceImag += output * std::sin(sourcePhase);
    }
    const double downEnergy = downReal * downReal + downImag * downImag;
    const double sourceEnergy = sourceReal * sourceReal
        + sourceImag * sourceImag;
    if (downEnergy <= sourceEnergy * 4.0) {
        std::cerr << "Processor Conduit octave path did not shift 440 Hz to 220 Hz\n";
        return 1;
    }

    s3g::ProcessorConduit silence;
    silence.prepare(kSampleRate);
    for (uint32_t i = 0u; i < 4096u; ++i) {
        if (silence.processSample(0.0f) != 0.0f) {
            std::cerr << "Processor Conduit idle path was not exact silence\n";
            return 1;
        }
    }

    std::array<Response, s3g::kProcessorConduitMaterialCount> responses {};
    for (uint32_t i = 0u; i < responses.size(); ++i) {
        responses[i] = render(
            static_cast<s3g::ProcessorConduitMaterial>(i), 0.58f, 0.16f);
        if (responses[i].peak < 0.002f || responses[i].peak > 1.001f
            || responses[i].energy < 0.001) {
            std::cerr << "Processor Conduit material response invalid for "
                      << s3g::processorConduitMaterialName(
                             static_cast<s3g::ProcessorConduitMaterial>(i))
                      << " peak=" << responses[i].peak
                      << " energy=" << responses[i].energy << "\n";
            return 1;
        }
    }

    uint32_t distinctPairs = 0u;
    for (uint32_t i = 1u; i < responses.size(); ++i) {
        const double energyRatio = responses[i].energy
            / std::max(1.0e-12, responses[0].energy);
        const double differenceRatio = responses[i].difference
            / std::max(1.0e-12, responses[0].difference);
        if (energyRatio < 0.82 || energyRatio > 1.18
            || differenceRatio < 0.92 || differenceRatio > 1.08) {
            ++distinctPairs;
        }
    }
    if (distinctPairs < 4u) {
        std::cerr << "Processor Conduit materials were not sufficiently distinct\n";
        return 1;
    }

    const auto vesselDry = render(
        s3g::ProcessorConduitMaterial::MetalVessel, 0.72f, 0.0f,
        s3g::ProcessorConduitPedal::Shred, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.68f, 0.86f);
    const auto vesselWet = render(
        s3g::ProcessorConduitMaterial::MetalVessel, 0.72f, 0.0f,
        s3g::ProcessorConduitPedal::Shred, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 0.68f, 0.86f);
    if (vesselWet.chamberEnergy
            <= vesselDry.chamberEnergy * 1.015 + 1.0e-10) {
        std::cerr << "Processor Conduit vessel did not develop a chamber tail\n";
        std::cerr << "dry=" << vesselDry.chamberEnergy
                  << " wet=" << vesselWet.chamberEnergy << "\n";
        return 1;
    }

    const auto vesselMono = render(
        s3g::ProcessorConduitMaterial::GlassVessel, 0.68f, 0.0f,
        s3g::ProcessorConduitPedal::Shred, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 0.0f);
    const auto vesselStereo = render(
        s3g::ProcessorConduitMaterial::GlassVessel, 0.68f, 0.0f,
        s3g::ProcessorConduitPedal::Shred, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 1.0f);
    if (vesselStereo.stereoDifference
            <= vesselMono.stereoDifference + 0.01) {
        std::cerr << "Processor Conduit stereo pickup/chamber field did not open\n";
        return 1;
    }

    const auto directBassRms = [](float frequency) {
        s3g::ProcessorConduit processor;
        processor.prepare(kSampleRate);
        s3g::ProcessorConduitParams params;
        params.material = s3g::ProcessorConduitMaterial::Direct;
        params.inputGainDb = 0.0f;
        params.driver = 0.0f;
        params.contact = 0.0f;
        params.feedback = 0.0f;
        params.pedalDrive = 0.0f;
        params.mix = 1.0f;
        params.outputGainDb = -6.0f;
        params.stereoWidth = 0.0f;
        processor.setParams(params);
        processor.reset();
        double energy = 0.0;
        uint32_t count = 0u;
        for (uint32_t i = 0u; i < 48000u; ++i) {
            const float input = 0.08f * std::sin(2.0f * s3g::kPi
                * frequency * static_cast<float>(i)
                / static_cast<float>(kSampleRate));
            const float output = processor.processSample(input);
            if (i >= 12000u) {
                energy += static_cast<double>(output) * output;
                ++count;
            }
        }
        return std::sqrt(energy / static_cast<double>(count));
    };
    const double bassRms = directBassRms(48.0f);
    const double midRms = directBassRms(220.0f);
    if (bassRms <= midRms * 0.38) {
        std::cerr << "Processor Conduit wide-band piezo preamp lost bass"
                  << " bass=" << bassRms << " mid=" << midRms << "\n";
        return 1;
    }

    std::array<Response, s3g::kProcessorConduitPedalCount> pedals {};
    for (uint32_t i = 0u; i < pedals.size(); ++i) {
        pedals[i] = render(s3g::ProcessorConduitMaterial::Plastic,
            0.48f, 0.0f,
            static_cast<s3g::ProcessorConduitPedal>(i), 0.86f);
    }
    uint32_t distinctPedals = 0u;
    for (uint32_t i = 1u; i < pedals.size(); ++i) {
        const double ratio = pedals[i].difference
            / std::max(1.0e-12, pedals[0].difference);
        if (ratio < 0.97 || ratio > 1.03) ++distinctPedals;
    }
    if (distinctPedals < 5u) {
        std::cerr << "Processor Conduit pedals were not sufficiently distinct\n";
        return 1;
    }

    std::array<Response, s3g::kProcessorConduitPedalPositionCount>
        pedalPositions {};
    for (uint32_t position = 0u;
         position < pedalPositions.size(); ++position) {
        pedalPositions[position] = render(
            s3g::ProcessorConduitMaterial::SteelShell,
            0.62f, 0.78f, s3g::ProcessorConduitPedal::Rat,
            0.88f, 0.0f, 0.78f, 0.72f, 0.80f, 0.86f, 0.30f,
            static_cast<s3g::ProcessorConduitPedalPosition>(position),
            0.86f);
    }
    uint32_t distinctPositions = 0u;
    for (uint32_t position = 1u;
         position < pedalPositions.size(); ++position) {
        const double energyRatio = pedalPositions[position].energy
            / std::max(1.0e-12, pedalPositions[0u].energy);
        const double differenceRatio = pedalPositions[position].difference
            / std::max(1.0e-12, pedalPositions[0u].difference);
        if (energyRatio < 0.97 || energyRatio > 1.03
            || differenceRatio < 0.97 || differenceRatio > 1.03) {
            ++distinctPositions;
        }
    }
    if (distinctPositions < 2u) {
        std::cerr << "Processor Conduit pedal positions did not alter topology\n";
        return 1;
    }

    s3g::ProcessorConduitParams audition;
    audition.inputGainDb = 11.25f;
    audition.mix = 0.37f;
    audition.outputGainDb = -13.75f;
    audition.inputListen = s3g::ProcessorConduitInputListen::Channel2;
    for (uint32_t index = 0u;
         index < s3g::kProcessorConduitFactoryPresetCount; ++index) {
        const auto preset = s3g::processorConduitFactoryPreset(
            index, audition);
        if (preset.inputGainDb != audition.inputGainDb
            || preset.mix != audition.mix
            || preset.outputGainDb != audition.outputGainDb
            || preset.inputListen != audition.inputListen) {
            std::cerr << "Processor Conduit preset changed audition gain at "
                      << index << "\n";
            return 1;
        }
    }
    const auto randomA = s3g::processorConduitRandomParams(
        audition, 0x12345678u);
    const auto randomB = s3g::processorConduitRandomParams(
        audition, 0x87654321u);
    if (randomA.inputGainDb != audition.inputGainDb
        || randomA.mix != audition.mix
        || randomA.outputGainDb != audition.outputGainDb
        || randomA.inputListen != audition.inputListen
        || randomB.inputGainDb != audition.inputGainDb
        || randomB.mix != audition.mix
        || randomB.outputGainDb != audition.outputGainDb
        || randomB.inputListen != audition.inputListen
        || (randomA.material == randomB.material
            && randomA.pedal == randomB.pedal
            && randomA.pedalPosition == randomB.pedalPosition
            && std::abs(randomA.driver - randomB.driver) < 0.001f)) {
        std::cerr << "Processor Conduit randomization was not gain-safe/distinct\n";
        return 1;
    }

    const auto shortBody = render(
        s3g::ProcessorConduitMaterial::Metal, 0.15f, 0.0f);
    const auto longBody = render(
        s3g::ProcessorConduitMaterial::Metal, 0.90f, 0.0f);
    if (std::abs(shortBody.difference - longBody.difference)
        < shortBody.difference * 0.08) {
        std::cerr << "Processor Conduit size did not alter the response\n";
        return 1;
    }

    const auto noFeedback = render(
        s3g::ProcessorConduitMaterial::Glass, 0.62f, 0.0f);
    const auto highFeedback = render(
        s3g::ProcessorConduitMaterial::Glass, 0.62f, 0.88f);
    if (highFeedback.lateEnergy <= noFeedback.lateEnergy * 1.4 + 1.0e-10) {
        std::cerr << "Processor Conduit feedback did not extend the material tail\n";
        return 1;
    }

    const auto cleanPa = render(
        s3g::ProcessorConduitMaterial::Direct, 0.56f, 0.86f,
        s3g::ProcessorConduitPedal::Shred, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f);
    const auto drivenPa = render(
        s3g::ProcessorConduitMaterial::Direct, 0.56f, 0.86f,
        s3g::ProcessorConduitPedal::Shred, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f);
    const double paDifferenceRatio = drivenPa.difference
        / std::max(1.0e-12, cleanPa.difference);
    if (paDifferenceRatio > 0.97 && paDifferenceRatio < 1.03) {
        std::cerr << "Processor Conduit PA drive did not change feedback breakup\n";
        return 1;
    }

    s3g::ProcessorConduit thrownMic;
    thrownMic.prepare(kSampleRate);
    s3g::ProcessorConduitParams thrown;
    thrown.material = s3g::ProcessorConduitMaterial::Direct;
    thrown.driver = 0.72f;
    thrown.contact = 0.78f;
    thrown.feedback = 0.96f;
    thrown.paDrive = 0.92f;
    thrown.micMotion = 1.0f;
    thrown.mix = 1.0f;
    thrown.outputGainDb = -9.0f;
    thrownMic.setParams(thrown);
    thrownMic.reset();
    float breakupPeak = 0.0f;
    float positionMinimum = 1.0f;
    float positionMaximum = -1.0f;
    for (uint32_t i = 0u; i < kFrames; ++i) {
        const float input = i < 16000u
            ? std::sin(static_cast<float>(i) * 0.31f) * 0.45f : 0.0f;
        const float output = thrownMic.processSample(input);
        if (!std::isfinite(output)) {
            std::cerr << "Processor Conduit thrown microphone became non-finite\n";
            return 1;
        }
        breakupPeak = std::max(breakupPeak,
            thrownMic.feedbackBreakupActivity());
        positionMinimum = std::min(positionMinimum,
            thrownMic.virtualMicPosition());
        positionMaximum = std::max(positionMaximum,
            thrownMic.virtualMicPosition());
    }
    if (breakupPeak < 1.0e-4f
        || positionMaximum - positionMinimum < 0.10f) {
        std::cerr << "Processor Conduit microphone throw did not become erratic"
                  << " breakup=" << breakupPeak
                  << " travel=" << positionMaximum - positionMinimum << "\n";
        return 1;
    }

    s3g::ProcessorConduit stress;
    stress.prepare(kSampleRate);
    s3g::ProcessorConduitParams extreme;
    extreme.inputGainDb = 36.0f;
    extreme.driver = 1.0f;
    extreme.contact = 1.0f;
    extreme.feedback = 1.0f;
    extreme.damping = 0.0f;
    extreme.mix = 1.0f;
    extreme.outputGainDb = 6.0f;
    stress.setParams(extreme);
    stress.reset();
    for (uint32_t i = 0u; i < kFrames; ++i) {
        const float input = (i & 1u) ? 8.0f : -8.0f;
        const float output = stress.processSample(input);
        if (!std::isfinite(output) || std::abs(output) > 1.001f) {
            std::cerr << "Processor Conduit containment failed\n";
            return 1;
        }
    }
    if (stress.governorReduction() < 0.02f) {
        std::cerr << "Processor Conduit feedback governor did not react\n";
        return 1;
    }
    stress.panic();
    for (uint32_t i = 0u; i < 4096u; ++i) {
        if (stress.processSample(0.0f) != 0.0f) {
            std::cerr << "Processor Conduit panic did not clear the body\n";
            return 1;
        }
    }

    std::cout << "Processor Conduit smoke passed\n";
    return 0;
}
