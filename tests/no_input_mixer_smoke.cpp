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

// The No Input Mixer insert and aux paths instantiate the shared Fracture
// processor template with their own private runtime types.  Keep a small
// public-template fixture here so listener-facing kernel behavior can be
// checked without exposing those real-time implementation details.
struct NoInputFractureTestState {
    float low = 0.0f;
    float high = 0.0f;
    float memory = 0.0f;
    float envelope = 0.0f;
    float previous = 0.0f;
    float phase = 0.0f;
    float gate = 0.0f;
};

struct NoInputFractureTestRuntime {
    std::array<float, s3g::kFractureTimeBufferSize> timeBuffer {};
    uint32_t timeWrite = 0u;
    uint32_t timeValid = 0u;
};

float processNoInputFracture(s3g::FractureProcessor processor,
    NoInputFractureTestRuntime& runtime,
    NoInputFractureTestState& state, float input, float ringSource,
    float gain, float tone, float bias)
{
    return s3g::processFractureProcessor(processor, runtime, state,
        input, ringSource, gain, tone, bias, 48000.0f);
}

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

bool testSharedFractureInsertKernels()
{
    constexpr std::array<s3g::FractureProcessor,
        s3g::kFractureProcessorCount> processors {{
        s3g::FractureProcessor::Relay,
        s3g::FractureProcessor::Crush,
        s3g::FractureProcessor::Splice,
        s3g::FractureProcessor::Logic,
        s3g::FractureProcessor::Void,
        s3g::FractureProcessor::Throat,
        s3g::FractureProcessor::Robot,
        s3g::FractureProcessor::OctDown,
        s3g::FractureProcessor::OctUp,
        s3g::FractureProcessor::OctStack,
    }};
    constexpr std::array<s3g::NoInputDistortionType,
        s3g::kFractureProcessorCount> insertTypes {{
        s3g::NoInputDistortionType::Relay,
        s3g::NoInputDistortionType::Crush,
        s3g::NoInputDistortionType::Splice,
        s3g::NoInputDistortionType::Logic,
        s3g::NoInputDistortionType::Void,
        s3g::NoInputDistortionType::Throat,
        s3g::NoInputDistortionType::Robot,
        s3g::NoInputDistortionType::OctDown,
        s3g::NoInputDistortionType::OctUp,
        s3g::NoInputDistortionType::OctStack,
    }};

    // The NIM menu and shared kernel must continue to describe the same ten
    // processors. Every one must also preserve true digital silence at all
    // values of its bipolar third control.
    constexpr std::array<float, 3u> biases {{ -1.0f, 0.0f, 1.0f }};
    for (uint32_t index = 0u; index < processors.size(); ++index) {
        if (std::strcmp(s3g::noInputDistortionName(insertTypes[index]),
                s3g::fractureProcessorName(processors[index])) != 0) {
            std::cerr << "No Input Mixer Fracture insert mapping regressed at "
                      << index << "\n";
            return false;
        }
        for (float bias : biases) {
            NoInputFractureTestRuntime runtime;
            NoInputFractureTestState state;
            for (uint32_t sample = 0u; sample < 8192u; ++sample) {
                const float output = processNoInputFracture(
                    processors[index], runtime, state, 0.0f, 0.0f,
                    1.0f, 0.67f, bias);
                if (!std::isfinite(output) || std::abs(output) > 1.0e-7f) {
                    std::cerr << "No Input Mixer "
                              << s3g::noInputDistortionName(
                                     insertTypes[index])
                              << " insert emitted from digital silence: "
                              << output << "\n";
                    return false;
                }
            }
        }
    }

    // A symmetric NIM network commonly gives LOGIC highly correlated lane
    // and ring-source signals. That must be an active, bounded case rather
    // than the old false/false comparator state that collapsed to silence.
    {
        NoInputFractureTestRuntime runtime;
        NoInputFractureTestState state;
        double sumSquares = 0.0;
        float maximumStep = 0.0f;
        float previous = 0.0f;
        constexpr uint32_t warmup = 24000u;
        constexpr uint32_t measured = 48000u;
        for (uint32_t sample = 0u; sample < warmup + measured; ++sample) {
            const float input = 0.15f * std::sin(2.0f * s3g::kPi
                * 173.0f * static_cast<float>(sample) / 48000.0f);
            const float output = processNoInputFracture(
                s3g::FractureProcessor::Logic, runtime, state,
                input, input, 0.80f, 0.60f, 0.0f);
            if (!std::isfinite(output)) {
                std::cerr << "No Input Mixer LOGIC insert became non-finite\n";
                return false;
            }
            if (sample >= warmup) {
                sumSquares += static_cast<double>(output) * output;
                maximumStep = std::max(maximumStep,
                    std::abs(output - previous));
            }
            previous = output;
        }
        const double rms = std::sqrt(sumSquares
            / static_cast<double>(measured));
        if (!(rms > 0.05) || maximumStep > 0.10f) {
            std::cerr << "No Input Mixer correlated LOGIC insert was silent "
                         "or clicky: RMS "
                      << rms << " step " << maximumStep << "\n";
            return false;
        }
    }

    // Full-depth CRUSH must quantize a quiet feed rather than interpreting
    // every sample as zero against a fixed full-scale step size.
    {
        NoInputFractureTestRuntime runtime;
        NoInputFractureTestState state;
        double sumSquares = 0.0;
        constexpr uint32_t warmup = 24000u;
        constexpr uint32_t measured = 48000u;
        for (uint32_t sample = 0u; sample < warmup + measured; ++sample) {
            const float input = 0.005f * std::sin(2.0f * s3g::kPi
                * 197.0f * static_cast<float>(sample) / 48000.0f);
            const float output = processNoInputFracture(
                s3g::FractureProcessor::Crush, runtime, state,
                input, input, 1.0f, 0.55f, 0.0f);
            if (!std::isfinite(output)) {
                std::cerr << "No Input Mixer CRUSH insert became non-finite\n";
                return false;
            }
            if (sample >= warmup)
                sumSquares += static_cast<double>(output) * output;
        }
        const double rms = std::sqrt(sumSquares
            / static_cast<double>(measured));
        if (!(rms > 0.001)) {
            std::cerr << "No Input Mixer CRUSH insert erased a quiet feed: "
                      << rms << " RMS\n";
            return false;
        }
    }

    // LENGTH automation is latched at a splice boundary. The edit itself
    // must not teleport the read head, and the following splice remains
    // bounded while the newly requested short segment takes over.
    {
        NoInputFractureTestRuntime changedRuntime;
        NoInputFractureTestRuntime controlRuntime;
        NoInputFractureTestState changedState;
        NoInputFractureTestState controlState;
        float previous = 0.0f;
        for (uint32_t sample = 0u; sample < 48000u; ++sample) {
            const float input = 0.18f * std::sin(2.0f * s3g::kPi
                * 173.0f * static_cast<float>(sample) / 48000.0f);
            previous = processNoInputFracture(
                s3g::FractureProcessor::Splice, changedRuntime,
                changedState, input, input, 0.92f, 0.92f, 0.30f);
            processNoInputFracture(s3g::FractureProcessor::Splice,
                controlRuntime, controlState, input, input,
                0.92f, 0.92f, 0.30f);
        }
        float firstSampleDifference = 0.0f;
        float maximumStep = 0.0f;
        for (uint32_t offset = 0u; offset < 24000u; ++offset) {
            const uint32_t sample = 48000u + offset;
            const float input = 0.18f * std::sin(2.0f * s3g::kPi
                * 173.0f * static_cast<float>(sample) / 48000.0f);
            const float changed = processNoInputFracture(
                s3g::FractureProcessor::Splice, changedRuntime,
                changedState, input, input, 0.92f, 0.02f, 0.30f);
            const float control = processNoInputFracture(
                s3g::FractureProcessor::Splice, controlRuntime,
                controlState, input, input, 0.92f, 0.92f, 0.30f);
            if (offset == 0u)
                firstSampleDifference = std::abs(changed - control);
            maximumStep = std::max(maximumStep,
                std::abs(changed - previous));
            previous = changed;
        }
        if (firstSampleDifference > 1.0e-6f || maximumStep > 0.12f) {
            std::cerr << "No Input Mixer SPLICE LENGTH automation clicked: "
                      << firstSampleDifference << " immediate, "
                      << maximumStep << " maximum step\n";
            return false;
        }
    }

    // CHARACTER now morphs through continuous carrier shapes. Check both the
    // neutral sine and the most square-like endpoint against the former hard
    // square edge without over-constraining their intended brightness.
    const auto robotMaximumStep = [](float bias) {
        NoInputFractureTestRuntime runtime;
        NoInputFractureTestState state;
        float previous = 0.0f;
        float maximumStep = 0.0f;
        for (uint32_t sample = 0u; sample < 72000u; ++sample) {
            const float input = 0.20f * std::sin(2.0f * s3g::kPi
                * 173.0f * static_cast<float>(sample) / 48000.0f);
            const float output = processNoInputFracture(
                s3g::FractureProcessor::Robot, runtime, state,
                input, input, 1.0f, 0.65f, bias);
            if (!std::isfinite(output))
                return std::numeric_limits<float>::infinity();
            if (sample >= 24000u) {
                maximumStep = std::max(maximumStep,
                    std::abs(output - previous));
            }
            previous = output;
        }
        return maximumStep;
    };
    const float neutralRobotStep = robotMaximumStep(0.0f);
    const float brightRobotStep = robotMaximumStep(1.0f);
    if (neutralRobotStep > 0.12f || brightRobotStep > 0.32f) {
        std::cerr << "No Input Mixer ROBOT carrier produced a hard edge: "
                  << neutralRobotStep << " / " << brightRobotStep << "\n";
        return false;
    }
    return true;
}

s3g::NoInputMixerParams fractureSelectorContinuityFixture()
{
    auto params = s3g::defaultNoInputMixerParams();
    params.outputGainDb = 6.0f;
    params.ceilingDb = 0.0f;
    params.limiterEnabled = 0u;
    params.feedback = 0.96f;
    params.coupling = 0.0f;
    params.phase = 0.0f;
    params.drift = 0.0f;
    params.formant = 0.0f;
    params.agency = 0.0f;
    params.space = 0.0f;
    params.variance = 0.0f;
    params.motion = 0.0f;
    params.reactMode = s3g::NoInputReactMode::Off;
    params.matrix.fill(0.0f);
    params.matrix[0] = 1.0f;
    for (uint32_t lane = 0u; lane < params.lanes.size(); ++lane) {
        auto& voice = params.lanes[lane];
        voice.mute = lane == 0u ? 0u : 1u;
        voice.pitchLock = 1u;
        voice.tuneNote = 48.0f;
        voice.tuneCents = 0.0f;
        voice.loss = 0.08f;
        voice.body = 0.25f;
        voice.levelDb = 0.0f;
        voice.lowDb = 0.0f;
        voice.midGainDb = 0.0f;
        voice.highDb = -12.0f;
        voice.auxSend = {{ 0.0f, 0.0f }};
        voice.auxReturn = {{ 0.0f, 0.0f }};
        for (auto& insert : voice.inserts) insert.bypass = 1u;
    }
    auto& insert = params.lanes[0].inserts[0];
    insert.type = s3g::NoInputDistortionType::Relay;
    insert.gain = 0.72f;
    insert.tone = 0.55f;
    insert.bias = 0.0f;
    insert.levelDb = 0.0f;
    insert.bypass = 0u;
    for (auto& aux : params.aux) {
        aux.feedback = 0.0f;
        aux.returnGain = 0.0f;
    }
    return params;
}

struct FractureSelectorRetargetStats {
    float peak = 0.0f;
    float maximumSwitchStep = 0.0f;
    float maximumExpectedStep = 0.0f;
    float maximumExcessStep = 0.0f;
    bool finite = true;
};

FractureSelectorRetargetStats measureFractureSelectorRetargeting(
    bool auxiliary)
{
    auto params = fractureSelectorContinuityFixture();
    if (auxiliary) {
        params.lanes[0].inserts[0].bypass = 1u;
        params.lanes[0].auxSend[0] = 1.0f;
        params.lanes[0].auxTap[0] = s3g::NoInputAuxTap::Return;
        params.lanes[0].auxReturn[0] = 1.0f;
        params.aux[0].effect.type = s3g::NoInputDistortionType::Relay;
        params.aux[0].effect.gain = 0.72f;
        params.aux[0].effect.tone = 0.55f;
        params.aux[0].effect.bias = 0.0f;
        params.aux[0].effect.bypass = 0u;
        params.aux[0].feedback = 0.18f;
        params.aux[0].returnGain = 0.72f;
    }

    s3g::NoInputMixer mixer;
    mixer.prepare(48000.0);
    mixer.setParams(params);
    mixer.reseed(0x4e494d46u, 0.55f);
    Frame frame {};
    float previous = 0.0f;
    for (uint32_t sample = 0u; sample < 48000u; ++sample) {
        mixer.processFrame(frame.data());
        previous = frame[0];
    }

    constexpr std::array<s3g::NoInputDistortionType,
        s3g::kFractureProcessorCount> types {{
        s3g::NoInputDistortionType::Logic,
        s3g::NoInputDistortionType::Robot,
        s3g::NoInputDistortionType::Crush,
        s3g::NoInputDistortionType::Splice,
        s3g::NoInputDistortionType::Void,
        s3g::NoInputDistortionType::Throat,
        s3g::NoInputDistortionType::OctDown,
        s3g::NoInputDistortionType::OctUp,
        s3g::NoInputDistortionType::OctStack,
        s3g::NoInputDistortionType::Relay,
    }};
    FractureSelectorRetargetStats stats;
    for (uint32_t sample = 0u; sample < 12000u; ++sample) {
        const bool switchType = (sample % 64u) == 0u;
        Frame expected {};
        if (switchType) {
            auto control = std::make_unique<s3g::NoInputMixer>(mixer);
            control->processFrame(expected.data());
            const auto type = types[(sample / 64u) % types.size()];
            if (auxiliary) params.aux[0].effect.type = type;
            else params.lanes[0].inserts[0].type = type;
            mixer.setParams(params);
        }
        mixer.processFrame(frame.data());
        for (float value : frame) {
            if (!std::isfinite(value)) stats.finite = false;
        }
        stats.peak = std::max(stats.peak, std::abs(frame[0]));
        if (switchType) {
            const float actualStep = std::abs(frame[0] - previous);
            const float expectedStep = std::abs(expected[0] - previous);
            stats.maximumSwitchStep = std::max(
                stats.maximumSwitchStep, actualStep);
            stats.maximumExpectedStep = std::max(
                stats.maximumExpectedStep, expectedStep);
            stats.maximumExcessStep = std::max(
                stats.maximumExcessStep,
                std::max(0.0f, actualStep - expectedStep));
        }
        previous = frame[0];
    }
    return stats;
}

bool testRapidFractureInsertRetargeting()
{
    const auto lane = measureFractureSelectorRetargeting(false);
    const auto aux = measureFractureSelectorRetargeting(true);
    constexpr float maximumExcessStep = 0.002f;
    if (!lane.finite || !(lane.peak > 0.01f)
        || lane.maximumExcessStep > maximumExcessStep) {
        std::cerr << "No Input Mixer lane insert rapid retarget clicked: peak "
                  << lane.peak << " switch " << lane.maximumSwitchStep
                  << " expected " << lane.maximumExpectedStep
                  << " excess " << lane.maximumExcessStep << "\n";
        return false;
    }
    if (!aux.finite || !(aux.peak > 0.01f)
        || aux.maximumExcessStep > maximumExcessStep) {
        std::cerr << "No Input Mixer aux insert rapid retarget clicked: peak "
                  << aux.peak << " switch " << aux.maximumSwitchStep
                  << " expected " << aux.maximumExpectedStep
                  << " excess " << aux.maximumExcessStep << "\n";
        return false;
    }
    return true;
}

bool testFractureSelectorFadeTiming()
{
    constexpr double sampleRate = 48000.0;
    constexpr uint32_t expectedSamples = 960u;
    uint32_t referenceLaneSamples = 0u;
    uint32_t referenceAuxSamples = 0u;
    for (uint32_t quality = 0u; quality <= 2u; ++quality) {
        for (uint32_t path = 0u; path < 2u; ++path) {
            const bool auxiliary = path != 0u;
            auto params = fractureSelectorContinuityFixture();
            params.quality = quality;
            if (auxiliary) {
                params.lanes[0].inserts[0].bypass = 1u;
                params.lanes[0].auxSend[0] = 1.0f;
                params.lanes[0].auxTap[0] = s3g::NoInputAuxTap::Return;
                params.lanes[0].auxReturn[0] = 1.0f;
                params.aux[0].effect.type =
                    s3g::NoInputDistortionType::Relay;
                params.aux[0].effect.gain = 0.72f;
                params.aux[0].effect.tone = 0.55f;
                params.aux[0].effect.bias = 0.0f;
                params.aux[0].effect.bypass = 0u;
                params.aux[0].feedback = 0.18f;
                params.aux[0].returnGain = 0.72f;
            }

            auto mixer = std::make_unique<s3g::NoInputMixer>();
            mixer->prepare(sampleRate);
            mixer->setParams(params);
            mixer->reseed(0x51464144u + quality * 17u + path, 0.55f);
            Frame frame {};
            for (uint32_t sample = 0u; sample < 4096u; ++sample)
                mixer->processFrame(frame.data());

            if (auxiliary) {
                params.aux[0].effect.type =
                    s3g::NoInputDistortionType::Robot;
            } else {
                params.lanes[0].inserts[0].type =
                    s3g::NoInputDistortionType::Robot;
            }
            mixer->setParams(params);
            const auto progress = [&]() {
                return auxiliary
                    ? mixer->auxInsertTransitionProgress(0u)
                    : mixer->insertTransitionProgress(0u, 0u);
            };
            if (progress() != 0.0f) {
                std::cerr << "No Input Mixer selector fade did not start at "
                          << "zero for quality " << quality << "\n";
                return false;
            }

            uint32_t completionSamples = 0u;
            while (progress() < 1.0f
                && completionSamples <= expectedSamples + 1u) {
                mixer->processFrame(frame.data());
                ++completionSamples;
            }
            if (completionSamples < expectedSamples
                || completionSamples > expectedSamples + 1u
                || progress() != 1.0f) {
                std::cerr << "No Input Mixer "
                          << (auxiliary ? "aux" : "lane")
                          << " selector fade duration regressed at quality "
                          << quality << ": " << completionSamples
                          << " samples\n";
                return false;
            }
            uint32_t& reference = auxiliary
                ? referenceAuxSamples : referenceLaneSamples;
            if (quality == 0u) reference = completionSamples;
            else if (completionSamples != reference) {
                std::cerr << "No Input Mixer "
                          << (auxiliary ? "aux" : "lane")
                          << " selector fade changed with quality: "
                          << reference << " vs " << completionSamples
                          << " samples\n";
                return false;
            }
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
        || s3g::noInputMixerFactoryBehavior(2u).behavior
            != s3g::NoInputMovementBehavior::Erode
        || s3g::noInputMixerFactoryBehavior(4u).behavior
            != s3g::NoInputMovementBehavior::Burst
        || s3g::noInputMixerFactoryBehavior(5u).behavior
            != s3g::NoInputMovementBehavior::Scramble
        || s3g::noInputMixerFactoryBehavior(7u).behavior
            != s3g::NoInputMovementBehavior::Ratchet
        || s3g::noInputMixerFactoryBehavior(9u).behavior
            != s3g::NoInputMovementBehavior::Cascade
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
        || rain.motionShape != s3g::MatrixFlowShape::Attract
        || s3g::noInputMixerFactoryPreset(7u).motionShape
            != s3g::MatrixFlowShape::Bloom
        || s3g::noInputMixerFactoryPreset(9u).motionShape
            != s3g::MatrixFlowShape::Braid
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
        || randomA.clockSync != 0u
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

    for (uint32_t seed = 1u; seed <= 512u; ++seed) {
        const auto high = s3g::randomizedNoInputMovementBehaviorParams(
            seed, Energy::High);
        const auto mid = s3g::randomizedNoInputMovementBehaviorParams(
            seed, Energy::Mid);
        const auto low = s3g::randomizedNoInputMovementBehaviorParams(
            seed, Energy::Low);
        const float highLengthMs = s3g::noInputMovementLengthMs(high.length);
        const float highEdgeMs = s3g::noInputMovementSlewMs(high.slew);
        const float midLengthMs = s3g::noInputMovementLengthMs(mid.length);
        if (highLengthMs < 5.999f || highLengthMs > 40.001f
            || highEdgeMs < 0.999f || highEdgeMs > 2.001f
            || midLengthMs < 19.999f || midLengthMs > 140.001f
            || high.behavior < s3g::NoInputMovementBehavior::Cut
            || mid.behavior < s3g::NoInputMovementBehavior::Step
            || low.behavior != s3g::NoInputMovementBehavior::Glide
            || low.choke != 0.0f) {
            std::cerr << "No Input Mixer random articulation profile escaped "
                         "its behavior or window range\n";
            return false;
        }
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
                || params.clockSync != 0u
                || repeat.clockSync != 0u
                || params.controllerHold != 0u) {
                std::cerr << "No Input Mixer energy randomization was not "
                             "deterministic, free-running, and controller-safe\n";
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
                        == s3g::NoInputMovementBehavior::Glide
                    && behavior.choke == 0.0f
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

    for (const auto shape : { s3g::MatrixFlowShape::Bloom,
             s3g::MatrixFlowShape::Braid,
             s3g::MatrixFlowShape::Attract }) {
        auto extended = params;
        extended.motionShape = shape;
        extended.flow = 0.78f;
        extended.spread = 0.36f;
        extended.vortex = 0.42f;
        const auto first = s3g::noInputMixerMotionWeights(extended, 0.08f);
        const auto second = s3g::noInputMixerMotionWeights(extended, 0.43f);
        if (first == second) {
            std::cerr << "Extended Field did not move: "
                      << s3g::matrixFlowShapeName(shape) << "\n";
            return false;
        }
        for (uint32_t source = 0u;
             source < s3g::kNoInputMixerChannels; ++source) {
            float columnSum = 0.0f;
            for (uint32_t destination = 0u;
                 destination < s3g::kNoInputMixerChannels; ++destination) {
                const float weight = first[destination
                    * s3g::kNoInputMixerChannels + source];
                if (!std::isfinite(weight) || weight < 0.0f
                    || weight > 1.0001f) {
                    std::cerr << "Extended Field escaped bounds: "
                              << s3g::matrixFlowShapeName(shape) << "\n";
                    return false;
                }
                columnSum += weight;
            }
            if (std::abs(columnSum - 1.0f) > 1.0e-4f) {
                std::cerr << "Extended Field lost column normalization: "
                          << s3g::matrixFlowShapeName(shape) << " / "
                          << columnSum << "\n";
                return false;
            }
        }
    }
    if (std::strcmp(s3g::matrixFlowShapeName(
                s3g::MatrixFlowShape::Bloom), "BLOOM") != 0
        || std::strcmp(s3g::matrixFlowShapeName(
                s3g::MatrixFlowShape::Braid), "BRAID") != 0
        || std::strcmp(s3g::matrixFlowShapeName(
                s3g::MatrixFlowShape::Attract), "ATTRACT") != 0
        || std::strcmp(s3g::noInputMovementBehaviorName(
                s3g::NoInputMovementBehavior::Ratchet), "RATCHET") != 0
        || std::strcmp(s3g::noInputMovementBehaviorName(
                s3g::NoInputMovementBehavior::Cascade), "CASCADE") != 0
        || std::strcmp(s3g::noInputMovementBehaviorName(
                s3g::NoInputMovementBehavior::Erode), "ERODE") != 0) {
        std::cerr << "Extended Field or Behavior names regressed\n";
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
        std::cerr << "Global aux mutes did not silence both return buses: "
                  << mutedDifference << " / "
                  << mutedAux.auxActivity(0u) << " / "
                  << mutedAux.auxActivity(1u) << "\n";
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
    auto normalDirection = s3g::defaultNoInputMixerParams();
    normalDirection.reactPolarity = 0.0f;
    normalDirection = s3g::sanitizeNoInputMixerParams(normalDirection);
    auto invertedDirection = normalDirection;
    invertedDirection.reactPolarity = -0.25f;
    invertedDirection = s3g::sanitizeNoInputMixerParams(invertedDirection);
    if (std::abs(s3g::noInputMovementEventRateHz(0.0f) - 0.25f)
            > 1.0e-6f
        || std::abs(s3g::noInputMovementEventRateHz(1.0f) - 80.0f)
            > 1.0e-3f
        || std::abs(s3g::noInputMovementSlewMs(0.0f) - 0.5f)
            > 1.0e-6f
        || std::abs(s3g::noInputMovementSlewMs(1.0f) - 20.0f)
            > 1.0e-4f
        || normalDirection.reactPolarity != 0.0f
        || invertedDirection.reactPolarity != -1.0f
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

    s3g::NoInputMixer depthControl;
    depthControl.prepare(48000.0);
    depthControl.setMovementBehaviorDepth(1.5f);
    if (depthControl.movementBehaviorDepth() != 1.0f) {
        std::cerr << "Behavior Depth did not clamp independently\n";
        return false;
    }
    depthControl.setMovementBehaviorDepth(-0.5f);
    if (depthControl.movementBehaviorDepth() != 0.0f) {
        std::cerr << "Behavior Depth did not reach its neutral state\n";
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

    for (uint32_t mode = 1u;
         mode < s3g::kNoInputMovementBehaviorCount; ++mode) {
        s3g::NoInputMovementBehaviorParams behavior;
        behavior.behavior = static_cast<s3g::NoInputMovementBehavior>(mode);
        behavior.eventRate = 0.74f;
        behavior.length = 0.16f;
        behavior.density = 0.48f;
        behavior.chaos = 0.72f;
        behavior.slew = 0.0f;
        behavior.choke = 1.0f;
        s3g::NoInputMixer mixer;
        mixer.prepare(48000.0);
        mixer.setParams(params);
        mixer.setMovementBehaviorParams(behavior);
        mixer.reseed(0x43564d58u, 0.72f);
        Frame frame {};
        float minimumGate = 1.0f;
        float maximumGate = 0.0f;
        float maximumGateDelta = 0.0f;
        std::array<float, s3g::kNoInputMixerMatrixCells> previousGate {};
        previousGate.fill(1.0f);
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
                maximumGateDelta = std::max(maximumGateDelta,
                    std::abs(gate - previousGate[route]));
                previousGate[route] = gate;
            }
            if (sample > 2000u && framePeak < 1.0e-4f) ++silentFrames;
            if (std::abs(mixer.routeSignal(closedRoute)) > 1.0e-9f) {
                std::cerr << "Movement behavior invented a closed route\n";
                return false;
            }
        }
        const float requiredMinimum = behavior.behavior
                == s3g::NoInputMovementBehavior::Erode
            ? 0.75f : 0.12f;
        if (!(minimumGate < requiredMinimum) || !(maximumGate > 0.45f)) {
            std::cerr << "Movement behavior did not articulate routes: "
                      << s3g::noInputMovementBehaviorName(behavior.behavior)
                      << " min " << minimumGate << " max " << maximumGate
                      << "\n";
            return false;
        }
        const float maximumAllowedDelta =
            s3g::noInputMovementBehaviorUsesAmplitude(behavior.behavior)
            ? 0.025f : 0.10f;
        if (maximumGateDelta > maximumAllowedDelta) {
            std::cerr << "Movement behavior window discontinuity: "
                      << s3g::noInputMovementBehaviorName(behavior.behavior)
                      << " delta " << maximumGateDelta << "\n";
            return false;
        }
        if ((behavior.behavior == s3g::NoInputMovementBehavior::Cut
                || behavior.behavior == s3g::NoInputMovementBehavior::Burst
                || behavior.behavior
                    == s3g::NoInputMovementBehavior::Ratchet
                || behavior.behavior
                    == s3g::NoInputMovementBehavior::Cascade)
            && silentFrames < 64u) {
            std::cerr << "Post-insert CHOKE did not expose cut gaps: "
                      << silentFrames << " frames\n";
            return false;
        }
    }

    s3g::NoInputMovementBehaviorParams scrambleA;
    scrambleA.behavior = s3g::NoInputMovementBehavior::Scramble;
    scrambleA.eventRate = 0.78f;
    scrambleA.length = 0.0f;
    scrambleA.density = 0.52f;
    scrambleA.chaos = 0.74f;
    scrambleA.slew = 0.08f;
    scrambleA.choke = 0.5f;
    auto scrambleB = scrambleA;
    scrambleB.length = 1.0f;
    s3g::NoInputMixer first;
    s3g::NoInputMixer second;
    first.prepare(48000.0);
    second.prepare(48000.0);
    first.setParams(params);
    second.setParams(params);
    first.setMovementBehaviorParams(scrambleA);
    second.setMovementBehaviorParams(scrambleB);
    first.reseed(0x5343524du, 0.72f);
    second.reseed(0x5343524du, 0.72f);
    Frame firstFrame {};
    Frame secondFrame {};
    for (uint32_t sample = 0u; sample < 12000u; ++sample) {
        first.processFrame(firstFrame.data());
        second.processFrame(secondFrame.data());
        for (uint32_t route = 0u;
             route < s3g::kNoInputMixerMatrixCells; ++route) {
            if (std::abs(first.behaviorRouteGate(route)
                    - second.behaviorRouteGate(route)) > 1.0e-7f) {
                std::cerr << "SCRAMBLE unexpectedly depends on LENGTH\n";
                return false;
            }
        }
    }
    return true;
}

bool testNeutralMovementBehaviorAndTransitions()
{
    auto params = s3g::noInputMixerFactoryPreset(9u);
    params.motion = 0.86f;
    params.motionRate = 0.63f;
    uint32_t activeRoute = 0u;
    while (activeRoute < params.matrix.size()
        && std::abs(params.matrix[activeRoute]) <= 1.0e-7f) {
        ++activeRoute;
    }
    if (activeRoute >= params.matrix.size()) return false;

    s3g::NoInputMovementBehaviorParams glide;
    glide.behavior = s3g::NoInputMovementBehavior::Glide;
    glide.eventRate = 0.82f;
    glide.length = 0.21f;
    glide.density = 0.37f;
    glide.chaos = 0.91f;
    glide.slew = 0.74f;
    glide.choke = 1.0f;
    auto neutralCut = glide;
    neutralCut.behavior = s3g::NoInputMovementBehavior::Cut;
    neutralCut.choke = 0.0f;

    s3g::NoInputMixer glideMixer;
    s3g::NoInputMixer neutralMixer;
    glideMixer.prepare(48000.0);
    neutralMixer.prepare(48000.0);
    glideMixer.setParams(params);
    neutralMixer.setParams(params);
    glideMixer.setMovementBehaviorParams(glide);
    neutralMixer.setMovementBehaviorParams(neutralCut);
    glideMixer.setMovementBehaviorDepth(1.0f);
    neutralMixer.setMovementBehaviorDepth(0.0f);
    glideMixer.reset();
    neutralMixer.reset();
    constexpr uint32_t seed = 0x4e455554u;
    glideMixer.reseed(seed, 0.68f);
    neutralMixer.reseed(seed, 0.68f);

    Frame glideFrame {};
    Frame neutralFrame {};
    for (uint32_t sample = 0u; sample < 24000u; ++sample) {
        glideMixer.processFrame(glideFrame.data());
        neutralMixer.processFrame(neutralFrame.data());
        for (uint32_t lane = 0u; lane < glideFrame.size(); ++lane) {
            if (!std::isfinite(neutralFrame[lane])
                || std::abs(glideFrame[lane] - neutralFrame[lane])
                    > 1.0e-7f) {
                std::cerr << "Zero-depth Behavior was not transparent at "
                          << sample << " lane " << lane + 1u << "\n";
                return false;
            }
        }
    }

    // Turning the dormant behavior on must enter through the transparent
    // state. The cached vactrol path may then close, but cannot jump there.
    neutralMixer.setMovementBehaviorDepth(1.0f);
    float previousGate = 1.0f;
    float minimumGate = 1.0f;
    float maximumGateDelta = 0.0f;
    for (uint32_t sample = 0u; sample < 24000u; ++sample) {
        neutralMixer.processFrame(neutralFrame.data());
        for (float value : neutralFrame) {
            if (!std::isfinite(value) || std::abs(value) > 1.01f) {
                std::cerr << "Behavior activation produced invalid audio\n";
                return false;
            }
        }
        const float gate = neutralMixer.behaviorRouteGate(activeRoute);
        minimumGate = std::min(minimumGate, gate);
        maximumGateDelta = std::max(maximumGateDelta,
            std::abs(gate - previousGate));
        previousGate = gate;
    }
    if (!(minimumGate < 0.95f) || maximumGateDelta > 0.025f) {
        std::cerr << "Behavior activation did not use a smooth circuit ramp: "
                  << minimumGate << " min, " << maximumGateDelta
                  << " delta\n";
        return false;
    }

    neutralMixer.setMovementBehaviorParams(glide);
    for (uint32_t sample = 0u; sample < 12000u; ++sample) {
        neutralMixer.processFrame(neutralFrame.data());
        for (float value : neutralFrame) {
            if (!std::isfinite(value) || std::abs(value) > 1.01f) {
                std::cerr << "Behavior bypass transition produced invalid "
                             "audio\n";
                return false;
            }
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

bool testMidiMatrixGrid()
{
    std::array<uint32_t, s3g::kNoInputMixerMatrixCells> visits {};
    for (uint8_t channel = 0u;
         channel < s3g::kNoInputMatrixGridChannels; ++channel) {
        for (uint8_t note = 0u;
             note < s3g::kNoInputMatrixGridNotesPerController; ++note) {
            uint32_t destination = 0u;
            uint32_t source = 0u;
            if (!s3g::decodeNoInputMatrixGridNote(
                    channel, note, destination, source)
                || destination >= s3g::kNoInputMixerChannels
                || source >= s3g::kNoInputMixerChannels) {
                std::cerr << "No Input Mixer MIDI grid decode failed\n";
                return false;
            }
            uint8_t encodedChannel = 0u;
            uint8_t encodedNote = 0u;
            if (!s3g::encodeNoInputMatrixGridPoint(
                    destination, source, encodedChannel, encodedNote)
                || encodedChannel != channel || encodedNote != note) {
                std::cerr << "No Input Mixer MIDI grid encode failed\n";
                return false;
            }
            ++visits[destination * s3g::kNoInputMixerChannels + source];
        }
    }
    if (!std::all_of(visits.begin(), visits.end(), [](uint32_t count) {
            return count == 1u;
        })) {
        std::cerr << "No Input Mixer MIDI grid did not cover 8x8 once\n";
        return false;
    }
    if (s3g::encodeNoInputMatrixFeedbackValue(-1.0f) != 0u
        || s3g::encodeNoInputMatrixFeedbackValue(-0.5f) != 32u
        || s3g::encodeNoInputMatrixFeedbackValue(0.0f) != 64u
        || s3g::encodeNoInputMatrixFeedbackValue(0.5f) != 96u
        || s3g::encodeNoInputMatrixFeedbackValue(1.0f) != 127u
        || std::abs(s3g::decodeNoInputMatrixFeedbackValue(0u) + 1.0f)
            > 1.0e-6f
        || s3g::decodeNoInputMatrixFeedbackValue(32u) >= 0.0f
        || s3g::decodeNoInputMatrixFeedbackValue(64u) != 0.0f
        || s3g::decodeNoInputMatrixFeedbackValue(96u) <= 0.0f
        || std::abs(s3g::decodeNoInputMatrixFeedbackValue(127u) - 1.0f)
            > 1.0e-6f) {
        std::cerr << "No Input Mixer signed matrix feedback encoding failed\n";
        return false;
    }
    if (std::abs(s3g::noInputMatrixMidiGain(
            s3g::NoInputMatrixMidiMode::Flip, 0.8f, 0u) - 0.8f)
            > 1.0e-6f
        || std::abs(s3g::noInputMatrixMidiGain(
            s3g::NoInputMatrixMidiMode::Flip, 0.8f, 127u) + 1.0f)
            > 1.0e-6f
        || std::abs(s3g::noInputMatrixMidiGain(
            s3g::NoInputMatrixMidiMode::Flip, -0.8f, 127u) - 1.0f)
            > 1.0e-6f
        || s3g::noInputMatrixMidiGain(
            s3g::NoInputMatrixMidiMode::Flip, 0.0f, 127u) != 0.0f
        || std::abs(s3g::noInputMatrixMidiGain(
            s3g::NoInputMatrixMidiMode::Latch, -0.5f, 64u)
            - 64.0f / 127.0f) > 1.0e-6f
        || std::abs(s3g::noInputMatrixMidiGain(
            s3g::NoInputMatrixMidiMode::Latch, 0.0f, 127u,
            s3g::NoInputMatrixMidiSign::Negative) + 1.0f) > 1.0e-6f
        || std::strcmp(s3g::noInputMatrixMidiModeName(
            s3g::NoInputMatrixMidiMode::Latch), "LATCH") != 0
        || std::strcmp(s3g::noInputMatrixMidiSignName(
            s3g::NoInputMatrixMidiSign::Negative), "NEGATIVE") != 0) {
        std::cerr << "No Input Mixer MIDI matrix mode gain failed\n";
        return false;
    }

    auto params = s3g::defaultNoInputMixerParams();
    constexpr uint32_t destination = 5u;
    constexpr uint32_t source = 6u;
    const uint32_t index = destination * s3g::kNoInputMixerChannels + source;
    params.matrix[index] = -0.42f;
    s3g::NoInputMixer mixer;
    mixer.prepare(48000.0);
    mixer.setParams(params);
    if (mixer.midiMatrixRampMs()
            != s3g::kNoInputMatrixMidiRampDefaultMs) {
        std::cerr << "No Input Mixer MIDI matrix ramp default failed\n";
        return false;
    }
    mixer.setMidiMatrixConnection(destination, source, -0.75f);
    std::array<float, s3g::kNoInputMixerChannels> frame {};
    for (uint32_t sample = 0u; sample < 48000u; ++sample) {
        mixer.processFrame(frame.data());
    }
    const float halfway = mixer.effectiveMatrixGain(destination, source);
    if (!(halfway < -0.60f && halfway > -0.75f)) {
        std::cerr << "No Input Mixer MIDI matrix 1000 ms ramp failed: "
                  << halfway << "\n";
        return false;
    }
    mixer.clearMidiMatrixConnections();
    mixer.setMidiMatrixRampMs(s3g::kNoInputMatrixMidiRampMinimumMs);
    mixer.setMidiMatrixConnection(destination, source, -0.75f);
    if (std::abs(mixer.effectiveMatrixGain(destination, source) + 0.42f)
            > 1.0e-6f
        || !mixer.midiMatrixConnectionActive(destination, source)
        || std::abs(mixer.params().matrix[index] + 0.42f) > 1.0e-6f) {
        std::cerr << "No Input Mixer MIDI matrix overlay changed base state\n";
        return false;
    }
    for (uint32_t sample = 0u; sample < 12000u; ++sample) {
        mixer.processFrame(frame.data());
    }
    if (std::abs(mixer.effectiveMatrixGain(destination, source) + 0.75f)
        > 1.0e-3f) {
        std::cerr << "No Input Mixer MIDI matrix attack did not converge\n";
        return false;
    }
    mixer.releaseMidiMatrixConnection(destination, source);
    if (!mixer.midiMatrixConnectionActive(destination, source)
        || std::abs(mixer.effectiveMatrixGain(destination, source) + 0.75f)
            > 1.0e-3f) {
        std::cerr << "No Input Mixer MIDI matrix release was not slewed\n";
        return false;
    }
    for (uint32_t sample = 0u; sample < 12000u; ++sample) {
        mixer.processFrame(frame.data());
    }
    if (mixer.midiMatrixConnectionActive(destination, source)
        || std::abs(mixer.effectiveMatrixGain(destination, source) + 0.42f)
            > 1.0e-6f) {
        std::cerr << "No Input Mixer MIDI matrix release did not restore base\n";
        return false;
    }
    mixer.setMidiMatrixConnection(destination, source, 0.9f);
    mixer.panic();
    if (std::abs(mixer.effectiveMatrixGain(destination, source) + 0.42f)
        > 1.0e-6f) {
        std::cerr << "No Input Mixer PANIC did not clear MIDI matrix overlay\n";
        return false;
    }
    return true;
}

bool testLiveParameterDezippering()
{
    s3g::NoInputMixer mixer;
    mixer.prepare(48000.0);
    std::array<float, s3g::kNoInputMixerChannels> frame {};

    // Once processing has begun, continuous parameter changes must ramp. This
    // is the final audio-rate stage behind the Parameter Surface's control-rate
    // cursor and protects the feedback network from coefficient steps.
    mixer.processFrame(frame.data());
    auto params = mixer.params();
    constexpr uint32_t destination = 0u;
    constexpr uint32_t source = 1u;
    constexpr uint32_t index = destination
        * s3g::kNoInputMixerChannels + source;
    params.matrix[index] = 1.0f;
    mixer.setParams(params);
    if (mixer.params().matrix[index] != 1.0f
        || mixer.effectiveMatrixGain(destination, source) != 0.0f) {
        std::cerr << "Live parameter target bypassed matrix dezipper\n";
        return false;
    }
    mixer.processFrame(frame.data());
    const float firstPositive = mixer.effectiveMatrixGain(
        destination, source);
    if (!(firstPositive > 0.0f && firstPositive < 0.05f)) {
        std::cerr << "Live matrix attack was discontinuous: "
                  << firstPositive << "\n";
        return false;
    }
    float previous = firstPositive;
    for (uint32_t sample = 1u; sample < 960u; ++sample) {
        mixer.processFrame(frame.data());
        const float current = mixer.effectiveMatrixGain(
            destination, source);
        if (current + 1.0e-7f < previous) {
            std::cerr << "Live matrix attack was not monotonic\n";
            return false;
        }
        previous = current;
    }
    if (previous < 0.998f) {
        std::cerr << "Live matrix attack did not settle: "
                  << previous << "\n";
        return false;
    }

    params.matrix[index] = -1.0f;
    mixer.setParams(params);
    const float beforeCrossing = mixer.effectiveMatrixGain(
        destination, source);
    mixer.processFrame(frame.data());
    const float firstCrossing = mixer.effectiveMatrixGain(
        destination, source);
    if (!(firstCrossing < beforeCrossing && firstCrossing > 0.90f)) {
        std::cerr << "Signed live matrix crossing jumped: "
                  << beforeCrossing << " -> " << firstCrossing << "\n";
        return false;
    }
    for (uint32_t sample = 1u; sample < 960u; ++sample) {
        mixer.processFrame(frame.data());
    }
    if (mixer.effectiveMatrixGain(destination, source) > -0.996f) {
        std::cerr << "Signed live matrix crossing did not settle\n";
        return false;
    }
    return true;
}

bool testAtomicWallEngineRecall()
{
    const auto wall = s3g::noInputMixerFactoryPreset(19u);
    const auto wallBehavior = s3g::noInputMixerFactoryBehavior(19u);
    s3g::NoInputMixer fresh;
    s3g::NoInputMixer recalled;
    fresh.prepare(48000.0);
    recalled.prepare(48000.0);

    fresh.setParams(wall);
    fresh.setMovementBehaviorParams(wallBehavior);
    fresh.setMovementBehaviorDepth(wall.motion);
    fresh.reset();
    fresh.reseed(wall.seed, 0.68f);

    const auto previous = s3g::noInputMixerFactoryPreset(4u);
    recalled.setParams(previous);
    recalled.setMovementBehaviorParams(
        s3g::noInputMixerFactoryBehavior(4u));
    recalled.setMovementBehaviorDepth(previous.motion);
    recalled.reset();
    recalled.reseed(previous.seed, 0.68f);
    render(recalled, 4096u);

    // This is the complete-scene path used before a factory recall reseeds
    // the network. It must produce the same circuit as a fresh instance,
    // independent of the coefficients and nonlinear state of the old scene.
    recalled.setParams(wall);
    recalled.setMovementBehaviorParams(wallBehavior);
    recalled.setMovementBehaviorDepth(wall.motion);
    recalled.reset();
    recalled.reseed(wall.seed, 0.68f);

    Frame freshFrame {};
    Frame recalledFrame {};
    float peak = 0.0f;
    for (uint32_t sample = 0u; sample < 24000u; ++sample) {
        fresh.processFrame(freshFrame.data());
        recalled.processFrame(recalledFrame.data());
        for (uint32_t lane = 0u; lane < freshFrame.size(); ++lane) {
            peak = std::max(peak, std::abs(freshFrame[lane]));
            if (!std::isfinite(freshFrame[lane])
                || std::abs(freshFrame[lane] - recalledFrame[lane])
                    > 1.0e-7f) {
                std::cerr << "Wall Engine recall retained previous scene "
                             "state at sample " << sample << " lane "
                          << lane + 1u << "\n";
                return false;
            }
        }
    }
    if (peak <= 1.0e-5f || peak > 1.01f) {
        std::cerr << "Wall Engine atomic recall was not a stable audible "
                     "circuit: " << peak << "\n";
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
        || clean.motionShape != s3g::MatrixFlowShape::Attract
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
    if (!testSharedFractureInsertKernels()) return 1;
    if (!testRapidFractureInsertRetargeting()) return 1;
    if (!testFractureSelectorFadeTiming()) return 1;
    if (!testFactoryPresetsAndRandomization()) return 1;
    if (!testRandomEnergyProfiles()) return 1;
    if (!testSignedMatrixChangesState()) return 1;
    if (!testHybridControlEcology()) return 1;
    if (!testMovementBehaviors()) return 1;
    if (!testNeutralMovementBehaviorAndTransitions()) return 1;
    if (!testReactClockTuningAndAuxTopology()) return 1;
    if (!testMidiMatrixGrid()) return 1;
    if (!testLiveParameterDezippering()) return 1;
    if (!testAtomicWallEngineRecall()) return 1;
    if (!testPanic()) return 1;
    if (!testSanitization()) return 1;
    std::cout << "No Input Mixer smoke passed\n";
    return 0;
}
