#include "s3g_no_input_mixer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>

namespace {

using Frame = std::array<float, s3g::kNoInputMixerChannels>;

struct RenderStats {
    std::array<double, s3g::kNoInputMixerChannels> sumSquares {};
    std::array<double, s3g::kNoInputMixerChannels> differences {};
    float peak = 0.0f;
    uint64_t samples = 0u;
    bool finite = true;
};

RenderStats render(s3g::NoInputMixer& mixer, uint32_t frames,
    uint32_t skip = 0u)
{
    RenderStats stats;
    Frame frame {};
    Frame previous {};
    for (uint32_t sample = 0u; sample < frames; ++sample) {
        mixer.processFrame(frame.data());
        for (uint32_t lane = 0u; lane < frame.size(); ++lane) {
            const float value = frame[lane];
            if (!std::isfinite(value)) stats.finite = false;
            stats.peak = std::max(stats.peak, std::abs(value));
            if (sample >= skip) {
                stats.sumSquares[lane] += static_cast<double>(value) * value;
                if (lane > 0u) {
                    stats.differences[lane] += std::abs(
                        static_cast<double>(value - frame[0]));
                }
            }
        }
        previous = frame;
    }
    stats.samples = frames > skip ? frames - skip : 0u;
    return stats;
}

bool testSilentReset()
{
    s3g::NoInputMixer mixer;
    mixer.prepare(48000.0);
    mixer.reset();
    const auto stats = render(mixer, 2048u);
    if (!stats.finite || stats.peak != 0.0f) {
        std::cerr << "No Input Mixer reset was not silent: "
                  << stats.peak << "\n";
        return false;
    }
    return true;
}

bool testAudioThreadStackReset()
{
    std::atomic<bool> passed { false };
    std::thread audioThread([&passed]() {
        auto mixer = std::make_unique<s3g::NoInputMixer>();
        mixer->prepare(48000.0);
        auto params = s3g::defaultNoInputMixerParams();
        for (auto& lane : params.lanes) {
            for (auto& insert : lane.inserts) {
                insert.type = s3g::NoInputDistortionType::Splice;
                insert.bypass = 0u;
            }
        }
        mixer->setParams(params);
        for (uint32_t reset = 0u; reset < 16u; ++reset) {
            mixer->reset();
            mixer->reseed(params.seed + reset, 0.5f);
        }
        Frame frame {};
        mixer->processFrame(frame.data());
        passed.store(std::all_of(frame.begin(), frame.end(), [](float value) {
            return std::isfinite(value);
        }), std::memory_order_release);
    });
    audioThread.join();
    if (!passed.load(std::memory_order_acquire)) {
        std::cerr << "No Input Mixer failed the audio-thread reset test\n";
        return false;
    }
    return true;
}

bool testDefaultEcology()
{
    s3g::NoInputMixer mixer;
    mixer.prepare(48000.0);
    const auto params = s3g::defaultNoInputMixerParams();
    mixer.setParams(params);
    mixer.reseed(params.seed, 0.58f);
    const auto stats = render(mixer, 96000u, 12000u);
    if (!stats.finite || !(stats.peak > 1.0e-5f) || stats.peak > 1.01f) {
        std::cerr << "No Input Mixer default ecology produced invalid peak: "
                  << stats.peak << "\n";
        return false;
    }
    for (uint32_t lane = 0u; lane < s3g::kNoInputMixerChannels; ++lane) {
        const double rms = std::sqrt(stats.sumSquares[lane]
            / static_cast<double>(stats.samples));
        if (!(rms > 1.0e-5) || rms > 1.01) {
            std::cerr << "No Input Mixer lane " << lane + 1u
                      << " had invalid RMS " << rms << "\n";
            return false;
        }
        if (lane > 0u && !(stats.differences[lane] > 0.01)) {
            std::cerr << "No Input Mixer lane " << lane + 1u
                      << " collapsed onto lane 1\n";
            return false;
        }
    }
    return true;
}

bool testDistortionFamilies()
{
    if (std::strcmp(s3g::noInputDistortionName(
            static_cast<s3g::NoInputDistortionType>(1u)), "WOOL") != 0
        || std::strcmp(s3g::noInputMixerFactoryPresetName(3u),
            "WOOL RING") != 0) {
        std::cerr << "No Input Mixer neutral processor names regressed\n";
        return false;
    }
    for (uint32_t type = 1u; type < s3g::kNoInputDistortionTypeCount;
         ++type) {
        auto params = s3g::defaultNoInputMixerParams();
        params.feedback = 0.94f;
        params.outputGainDb = -24.0f;
        for (auto& lane : params.lanes) {
            lane.inserts[0].type =
                static_cast<s3g::NoInputDistortionType>(type);
            lane.inserts[0].gain = 0.68f;
            lane.inserts[0].tone = 0.61f;
            lane.inserts[0].bias = -0.17f;
            lane.inserts[0].levelDb = -9.0f;
            lane.inserts[0].bypass = 0u;
            lane.inserts[1].bypass = 1u;
            lane.inserts[2].bypass = 1u;
        }
        s3g::NoInputMixer mixer;
        mixer.prepare(48000.0);
        mixer.setParams(params);
        mixer.reseed(params.seed + type * 17u, 0.72f);
        const auto stats = render(mixer, 24000u, 2000u);
        if (!stats.finite || !(stats.peak > 1.0e-7f)
            || stats.peak > 1.01f) {
            std::cerr << "No Input Mixer distortion "
                      << s3g::noInputDistortionName(
                             static_cast<s3g::NoInputDistortionType>(type))
                      << " failed: peak " << stats.peak << "\n";
            return false;
        }
    }
    return true;
}

bool testFactoryPresetsAndRandomization()
{
    if (s3g::kNoInputMixerFactoryPresetCount != 20u
        || std::strcmp(s3g::noInputMixerFactoryPresetName(10u),
            "STATIC CHOIR") != 0
        || std::strcmp(s3g::noInputMixerFactoryPresetName(19u),
            "WALL ENGINE") != 0
        || s3g::noInputMixerFactoryBehavior(4u).behavior
            != s3g::NoInputMovementBehavior::Burst
        || s3g::noInputMixerFactoryBehavior(5u).behavior
            != s3g::NoInputMovementBehavior::Scramble
        || s3g::noInputMixerFactoryBehavior(11u).behavior
            != s3g::NoInputMovementBehavior::Cut
        || s3g::noInputMixerFactoryBehavior(14u).behavior
            != s3g::NoInputMovementBehavior::Burst
        || s3g::noInputMixerFactoryBehavior(16u).behavior
            != s3g::NoInputMovementBehavior::Scramble
        || s3g::noInputMixerFactoryBehavior(19u).behavior
            != s3g::NoInputMovementBehavior::Glide) {
        std::cerr << "No Input Mixer articulation presets regressed\n";
        return false;
    }
    const auto lattice = s3g::noInputMixerFactoryPreset(1u);
    const auto rain = s3g::noInputMixerFactoryPreset(2u);
    const auto ratCage = s3g::noInputMixerFactoryPreset(4u);
    const auto openHouse = s3g::noInputMixerFactoryPreset(8u);
    const auto staticChoir = s3g::noInputMixerFactoryPreset(10u);
    const auto spliceStorm = s3g::noInputMixerFactoryPreset(14u);
    const auto octaveLadder = s3g::noInputMixerFactoryPreset(17u);
    const auto auxMirror = s3g::noInputMixerFactoryPreset(18u);
    const auto wallEngine = s3g::noInputMixerFactoryPreset(19u);
    if (lattice.reactMode != s3g::NoInputReactMode::Balance
        || lattice.lanes[0].pitchLock == 0u
        || rain.slowTime == 0u
        || ratCage.clockSync == 0u
        || openHouse.lanes[0].auxTap[0]
            == s3g::NoInputAuxTap::Return
        || openHouse.lanes[0].auxReturn[1] >= 0.0f
        || staticChoir.lanes[0].pitchLock == 0u
        || staticChoir.lanes[0].inserts[0].type
            != s3g::NoInputDistortionType::Throat
        || spliceStorm.lanes[0].inserts[0].type
            != s3g::NoInputDistortionType::Splice
        || spliceStorm.clockSync == 0u
        || octaveLadder.lanes[0].inserts[0].type
            != s3g::NoInputDistortionType::OctDown
        || octaveLadder.lanes[7].inserts[0].type
            != s3g::NoInputDistortionType::OctUp
        || auxMirror.lanes[0].auxTap[0]
            == auxMirror.lanes[1].auxTap[0]
        || auxMirror.lanes[0].auxReturn[1] >= 0.0f
        || auxMirror.lanes[1].auxReturn[1] <= 0.0f
        || wallEngine.reactMode != s3g::NoInputReactMode::Off
        || wallEngine.lanes[0].inserts[2].bypass != 0u) {
        std::cerr << "No Input Mixer factory presets do not expose the "
                     "reactive, tuned, clocked, processor, and aux-topology "
                     "layers\n";
        return false;
    }
    for (uint32_t preset = 0u;
         preset < s3g::kNoInputMixerFactoryPresetCount; ++preset) {
        const auto params = s3g::noInputMixerFactoryPreset(preset);
        s3g::NoInputMixer mixer;
        mixer.prepare(48000.0);
        mixer.setParams(params);
        mixer.setMovementBehaviorParams(
            s3g::noInputMixerFactoryBehavior(preset));
        mixer.reseed(params.seed, 0.58f);
        const auto stats = render(mixer, 32000u, 3000u);
        if (!stats.finite || !(stats.peak > 1.0e-7f)
            || stats.peak > 1.01f) {
            std::cerr << "No Input Mixer factory preset "
                      << s3g::noInputMixerFactoryPresetName(preset)
                      << " failed: peak " << stats.peak << "\n";
            return false;
        }
    }

    const auto randomA = s3g::randomizedNoInputMixerParams(0x12345678u);
    const auto randomB = s3g::randomizedNoInputMixerParams(0x12345678u);
    const auto randomC = s3g::randomizedNoInputMixerParams(0x87654321u);
    if (randomA.seed != randomB.seed
        || randomA.feedback != randomB.feedback
        || randomA.matrix != randomB.matrix
        || randomA.lanes[3].body != randomB.lanes[3].body
        || randomA.reactMode != randomB.reactMode
        || randomA.lanes[3].tuneNote != randomB.lanes[3].tuneNote
        || randomA.lanes[3].auxTap != randomB.lanes[3].auxTap
        || randomA.lanes[3].auxReturn != randomB.lanes[3].auxReturn
        || randomA.controllerHold != 0u
        || (randomA.feedback == randomC.feedback
            && randomA.matrix == randomC.matrix)) {
        std::cerr << "No Input Mixer bounded randomization was not "
                     "deterministic and seed-sensitive\n";
        return false;
    }
    for (const uint32_t seed : { 0x13579bdfu, 0x2468ace0u, 0x10203040u }) {
        const auto params = s3g::randomizedNoInputMixerParams(seed);
        s3g::NoInputMixer mixer;
        mixer.prepare(48000.0);
        mixer.setParams(params);
        mixer.reseed(params.seed, 0.62f);
        const auto stats = render(mixer, 32000u, 3000u);
        if (!stats.finite || !(stats.peak > 1.0e-7f)
            || stats.peak > 1.01f) {
            std::cerr << "No Input Mixer randomized patch failed for seed "
                      << seed << ": peak " << stats.peak << "\n";
            return false;
        }
    }
    return true;
}

bool testRandomEnergyProfiles()
{
    using Energy = s3g::NoInputRandomEnergy;
    if (std::strcmp(s3g::noInputRandomEnergyName(Energy::High),
            "HIGH / QUICK") != 0
        || std::strcmp(s3g::noInputRandomEnergyName(Energy::Mid),
            "MID / MODERATE") != 0
        || std::strcmp(s3g::noInputRandomEnergyName(Energy::Low),
            "LOW / SLOW") != 0
        || !(s3g::noInputRandomSeedAmount(Energy::High)
            > s3g::noInputRandomSeedAmount(Energy::Mid))
        || !(s3g::noInputRandomSeedAmount(Energy::Mid)
            > s3g::noInputRandomSeedAmount(Energy::Low))) {
        std::cerr << "No Input Mixer random energy vocabulary regressed\n";
        return false;
    }

    const std::array<Energy, 3u> profiles {{
        Energy::High, Energy::Mid, Energy::Low,
    }};
    for (uint32_t profile = 0u; profile < profiles.size(); ++profile) {
        const Energy energy = profiles[profile];
        for (const uint32_t seed : { 0x3152414eu, 0x3252414eu }) {
            const auto params = s3g::randomizedNoInputMixerParams(
                seed + profile * 0x1000u, energy);
            const auto repeat = s3g::randomizedNoInputMixerParams(
                seed + profile * 0x1000u, energy);
            const auto behavior =
                s3g::randomizedNoInputMovementBehaviorParams(
                    seed ^ 0x43564d58u, energy);
            if (params.matrix != repeat.matrix
                || params.motion != repeat.motion
                || params.slowTime != repeat.slowTime
                || params.controllerHold != 0u) {
                std::cerr << "No Input Mixer energy randomization was not "
                             "deterministic and controller-safe\n";
                return false;
            }

            const float fieldHz = params.clockSync != 0u
                ? s3g::noInputSyncedRateHz(params.fieldDivision, 120.0)
                : s3g::noInputMixerMotionRateHz(
                    params.motionRate, params.slowTime != 0u);
            const float eventHz = params.clockSync != 0u
                ? s3g::noInputSyncedRateHz(params.eventDivision, 120.0)
                : s3g::noInputMovementEventRateHz(
                    behavior.eventRate, params.slowTime != 0u);
            bool profileValid = false;
            if (energy == Energy::High) {
                profileValid = params.slowTime == 0u
                    && params.motion >= 0.72f
                    && behavior.behavior >= s3g::NoInputMovementBehavior::Cut
                    && behavior.choke >= 0.66f
                    && fieldHz >= 1.0f && eventHz >= 4.0f;
            } else if (energy == Energy::Mid) {
                profileValid = params.slowTime == 0u
                    && params.motion >= 0.42f && params.motion <= 0.681f
                    && behavior.behavior
                        >= s3g::NoInputMovementBehavior::Step
                    && fieldHz >= 0.25f && fieldHz <= 8.01f
                    && eventHz >= 0.9f && eventHz <= 16.1f;
            } else {
                profileValid = params.slowTime != 0u
                    && params.motion >= 0.38f && params.motion <= 0.561f
                    && behavior.behavior
                        <= s3g::NoInputMovementBehavior::Step
                    && behavior.choke <= 0.301f
                    && fieldHz >= 0.03f && fieldHz <= 1.01f
                    && eventHz >= 0.03f && eventHz <= 1.01f;
            }
            if (!profileValid) {
                std::cerr << "No Input Mixer random profile "
                          << s3g::noInputRandomEnergyName(energy)
                          << " escaped its movement band: field/event "
                          << fieldHz << "/" << eventHz << " Hz\n";
                return false;
            }

            s3g::NoInputMixer mixer;
            mixer.prepare(48000.0);
            mixer.setParams(params);
            mixer.setMovementBehaviorParams(behavior);
            mixer.setTransport(120.0, true);
            mixer.reseed(params.seed,
                s3g::noInputRandomSeedAmount(energy));
            const auto onset = render(mixer, 2048u);
            const auto ecology = render(mixer, 36000u, 4000u);
            double ecologyEnergy = 0.0;
            for (double value : ecology.sumSquares) ecologyEnergy += value;
            if (!onset.finite || !ecology.finite
                || onset.peak <= 1.0e-4f || onset.peak > 1.01f
                || ecologyEnergy <= 1.0e-8) {
                std::cerr << "No Input Mixer random profile "
                          << s3g::noInputRandomEnergyName(energy)
                          << " was not immediately and persistently audible: "
                          << onset.peak << "/" << ecologyEnergy << "\n";
                return false;
            }
        }
    }
    return true;
}

bool testSignedMatrixChangesState()
{
    auto positiveParams = s3g::defaultNoInputMixerParams();
    auto negativeParams = positiveParams;
    positiveParams.matrix[1u * s3g::kNoInputMixerChannels] = 0.74f;
    negativeParams.matrix[1u * s3g::kNoInputMixerChannels] = -0.74f;

    s3g::NoInputMixer positive;
    s3g::NoInputMixer negative;
    positive.prepare(48000.0);
    negative.prepare(48000.0);
    positive.setParams(positiveParams);
    negative.setParams(negativeParams);
    positive.reseed(0x51515151u, 0.55f);
    negative.reseed(0x51515151u, 0.55f);

    Frame positiveFrame {};
    Frame negativeFrame {};
    double difference = 0.0;
    float positiveRoutePeak = 0.0f;
    float negativeRoutePeak = 0.0f;
    constexpr uint32_t route = 1u * s3g::kNoInputMixerChannels;
    for (uint32_t sample = 0u; sample < 18000u; ++sample) {
        positive.processFrame(positiveFrame.data());
        negative.processFrame(negativeFrame.data());
        const float positiveRoute = positive.routeSignal(route);
        const float negativeRoute = negative.routeSignal(route);
        if (!std::isfinite(positiveFrame[1])
            || !std::isfinite(negativeFrame[1])
            || !std::isfinite(positiveRoute)
            || !std::isfinite(negativeRoute)) {
            std::cerr << "No Input Mixer signed matrix produced non-finite audio\n";
            return false;
        }
        positiveRoutePeak = std::max(
            positiveRoutePeak, std::abs(positiveRoute));
        negativeRoutePeak = std::max(
            negativeRoutePeak, std::abs(negativeRoute));
        difference += std::abs(static_cast<double>(
            positiveFrame[1] - negativeFrame[1]));
    }
    if (!(difference > 0.01)) {
        std::cerr << "No Input Mixer matrix polarity had no material effect\n";
        return false;
    }
    if (!(positiveRoutePeak > 1.0e-7f)
        || !(negativeRoutePeak > 1.0e-7f)) {
        std::cerr << "No Input Mixer routed-audio telemetry stayed silent\n";
        return false;
    }
    return true;
}

bool testHybridControlEcology()
{
    if (std::abs(s3g::noInputMixerMotionRateHz(0.0f) - 0.05f) > 1.0e-6f
        || std::abs(s3g::noInputMixerMotionRateHz(0.5f) - 0.5f) > 1.0e-5f
        || std::abs(s3g::noInputMixerMotionRateHz(1.0f) - 5.0f) > 1.0e-4f) {
        std::cerr << "No Input Mixer movement rate law is incorrect\n";
        return false;
    }
    const float focused = s3g::noInputMixerMotionGainScale(
        1.0f, 1.0f, 2u, 0.38f);
    const float background = s3g::noInputMixerMotionGainScale(
        0.1f, 1.0f, 2u, 0.38f);
    const float contrastDb = 20.0f * std::log10(background / focused);
    if (std::abs(focused - 1.0f) > 1.0e-6f || contrastDb > -9.0f
        || s3g::noInputMixerMotionGainScale(
            0.0f, 1.0f, 2u, 0.0f) != 1.0f) {
        std::cerr << "No Input Mixer movement contrast is not audible: "
                  << contrastDb << " dB\n";
        return false;
    }

    auto params = s3g::noInputMixerFactoryPreset(9u);
    const auto phaseA = s3g::noInputMixerMotionWeights(params, 0.10f);
    const auto phaseB = s3g::noInputMixerMotionWeights(params, 0.55f);
    if (phaseA == phaseB) {
        std::cerr << "No Input Mixer Matrix movement did not move\n";
        return false;
    }
    bool patchedGainMoved = false;
    for (float weight : phaseA) {
        if (!std::isfinite(weight) || weight < 0.0f || weight > 1.0001f) {
            std::cerr << "No Input Mixer Matrix movement escaped bounds\n";
            return false;
        }
    }
    std::array<float, s3g::kNoInputMixerChannels> peakA {};
    std::array<float, s3g::kNoInputMixerChannels> peakB {};
    std::array<uint32_t, s3g::kNoInputMixerChannels> routeCount {};
    for (uint32_t destination = 0u;
         destination < s3g::kNoInputMixerChannels; ++destination) {
        for (uint32_t source = 0u;
             source < s3g::kNoInputMixerChannels; ++source) {
            const uint32_t index = destination
                * s3g::kNoInputMixerChannels + source;
            if (std::abs(params.matrix[index]) <= 0.001f) continue;
            peakA[source] = std::max(peakA[source], phaseA[index]);
            peakB[source] = std::max(peakB[source], phaseB[index]);
            ++routeCount[source];
        }
    }
    for (uint32_t index = 0u; index < params.matrix.size(); ++index) {
        const float stored = std::abs(params.matrix[index]);
        if (stored <= 0.001f) continue;
        const uint32_t source = index % s3g::kNoInputMixerChannels;
        const float gainA = stored * s3g::noInputMixerMotionGainScale(
            phaseA[index], peakA[source], routeCount[source], params.motion);
        const float gainB = stored * s3g::noInputMixerMotionGainScale(
            phaseB[index], peakB[source], routeCount[source], params.motion);
        if (gainA > stored + 1.0e-5f || gainB > stored + 1.0e-5f) {
            std::cerr << "Matrix movement exceeded stored route gain\n";
            return false;
        }
        patchedGainMoved = patchedGainMoved
            || std::abs(gainA - gainB) > 1.0e-4f;
    }
    if (!patchedGainMoved) {
        std::cerr << "Matrix movement did not modulate patched route gain\n";
        return false;
    }

    const auto varied = s3g::variedNoInputMixerParams(
        params, 0x56415259u, 0.72f);
    for (uint32_t index = 0u; index < params.matrix.size(); ++index) {
        if (params.matrix[index] == 0.0f && varied.matrix[index] != 0.0f) {
            std::cerr << "Preset variance invented a matrix connection\n";
            return false;
        }
    }
    const auto forgotten = s3g::forgottenNoInputMixerParams(
        params, 0x464f5247u);
    bool routeChanged = false;
    for (uint32_t index = 0u; index < params.matrix.size(); ++index) {
        routeChanged = routeChanged
            || params.matrix[index] != forgotten.matrix[index];
    }
    if (!routeChanged
        || forgotten.lanes[2].inserts[0].type
            != params.lanes[2].inserts[0].type) {
        std::cerr << "FORGET did not remain a local routing operation\n";
        return false;
    }

    auto dryParams = params;
    auto auxParams = params;
    for (auto& lane : dryParams.lanes) lane.auxSend = { 0.0f, 0.0f };
    for (auto& lane : auxParams.lanes) lane.auxSend = { 0.72f, 0.58f };
    s3g::NoInputMixer dry;
    s3g::NoInputMixer wet;
    dry.prepare(48000.0);
    wet.prepare(48000.0);
    dry.setParams(dryParams);
    wet.setParams(auxParams);
    dry.reseed(params.seed, 0.58f);
    wet.reseed(params.seed, 0.58f);
    Frame dryFrame {};
    Frame wetFrame {};
    double difference = 0.0;
    for (uint32_t sample = 0u; sample < 24000u; ++sample) {
        dry.processFrame(dryFrame.data());
        wet.processFrame(wetFrame.data());
        for (uint32_t lane = 0u; lane < dryFrame.size(); ++lane) {
            if (!std::isfinite(dryFrame[lane])
                || !std::isfinite(wetFrame[lane])) return false;
            difference += std::abs(static_cast<double>(
                dryFrame[lane] - wetFrame[lane]));
        }
    }
    if (!(difference > 0.01) || !(wet.auxActivity(0u) > 0.0f)) {
        std::cerr << "Aux-return loops had no material DSP effect\n";
        return false;
    }

    s3g::NoInputMixer sendlessReference;
    s3g::NoInputMixer mutedAux;
    sendlessReference.prepare(48000.0);
    mutedAux.prepare(48000.0);
    sendlessReference.setParams(dryParams);
    mutedAux.setParams(auxParams);
    mutedAux.setAuxMuted(0u, true);
    mutedAux.setAuxMuted(1u, true);
    sendlessReference.reseed(params.seed, 0.58f);
    mutedAux.reseed(params.seed, 0.58f);
    double mutedDifference = 0.0;
    for (uint32_t sample = 0u; sample < 8000u; ++sample) {
        sendlessReference.processFrame(dryFrame.data());
        mutedAux.processFrame(wetFrame.data());
        for (uint32_t lane = 0u; lane < dryFrame.size(); ++lane) {
            mutedDifference += std::abs(static_cast<double>(
                dryFrame[lane] - wetFrame[lane]));
        }
    }
    if (mutedDifference > 1.0e-6 || mutedAux.auxActivity(0u) != 0.0f
        || mutedAux.auxActivity(1u) != 0.0f) {
        std::cerr << "Global aux mutes did not silence both return buses\n";
        return false;
    }
    mutedAux.setAuxMuted(0u, false);
    double restoredDifference = 0.0;
    for (uint32_t sample = 0u; sample < 16000u; ++sample) {
        sendlessReference.processFrame(dryFrame.data());
        mutedAux.processFrame(wetFrame.data());
        for (uint32_t lane = 0u; lane < dryFrame.size(); ++lane) {
            restoredDifference += std::abs(static_cast<double>(
                dryFrame[lane] - wetFrame[lane]));
        }
    }
    if (!(restoredDifference > 0.01)
        || !(mutedAux.auxActivity(0u) > 0.0f)
        || !mutedAux.auxMuted(1u)) {
        std::cerr << "Global aux mute did not restore the preserved send mix\n";
        return false;
    }
    return true;
}

bool testMovementBehaviors()
{
    if (std::abs(s3g::noInputMovementEventRateHz(0.0f) - 0.25f)
            > 1.0e-6f
        || std::abs(s3g::noInputMovementEventRateHz(1.0f) - 80.0f)
            > 1.0e-3f
        || std::strcmp(s3g::noInputMovementBehaviorName(
                s3g::NoInputMovementBehavior::Scramble), "SCRAMBLE") != 0) {
        std::cerr << "No Input Mixer articulation control laws regressed\n";
        return false;
    }
    const auto randomizedA = s3g::randomizedNoInputMovementBehaviorParams(
        0x12345678u);
    const auto randomizedB = s3g::randomizedNoInputMovementBehaviorParams(
        0x12345678u);
    if (randomizedA.behavior != randomizedB.behavior
        || randomizedA.eventRate != randomizedB.eventRate
        || randomizedA.choke != randomizedB.choke) {
        std::cerr << "Movement behavior randomization was not deterministic\n";
        return false;
    }

    auto params = s3g::noInputMixerFactoryPreset(9u);
    params.motion = 1.0f;
    params.motionRate = 1.0f;
    uint32_t closedRoute = 0u;
    while (closedRoute < params.matrix.size()
        && std::abs(params.matrix[closedRoute]) > 1.0e-7f) {
        ++closedRoute;
    }
    if (closedRoute >= params.matrix.size()) return false;

    for (uint32_t mode = 1u; mode < 5u; ++mode) {
        s3g::NoInputMovementBehaviorParams behavior;
        behavior.behavior = static_cast<s3g::NoInputMovementBehavior>(mode);
        behavior.eventRate = 0.74f;
        behavior.length = 0.16f;
        behavior.density = 0.48f;
        behavior.chaos = 0.72f;
        behavior.slew = 0.04f;
        behavior.choke = 1.0f;
        s3g::NoInputMixer mixer;
        mixer.prepare(48000.0);
        mixer.setParams(params);
        mixer.setMovementBehaviorParams(behavior);
        mixer.reseed(0x43564d58u, 0.72f);
        Frame frame {};
        float minimumGate = 1.0f;
        float maximumGate = 0.0f;
        uint32_t silentFrames = 0u;
        for (uint32_t sample = 0u; sample < 36000u; ++sample) {
            mixer.processFrame(frame.data());
            float framePeak = 0.0f;
            for (float value : frame) {
                if (!std::isfinite(value) || std::abs(value) > 1.01f)
                    return false;
                framePeak = std::max(framePeak, std::abs(value));
            }
            for (uint32_t route = 0u; route < params.matrix.size(); ++route) {
                if (std::abs(params.matrix[route]) <= 1.0e-7f) continue;
                const float gate = mixer.behaviorRouteGate(route);
                minimumGate = std::min(minimumGate, gate);
                maximumGate = std::max(maximumGate, gate);
            }
            if (sample > 2000u && framePeak < 1.0e-4f) ++silentFrames;
            if (std::abs(mixer.routeSignal(closedRoute)) > 1.0e-9f) {
                std::cerr << "Movement behavior invented a closed route\n";
                return false;
            }
        }
        if (!(minimumGate < 0.12f) || !(maximumGate > 0.45f)) {
            std::cerr << "Movement behavior did not articulate routes: "
                      << s3g::noInputMovementBehaviorName(behavior.behavior)
                      << " min " << minimumGate << " max " << maximumGate
                      << "\n";
            return false;
        }
        if ((behavior.behavior == s3g::NoInputMovementBehavior::Cut
                || behavior.behavior == s3g::NoInputMovementBehavior::Burst)
            && silentFrames < 64u) {
            std::cerr << "Post-insert CHOKE did not expose cut gaps: "
                      << silentFrames << " frames\n";
            return false;
        }
    }
    return true;
}

bool testReactClockTuningAndAuxTopology()
{
    if (std::abs(s3g::noInputMixerMotionRateHz(0.0f, true)
            - 1.0f / 600.0f) > 1.0e-7f
        || std::abs(s3g::noInputMovementEventRateHz(1.0f, true) - 1.0f)
            > 1.0e-5f
        || std::abs(s3g::noInputSyncedRateHz(6u, 120.0) - 0.5f)
            > 1.0e-6f
        || std::strcmp(s3g::noInputClockDivisionName(9u), "8 BARS") != 0) {
        std::cerr << "Slow/tempo clock laws regressed\n";
        return false;
    }

    auto reactParams = s3g::defaultNoInputMixerParams();
    reactParams.motion = 0.72f;
    reactParams.motionRate = 0.8f;
    reactParams.reactMode = s3g::NoInputReactMode::Avoid;
    reactParams.reactDepth = 1.0f;
    reactParams.reactThreshold = 0.0f;
    reactParams.reactAttack = 0.0f;
    reactParams.reactRelease = 0.0f;
    uint32_t closedRoute = 0u;
    while (closedRoute < reactParams.matrix.size()
        && std::abs(reactParams.matrix[closedRoute]) > 1.0e-7f) {
        ++closedRoute;
    }
    if (closedRoute >= reactParams.matrix.size()) return false;
    s3g::NoInputMixer react;
    react.prepare(48000.0);
    react.setParams(reactParams);
    react.reseed(0x52454143u, 0.82f);
    Frame frame {};
    float minimumReact = 1.0f;
    for (uint32_t sample = 0u; sample < 32000u; ++sample) {
        react.processFrame(frame.data());
        for (uint32_t route = 0u; route < reactParams.matrix.size(); ++route) {
            if (std::abs(reactParams.matrix[route]) <= 1.0e-7f) continue;
            minimumReact = std::min(minimumReact,
                react.reactRouteGate(route));
        }
        if (std::abs(react.routeSignal(closedRoute)) > 1.0e-9f) {
            std::cerr << "REACT invented a closed matrix route\n";
            return false;
        }
    }
    if (!(minimumReact < 0.65f)) {
        std::cerr << "REACT did not materially articulate route gain: "
                  << minimumReact << "\n";
        return false;
    }
    const float heldPhase = react.motionPhase();
    const float heldGate = react.reactRouteGate(0u);
    reactParams.controllerHold = 1u;
    react.setParams(reactParams);
    render(react, 4096u);
    if (react.motionPhase() != heldPhase
        || react.reactRouteGate(0u) != heldGate) {
        std::cerr << "Hold Ecology did not freeze controller state\n";
        return false;
    }

    auto lowTune = s3g::defaultNoInputMixerParams();
    auto highTune = lowTune;
    for (uint32_t lane = 0u; lane < s3g::kNoInputMixerChannels; ++lane) {
        lowTune.lanes[lane].pitchLock = 1u;
        highTune.lanes[lane].pitchLock = 1u;
        lowTune.lanes[lane].tuneNote = 36.0f + lane;
        highTune.lanes[lane].tuneNote = 84.0f + lane;
    }
    s3g::NoInputMixer low;
    s3g::NoInputMixer high;
    low.prepare(48000.0);
    high.prepare(48000.0);
    low.setParams(lowTune);
    high.setParams(highTune);
    low.reseed(0x54554e45u, 0.72f);
    high.reseed(0x54554e45u, 0.72f);
    Frame lowFrame {};
    Frame highFrame {};
    double tunedDifference = 0.0;
    for (uint32_t sample = 0u; sample < 16000u; ++sample) {
        low.processFrame(lowFrame.data());
        high.processFrame(highFrame.data());
        for (uint32_t lane = 0u; lane < lowFrame.size(); ++lane) {
            tunedDifference += std::abs(static_cast<double>(
                lowFrame[lane] - highFrame[lane]));
        }
    }
    if (!(tunedDifference > 0.01)) {
        std::cerr << "Pitch-locked lane tuning had no DSP effect\n";
        return false;
    }

    auto returnTap = s3g::defaultNoInputMixerParams();
    auto insertTap = returnTap;
    for (auto& lane : returnTap.lanes) {
        lane.auxSend = { 0.72f, 0.0f };
        lane.auxReturn = { 0.65f, 0.0f };
        lane.auxTap[0] = s3g::NoInputAuxTap::Return;
    }
    for (auto& lane : insertTap.lanes) {
        lane.auxSend = { 0.72f, 0.0f };
        lane.auxReturn = { -0.65f, 0.0f };
        lane.auxTap[0] = s3g::NoInputAuxTap::PostInsert;
    }
    s3g::NoInputMixer returnMixer;
    s3g::NoInputMixer insertMixer;
    returnMixer.prepare(48000.0);
    insertMixer.prepare(48000.0);
    returnMixer.setParams(returnTap);
    insertMixer.setParams(insertTap);
    returnMixer.reseed(0x41555854u, 0.68f);
    insertMixer.reseed(0x41555854u, 0.68f);
    double topologyDifference = 0.0;
    for (uint32_t sample = 0u; sample < 20000u; ++sample) {
        returnMixer.processFrame(lowFrame.data());
        insertMixer.processFrame(highFrame.data());
        for (uint32_t lane = 0u; lane < lowFrame.size(); ++lane) {
            topologyDifference += std::abs(static_cast<double>(
                lowFrame[lane] - highFrame[lane]));
        }
    }
    if (!(topologyDifference > 0.01)
        || !(insertMixer.auxActivity(0u) > 0.0f)) {
        std::cerr << "Configurable aux tap/return topology had no DSP effect\n";
        return false;
    }
    return true;
}

bool testPanic()
{
    auto params = s3g::defaultNoInputMixerParams();
    params.feedback = 1.20f;
    s3g::NoInputMixer mixer;
    mixer.prepare(48000.0);
    mixer.setParams(params);
    mixer.reseed(0x50414e49u, 0.85f);
    render(mixer, 4096u);
    mixer.panic();
    const auto faded = render(mixer, 1024u);
    const auto silent = render(mixer, 1024u);
    if (!faded.finite || !silent.finite || silent.peak != 0.0f
        || mixer.containmentState() != s3g::NoInputContainmentState::Quiet) {
        std::cerr << "No Input Mixer PANIC did not clear the graph: faded finite "
                  << faded.finite << ", silent finite " << silent.finite
                  << ", silent peak " << silent.peak << ", state "
                  << static_cast<uint32_t>(mixer.containmentState()) << "\n";
        return false;
    }
    return true;
}

bool testSanitization()
{
    auto params = s3g::defaultNoInputMixerParams();
    params.feedback = std::numeric_limits<float>::infinity();
    params.matrix[0] = std::numeric_limits<float>::quiet_NaN();
    params.lanes[0].midFrequencyHz = -1000.0f;
    params.lanes[0].inserts[0].gain = 9.0f;
    params.lanes[0].inserts[0].type =
        static_cast<s3g::NoInputDistortionType>(999u);
    params.agency = -8.0f;
    params.motionShape = static_cast<s3g::MatrixFlowShape>(999u);
    params.aux[0].feedback = 7.0f;
    params.lanes[0].auxSend[0] = -2.0f;
    const auto clean = s3g::sanitizeNoInputMixerParams(params);
    if (!std::isfinite(clean.feedback) || clean.feedback != 0.82f
        || clean.matrix[0] != 0.0f
        || clean.lanes[0].midFrequencyHz != 80.0f
        || clean.lanes[0].inserts[0].gain != 1.0f
        || clean.lanes[0].inserts[0].type
            != s3g::NoInputDistortionType::OctStack
        || clean.agency != 0.0f
        || clean.motionShape != s3g::MatrixFlowShape::Hold
        || clean.aux[0].feedback != 0.96f
        || clean.lanes[0].auxSend[0] != 0.0f) {
        std::cerr << "No Input Mixer parameter sanitization failed\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!testSilentReset()) return 1;
    if (!testAudioThreadStackReset()) return 1;
    if (!testDefaultEcology()) return 1;
    if (!testDistortionFamilies()) return 1;
    if (!testFactoryPresetsAndRandomization()) return 1;
    if (!testRandomEnergyProfiles()) return 1;
    if (!testSignedMatrixChangesState()) return 1;
    if (!testHybridControlEcology()) return 1;
    if (!testMovementBehaviors()) return 1;
    if (!testReactClockTuningAndAuxTopology()) return 1;
    if (!testPanic()) return 1;
    if (!testSanitization()) return 1;
    std::cout << "No Input Mixer smoke passed\n";
    return 0;
}
