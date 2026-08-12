#include "s3g_acapella_source_synth.h"
#include "s3g_acapella_ensemble_synth.h"
#include "s3g_acapella_pvoc_field.h"
#include "s3g_acapella_text_compiler.h"
#include "s3g_acapella_vocal_fx.h"
#include "s3g_vox_builder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 48000u;

struct Render {
    std::vector<float> signal;
    bool finished = false;
};

Render render(const s3g::AcapellaSourceParams& params,
    s3g::AcapellaVowel vowel, s3g::AcapellaOnset onset,
    float frequencyHz = 146.83f, uint32_t releaseFrame = 28800u,
    uint32_t frames = 48000u)
{
    s3g::AcapellaSourceSynth synth;
    synth.setParams(params);
    synth.prepare(kSampleRate);
    synth.trigger({ vowel, onset, frequencyHz, 0.86f, 420.0f });
    Render result { std::vector<float>(frames), false };
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        if (frame == releaseFrame) synth.release();
        result.signal[frame] = synth.processFrame();
    }
    result.finished = !synth.active();
    return result;
}

double energy(const std::vector<float>& signal,
    uint32_t begin = 0u, uint32_t end = std::numeric_limits<uint32_t>::max())
{
    begin = std::min<uint32_t>(begin, static_cast<uint32_t>(signal.size()));
    end = std::min<uint32_t>(end, static_cast<uint32_t>(signal.size()));
    double result = 0.0;
    for (uint32_t frame = begin; frame < end; ++frame) {
        const double sample = signal[frame];
        result += sample * sample;
    }
    return result;
}

double difference(const std::vector<float>& left,
    const std::vector<float>& right)
{
    double result = 0.0;
    const size_t count = std::min(left.size(), right.size());
    for (size_t index = 0u; index < count; ++index) {
        result += std::abs(static_cast<double>(left[index]) - right[index]);
    }
    return result;
}

double transientConcentration(const std::vector<float>& signal,
    uint32_t endFrame, uint32_t windowFrames)
{
    endFrame = std::min<uint32_t>(endFrame,
        static_cast<uint32_t>(signal.size()));
    windowFrames = std::max<uint32_t>(1u,
        std::min(windowFrames, endFrame));
    double total = 0.0;
    double window = 0.0;
    double maximum = 0.0;
    for (uint32_t frame = 0u; frame < endFrame; ++frame) {
        const double square = static_cast<double>(signal[frame]) * signal[frame];
        total += square;
        window += square;
        if (frame >= windowFrames) {
            const double old = signal[frame - windowFrames];
            window -= old * old;
        }
        maximum = std::max(maximum, window);
    }
    return maximum / std::max(total, 1.0e-20);
}

double maximumStep(const std::vector<float>& signal, uint32_t endFrame)
{
    endFrame = std::min<uint32_t>(endFrame,
        static_cast<uint32_t>(signal.size()));
    double previous = 0.0;
    double maximum = 0.0;
    for (uint32_t frame = 0u; frame < endFrame; ++frame) {
        maximum = std::max(maximum,
            std::abs(static_cast<double>(signal[frame]) - previous));
        previous = signal[frame];
    }
    return maximum;
}

bool silenceSafetyAndLifecycleProbe()
{
    s3g::AcapellaSourceSynth synth;
    s3g::AcapellaSourceParams invalid;
    invalid.voice.tractScale = std::numeric_limits<float>::infinity();
    invalid.voice.breath = -4.0f;
    invalid.voice.roughness = std::numeric_limits<float>::quiet_NaN();
    invalid.voice.harshness = 4.0f;
    invalid.voice.falseFold = -2.0f;
    invalid.voice.throat = std::numeric_limits<float>::infinity();
    invalid.intensity = 99.0f;
    invalid.releaseMs = -12.0f;
    invalid.waveguideBlend = 9.0f;
    invalid.coarticulation = std::numeric_limits<float>::quiet_NaN();
    invalid.intelligibility = 4.0f;
    invalid.retriggerMs = std::numeric_limits<float>::quiet_NaN();
    invalid.randomSeed = 0u;
    synth.setParams(invalid);
    synth.prepare(std::numeric_limits<double>::quiet_NaN());
    const auto sanitized = synth.params();
    if (sanitized.voice.tractScale != 1.0f
        || sanitized.voice.breath != 0.0f
        || sanitized.voice.roughness != 0.08f
        || sanitized.voice.harshness != 1.0f
        || sanitized.voice.falseFold != 0.0f
        || sanitized.voice.throat != 0.0f
        || sanitized.intensity != 1.0f
        || sanitized.releaseMs != 2.0f
        || sanitized.waveguideBlend != 1.0f
        || sanitized.coarticulation != 0.68f
        || sanitized.intelligibility != 1.0f
        || sanitized.retriggerMs != 6.0f
        || sanitized.randomSeed == 0u) {
        std::cerr << "acapella parameter sanitation failed\n";
        return false;
    }

    std::array<float, 64u> silent {};
    synth.processBlock(silent.data(), static_cast<uint32_t>(silent.size()));
    for (const float sample : silent) {
        if (sample != 0.0f) {
            std::cerr << "untriggered acapella source was not silent\n";
            return false;
        }
    }

    if (!synth.trigger({ s3g::AcapellaVowel::Schwa,
            s3g::AcapellaOnset::S,
            std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN() })) {
        std::cerr << "sanitized acapella syllable did not trigger\n";
        return false;
    }
    double totalEnergy = 0.0;
    for (uint32_t frame = 0u; frame < 24000u; ++frame) {
        const float sample = synth.processFrame();
        if (!std::isfinite(sample) || std::abs(sample) > 0.981f) {
            std::cerr << "acapella safety bound failed at " << frame << '\n';
            return false;
        }
        totalEnergy += static_cast<double>(sample) * sample;
    }
    synth.release();
    for (uint32_t frame = 0u; frame < 48000u && synth.active(); ++frame) {
        const float sample = synth.processFrame();
        if (!std::isfinite(sample)) return false;
    }
    if (!(totalEnergy > 0.01) || synth.active()) {
        std::cerr << "acapella source lifecycle failed\n";
        return false;
    }
    return true;
}

bool deterministicResetProbe()
{
    auto params = s3g::acapellaSourcePreset(
        s3g::AcapellaSourcePreset::NeutralSung);
    params.randomSeed = 8128u;
    s3g::AcapellaSourceSynth synth;
    synth.setParams(params);
    synth.prepare(kSampleRate);
    const auto capture = [&]() {
        std::array<float, 8192u> result {};
        synth.trigger({ s3g::AcapellaVowel::A, s3g::AcapellaOnset::M,
            164.81f, 0.81f, 360.0f });
        for (float& sample : result) sample = synth.processFrame();
        return result;
    };
    const auto first = capture();
    synth.reset();
    const auto second = capture();
    if (first != second) {
        std::cerr << "acapella reset was not deterministic\n";
        return false;
    }
    return true;
}

bool timbreAndArticulationProbe()
{
    auto params = s3g::acapellaSourcePreset(
        s3g::AcapellaSourcePreset::NeutralSung);
    params.voice.breath = 0.06f;
    params.voice.roughness = 0.03f;
    params.vibratoDepthCents = 0.0f;
    params.pitchDriftCents = 0.0f;
    params.onsetScoopSemitones = 0.0f;
    const auto vowelA = render(params, s3g::AcapellaVowel::A,
        s3g::AcapellaOnset::None);
    const auto vowelI = render(params, s3g::AcapellaVowel::I,
        s3g::AcapellaOnset::None);
    const auto vowelU = render(params, s3g::AcapellaVowel::U,
        s3g::AcapellaOnset::None);
    const auto acousticA = s3g::voxBuilderAnalyzeAcoustics(
        vowelA.signal, kSampleRate, 9600u, 26400u);
    const auto acousticI = s3g::voxBuilderAnalyzeAcoustics(
        vowelI.signal, kSampleRate, 9600u, 26400u);
    const auto acousticU = s3g::voxBuilderAnalyzeAcoustics(
        vowelU.signal, kSampleRate, 9600u, 26400u);
    if (!(acousticA.formant1Hz > acousticI.formant1Hz * 1.15f)
        || !(acousticI.formant2Hz > acousticU.formant2Hz * 1.25f)) {
        std::cerr << "acapella vowel separation failed: A "
                  << acousticA.formant1Hz << '/' << acousticA.formant2Hz
                  << ", I " << acousticI.formant1Hz << '/'
                  << acousticI.formant2Hz << ", U "
                  << acousticU.formant1Hz << '/' << acousticU.formant2Hz
                  << '\n';
        return false;
    }

    const auto fricative = render(params, s3g::AcapellaVowel::A,
        s3g::AcapellaOnset::S);
    const auto nasal = render(params, s3g::AcapellaVowel::A,
        s3g::AcapellaOnset::M);
    const auto acousticS = s3g::voxBuilderAnalyzeAcoustics(
        fricative.signal, kSampleRate, 0u, 6000u);
    const auto acousticM = s3g::voxBuilderAnalyzeAcoustics(
        nasal.signal, kSampleRate, 0u, 6000u);
    if (!(acousticS.onsetHighness > acousticM.onsetHighness + 0.008f)
        || !(acousticS.onsetTransient < 0.40f)) {
        std::cerr << "acapella consonant contrast failed: "
                  << acousticS.onsetHighness << " / "
                  << acousticM.onsetHighness << ", transient "
                  << acousticS.onsetTransient << '\n';
        return false;
    }

    const std::array<s3g::AcapellaOnset, 4u> plosives {
        s3g::AcapellaOnset::P,
        s3g::AcapellaOnset::T,
        s3g::AcapellaOnset::K,
        s3g::AcapellaOnset::Ch,
    };
    for (const auto onset : plosives) {
        const auto plosive = render(params, s3g::AcapellaVowel::A, onset);
        const double concentration = transientConcentration(
            plosive.signal, 6000u, 240u);
        const double step = maximumStep(plosive.signal, 6000u);
        if (!(concentration < 0.16) || !(step < 0.040)) {
            std::cerr << "non-vocal plosive transient: concentration "
                      << concentration << ", step " << step << '\n';
            return false;
        }
    }
    return true;
}

bool deliveryAndControlProbe()
{
    const auto sungParams = s3g::acapellaSourcePreset(
        s3g::AcapellaSourcePreset::NeutralSung);
    const auto rapParams = s3g::acapellaSourcePreset(
        s3g::AcapellaSourcePreset::RhythmicRap);
    const auto sung = render(sungParams, s3g::AcapellaVowel::Schwa,
        s3g::AcapellaOnset::T);
    const auto rap = render(rapParams, s3g::AcapellaVowel::Schwa,
        s3g::AcapellaOnset::T);
    const double sungEnergy = energy(sung.signal);
    const double rapEnergy = energy(rap.signal);
    const double deliveryDifference = difference(sung.signal, rap.signal);
    if (!(sungEnergy > 0.1) || !(rapEnergy > 0.1)
        || !(deliveryDifference > 100.0)
        || !sung.finished || !rap.finished) {
        std::cerr << "acapella sung/rap delivery contract failed: energy "
                  << sungEnergy << " / " << rapEnergy << ", difference "
                  << deliveryDifference << ", finished " << sung.finished
                  << " / " << rap.finished << '\n';
        return false;
    }

    s3g::AcapellaSourceSynth synth;
    synth.setParams(sungParams);
    synth.prepare(kSampleRate);
    synth.trigger({ s3g::AcapellaVowel::A, s3g::AcapellaOnset::None,
        110.0f, 0.8f, 500.0f });
    double firstEnergy = 0.0;
    double secondEnergy = 0.0;
    for (uint32_t frame = 0u; frame < 24000u; ++frame) {
        if (frame == 8000u) {
            synth.setFrequencyHz(220.0f);
            synth.setVowel(s3g::AcapellaVowel::I, 40.0f);
        }
        const float sample = synth.processFrame();
        if (frame < 8000u) firstEnergy += static_cast<double>(sample) * sample;
        else secondEnergy += static_cast<double>(sample) * sample;
    }
    if (!(firstEnergy > 0.01) || !(secondEnergy > 0.01)) {
        std::cerr << "live pitch/vowel controls muted the source\n";
        return false;
    }
    return true;
}

bool retriggerContinuityProbe()
{
    auto params = s3g::acapellaSourcePreset(
        s3g::AcapellaSourcePreset::DeathGrowl);
    params.retriggerMs = 8.0f;
    s3g::AcapellaSourceSynth synth;
    synth.setParams(params);
    synth.prepare(kSampleRate);
    synth.trigger({ s3g::AcapellaVowel::A, s3g::AcapellaOnset::None,
        82.41f, 1.0f, 600.0f });

    float previous = 0.0f;
    for (uint32_t frame = 0u; frame < 12000u; ++frame) {
        previous = synth.processFrame();
    }
    // Exercise the worst monophonic steal: pitch, vowel, consonant, and
    // velocity all change on the same sample after the previous note-off.
    synth.release();
    synth.trigger({ s3g::AcapellaVowel::I, s3g::AcapellaOnset::T,
        196.0f, 0.24f, 240.0f });
    const float first = synth.processFrame();
    const double firstStep = std::abs(static_cast<double>(first) - previous);
    double maximumTransitionStep = firstStep;
    double transitionEnergy = static_cast<double>(first) * first;
    float last = first;
    for (uint32_t frame = 1u; frame < 960u; ++frame) {
        const float sample = synth.processFrame();
        maximumTransitionStep = std::max(maximumTransitionStep,
            std::abs(static_cast<double>(sample) - last));
        transitionEnergy += static_cast<double>(sample) * sample;
        last = sample;
    }
    if (!(firstStep < 1.0e-6)
        || !(maximumTransitionStep < 0.12)
        || !(transitionEnergy > 1.0e-4)) {
        std::cerr << "acapella retrigger discontinuity: first step "
                  << firstStep << ", transition step "
                  << maximumTransitionStep << ", energy "
                  << transitionEnergy << '\n';
        return false;
    }
    return true;
}

bool hybridSourceAndFreshOnsetProbe()
{
    s3g::AcapellaSourceParams invalid;
    invalid.hybridBlend = std::numeric_limits<float>::infinity();
    invalid.onsetGuardMs = -50.0f;
    const auto sanitized = s3g::sanitizeAcapellaSourceParams(invalid);
    if (sanitized.hybridBlend != 0.16f
        || sanitized.onsetGuardMs != 2.0f) {
        std::cerr << "acapella hybrid parameter sanitation failed\n";
        return false;
    }

    auto shortGuard = s3g::acapellaSourcePreset(
        s3g::AcapellaSourcePreset::HarshScream);
    shortGuard.onsetGuardMs = 2.0f;
    auto guarded = shortGuard;
    guarded.onsetGuardMs = 18.0f;
    const auto abrupt = render(shortGuard, s3g::AcapellaVowel::A,
        s3g::AcapellaOnset::H, 110.0f);
    const auto smooth = render(guarded, s3g::AcapellaVowel::A,
        s3g::AcapellaOnset::H, 110.0f);
    // The objectionable click is the silence-to-note edge. Compare the first
    // 2 ms, before either source reaches its sustained harsh waveform.
    const double abruptStep = maximumStep(abrupt.signal, 96u);
    const double smoothStep = maximumStep(smooth.signal, 96u);
    if (smooth.signal.empty() || smooth.signal.front() != 0.0f
        || !(smoothStep < 0.010)
        || !(smoothStep < abruptStep * 0.92)) {
        std::cerr << "acapella onset guard failed: first "
                  << (smooth.signal.empty() ? 1.0f : smooth.signal.front())
                  << ", smooth step " << smoothStep << ", short step "
                  << abruptStep << '\n';
        return false;
    }

    auto tractOnly = guarded;
    tractOnly.hybridBlend = 0.0f;
    auto additiveOnly = guarded;
    additiveOnly.hybridBlend = 1.0f;
    const auto tract = render(tractOnly, s3g::AcapellaVowel::O,
        s3g::AcapellaOnset::R, 98.0f);
    const auto additive = render(additiveOnly, s3g::AcapellaVowel::O,
        s3g::AcapellaOnset::R, 98.0f);
    const double hybridDifference = difference(tract.signal, additive.signal);
    if (!(energy(tract.signal, 4800u, 24000u) > 0.05)
        || !(energy(additive.signal, 4800u, 24000u) > 0.05)
        || !(hybridDifference > 80.0)) {
        std::cerr << "acapella hybrid sources were not independent: "
                  << hybridDifference << '\n';
        return false;
    }
    return true;
}

bool articulatoryWaveguideProbe()
{
    s3g::ArticulatoryGesture invalid;
    invalid.tonguePosition = -4.0f;
    invalid.tongueConstriction = 8.0f;
    invalid.jawOpen = std::numeric_limits<float>::quiet_NaN();
    invalid.velumOpen = 3.0f;
    invalid.tractScale = std::numeric_limits<float>::infinity();
    const auto sanitized = s3g::sanitizeArticulatoryGesture(invalid);
    if (sanitized.tonguePosition != 0.0f
        || sanitized.tongueConstriction != 1.0f
        || sanitized.jawOpen != 0.50f
        || sanitized.velumOpen != 1.0f
        || sanitized.tractScale != 1.0f) {
        std::cerr << "articulatory gesture sanitation failed\n";
        return false;
    }

    s3g::ArticulatoryGesture open;
    open.tonguePosition = 0.28f;
    open.tongueConstriction = 0.16f;
    open.jawOpen = 0.94f;
    open.coarticulation = 0.82f;
    s3g::ArticulatoryGesture rounded;
    rounded.tonguePosition = 0.40f;
    rounded.tongueConstriction = 0.66f;
    rounded.jawOpen = 0.24f;
    rounded.lipRound = 0.94f;
    rounded.velumOpen = 0.72f;
    rounded.coarticulation = 0.82f;
    s3g::ArticulatoryWaveguide first;
    s3g::ArticulatoryWaveguide second;
    first.prepare(kSampleRate);
    second.prepare(kSampleRate);
    first.setGesture(open);
    second.setGesture(rounded);
    double firstEnergy = 0.0;
    double secondEnergy = 0.0;
    double nasalEnergy = 0.0;
    double gestureDifference = 0.0;
    for (uint32_t frame = 0u; frame < 16000u; ++frame) {
        const float time = static_cast<float>(frame)
            / static_cast<float>(kSampleRate);
        const float excitation = frame < 10000u
            ? 0.16f * std::sin(2.0f * s3g::kPi * 110.0f * time)
                + 0.04f * std::sin(2.0f * s3g::kPi * 220.0f * time)
            : 0.0f;
        const auto a = first.processFrame(excitation);
        const auto b = second.processFrame(excitation);
        if (!std::isfinite(a.oral) || !std::isfinite(a.nasal)
            || !std::isfinite(b.oral) || !std::isfinite(b.nasal)
            || std::abs(a.oral) > 0.461f || std::abs(a.nasal) > 0.381f
            || std::abs(b.oral) > 0.461f || std::abs(b.nasal) > 0.381f) {
            std::cerr << "articulatory waveguide safety bound failed\n";
            return false;
        }
        firstEnergy += static_cast<double>(a.oral) * a.oral;
        secondEnergy += static_cast<double>(b.oral) * b.oral;
        nasalEnergy += static_cast<double>(b.nasal) * b.nasal;
        gestureDifference += std::abs(static_cast<double>(a.oral) - b.oral);
    }
    if (!(firstEnergy > 0.01) || !(secondEnergy > 0.01)
        || !(nasalEnergy > 1.0e-5) || !(gestureDifference > 4.0)) {
        std::cerr << "articulatory waveguide gesture contract failed: "
                  << firstEnergy << ", " << secondEnergy << ", nasal "
                  << nasalEnergy << ", difference " << gestureDifference
                  << '\n';
        return false;
    }

    auto resonator = s3g::acapellaSourcePreset(
        s3g::AcapellaSourcePreset::NeutralSung);
    resonator.waveguideBlend = 0.0f;
    auto tube = resonator;
    tube.waveguideBlend = 1.0f;
    const auto legacy = render(resonator, s3g::AcapellaVowel::E,
        s3g::AcapellaOnset::Y, 130.81f);
    const auto waveguide = render(tube, s3g::AcapellaVowel::E,
        s3g::AcapellaOnset::Y, 130.81f);
    if (!(energy(waveguide.signal, 4800u, 24000u) > 0.05)
        || !(difference(legacy.signal, waveguide.signal) > 50.0)) {
        std::cerr << "waveguide blend did not provide a distinct tract\n";
        return false;
    }
    return true;
}

bool phonemeGestureSequencerProbe()
{
    s3g::AcapellaSourceParams invalid;
    invalid.gestureSequence = static_cast<s3g::AcapellaGestureSequence>(255u);
    invalid.gestureSync = static_cast<s3g::AcapellaGestureSync>(255u);
    invalid.gestureDivision = static_cast<s3g::AcapellaGestureDivision>(255u);
    invalid.gestureRateHz = std::numeric_limits<float>::quiet_NaN();
    invalid.gestureDepth = -4.0f;
    const auto sanitized = s3g::sanitizeAcapellaSourceParams(invalid);
    if (sanitized.gestureSequence != s3g::AcapellaGestureSequence::Off
        || sanitized.gestureSync != s3g::AcapellaGestureSync::Free
        || sanitized.gestureDivision
            != s3g::AcapellaGestureDivision::Eighth
        || sanitized.gestureRateHz != 5.0f
        || sanitized.gestureDepth != 0.0f) {
        std::cerr << "phoneme sequencer parameter sanitation failed\n";
        return false;
    }

    auto offParams = s3g::acapellaSourcePreset(
        s3g::AcapellaSourcePreset::DeathGrowl);
    offParams.gestureSequence = s3g::AcapellaGestureSequence::Off;
    offParams.randomSeed = 0x73657131u;
    auto loopParams = offParams;
    loopParams.gestureSequence = s3g::AcapellaGestureSequence::DeathChant;
    loopParams.gestureRateHz = 8.0f;
    loopParams.gestureDepth = 1.0f;
    loopParams.gestureLoop = true;
    auto oneShotParams = loopParams;
    oneShotParams.gestureLoop = false;
    auto zeroDepthParams = loopParams;
    zeroDepthParams.gestureDepth = 0.0f;

    const auto off = render(offParams, s3g::AcapellaVowel::O,
        s3g::AcapellaOnset::None, 98.0f, 60000u, 72000u);
    const auto loop = render(loopParams, s3g::AcapellaVowel::O,
        s3g::AcapellaOnset::None, 98.0f, 60000u, 72000u);
    const auto oneShot = render(oneShotParams, s3g::AcapellaVowel::O,
        s3g::AcapellaOnset::None, 98.0f, 60000u, 72000u);
    const auto zeroDepth = render(zeroDepthParams, s3g::AcapellaVowel::O,
        s3g::AcapellaOnset::None, 98.0f, 60000u, 72000u);
    const double sequenceDifference = difference(off.signal, loop.signal);
    const double zeroDepthDifference = difference(off.signal,
        zeroDepth.signal);
    double loopDifference = 0.0;
    for (uint32_t frame = 38000u; frame < 60000u; ++frame) {
        loopDifference += std::abs(static_cast<double>(loop.signal[frame])
            - oneShot.signal[frame]);
    }
    const double loopLateEnergy = energy(loop.signal, 52000u, 60000u);
    const double oneShotLateEnergy = energy(oneShot.signal, 52000u, 60000u);
    const double transitionStep = maximumStep(loop.signal, 60000u);
    if (!(sequenceDifference > 100.0)
        || !(loopDifference > 30.0)
        || !(zeroDepthDifference < 1.0)
        || !(oneShotLateEnergy < loopLateEnergy * 0.25)
        || !(transitionStep < 0.14)
        || !loop.finished || !oneShot.finished) {
        std::cerr << "phoneme gesture sequencer contract failed: sequence "
                  << sequenceDifference << ", loop " << loopDifference
                  << ", late energy " << oneShotLateEnergy << '/'
                  << loopLateEnergy << ", zero depth " << zeroDepthDifference
                  << ", step "
                  << transitionStep << ", finished " << loop.finished
                  << '/' << oneShot.finished << '\n';
        return false;
    }
    return true;
}

bool textCompilerAndTempoSyncProbe()
{
    static_assert(sizeof(s3g::acapella_text_detail::lexicon)
            / sizeof(s3g::acapella_text_detail::lexicon[0]) >= 300u,
        "natural-text lexicon unexpectedly shrank");
    const auto pronunciationMatches = [](const char* word,
                                          const auto& expected) {
        const auto result = s3g::compileAcapellaText(word);
        if (result.program.wordCount != 1u
            || result.program.count != expected.size()) return false;
        for (uint32_t index = 0u; index < result.program.count; ++index) {
            if (result.program.steps[index].phoneme != expected[index]) {
                return false;
            }
        }
        return true;
    };
    const auto phraseWordMatches = [](const char* phrase,
                                      uint32_t targetWord,
                                      const auto& expected) {
        const auto result = s3g::compileAcapellaText(phrase);
        uint32_t wordIndex = 0u;
        uint32_t phoneIndex = 0u;
        bool targetStarted = false;
        for (uint32_t index = 0u; index < result.program.count; ++index) {
            const auto& step = result.program.steps[index];
            if ((step.flags & s3g::kAcapellaWordStart) != 0u) {
                targetStarted = wordIndex == targetWord;
                if (targetStarted
                    && (step.flags
                        & s3g::kAcapellaContextualPronunciation) == 0u) {
                    return false;
                }
                ++wordIndex;
            }
            if (targetStarted) {
                if (phoneIndex >= expected.size()
                    || step.phoneme != expected[phoneIndex++]) return false;
                if ((step.flags & s3g::kAcapellaWordEnd) != 0u) {
                    return phoneIndex == expected.size()
                        && result.contextualWordCount > 0u;
                }
            }
        }
        return false;
    };
    constexpr uint32_t lexiconSize = static_cast<uint32_t>(
        sizeof(s3g::acapella_text_detail::lexicon)
        / sizeof(s3g::acapella_text_detail::lexicon[0]));
    for (uint32_t index = 0u; index < lexiconSize; ++index) {
        const auto& entry = s3g::acapella_text_detail::lexicon[index];
        s3g::AcapellaTextCompileResult pronunciation;
        if (!s3g::acapella_text_detail::compilePronunciation(
                pronunciation, entry.pronunciation)
            || pronunciation.program.count == 0u
            || pronunciation.syllableCount == 0u) {
            std::cerr << "invalid lexicon pronunciation: " << entry.word
                      << '\n';
            return false;
        }
        for (uint32_t other = index + 1u; other < lexiconSize; ++other) {
            if (std::strcmp(entry.word,
                    s3g::acapella_text_detail::lexicon[other].word) == 0) {
                std::cerr << "duplicate lexicon word: " << entry.word << '\n';
                return false;
            }
        }
    }
    const auto compiled = s3g::compileAcapellaText(
        "Break the silence, scream it down!");
    bool hasPause = false;
    bool hasFricative = false;
    for (uint32_t index = 0u; index < compiled.program.count; ++index) {
        const auto& step = compiled.program.steps[index];
        hasPause |= step.amplitude <= 1.0e-5f;
        hasFricative |= step.phoneme == s3g::AcapellaPhoneme::S
            || step.phoneme == s3g::AcapellaPhoneme::SH
            || step.phoneme == s3g::AcapellaPhoneme::TH;
    }
    if (compiled.program.wordCount != 6u
        || compiled.syllableCount < 6u
        || compiled.program.count < 12u
        || !hasPause || !hasFricative || compiled.program.truncated) {
        std::cerr << "natural text compiler contract failed: words "
                  << compiled.program.wordCount << ", syllables "
                  << compiled.syllableCount << ", gestures "
                  << compiled.program.count << '\n';
        return false;
    }

    const auto clusters = s3g::compileAcapellaText(
        "scream strength voice");
    constexpr std::array<s3g::AcapellaPhoneme, 5u> scream {{
        s3g::AcapellaPhoneme::S,
        s3g::AcapellaPhoneme::K,
        s3g::AcapellaPhoneme::R,
        s3g::AcapellaPhoneme::IY,
        s3g::AcapellaPhoneme::M,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 6u> strength {{
        s3g::AcapellaPhoneme::S,
        s3g::AcapellaPhoneme::T,
        s3g::AcapellaPhoneme::R,
        s3g::AcapellaPhoneme::EH,
        s3g::AcapellaPhoneme::NG,
        s3g::AcapellaPhoneme::TH,
    }};
    bool clusterOrder = clusters.program.count >= 17u;
    for (uint32_t index = 0u; clusterOrder && index < scream.size(); ++index) {
        clusterOrder = clusters.program.steps[index].phoneme == scream[index];
    }
    const uint32_t strengthBegin = 6u; // five phones + inter-word boundary
    for (uint32_t index = 0u; clusterOrder && index < strength.size(); ++index) {
        clusterOrder = clusters.program.steps[strengthBegin + index].phoneme
            == strength[index];
    }
    const auto irregular = s3g::compileAcapellaText("the voice");
    const auto worlds = s3g::compileAcapellaText("worlds");
    constexpr std::array<s3g::AcapellaPhoneme, 5u> worldsPhones {{
        s3g::AcapellaPhoneme::W,
        s3g::AcapellaPhoneme::ER,
        s3g::AcapellaPhoneme::L,
        s3g::AcapellaPhoneme::D,
        s3g::AcapellaPhoneme::Z,
    }};
    bool worldsPronunciation = worlds.program.count == worldsPhones.size();
    for (uint32_t index = 0u;
         worldsPronunciation && index < worldsPhones.size(); ++index) {
        worldsPronunciation = worlds.program.steps[index].phoneme
            == worldsPhones[index];
    }
    constexpr std::array<s3g::AcapellaPhoneme, 4u> walkedPhones {{
        s3g::AcapellaPhoneme::W, s3g::AcapellaPhoneme::AO,
        s3g::AcapellaPhoneme::K, s3g::AcapellaPhoneme::T,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 4u> lovedPhones {{
        s3g::AcapellaPhoneme::L, s3g::AcapellaPhoneme::AH,
        s3g::AcapellaPhoneme::V, s3g::AcapellaPhoneme::D,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 6u> wantedPhones {{
        s3g::AcapellaPhoneme::W, s3g::AcapellaPhoneme::AA,
        s3g::AcapellaPhoneme::N, s3g::AcapellaPhoneme::T,
        s3g::AcapellaPhoneme::IH, s3g::AcapellaPhoneme::D,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 4u> singsPhones {{
        s3g::AcapellaPhoneme::S, s3g::AcapellaPhoneme::IH,
        s3g::AcapellaPhoneme::NG, s3g::AcapellaPhoneme::Z,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 6u> boxesPhones {{
        s3g::AcapellaPhoneme::B, s3g::AcapellaPhoneme::AA,
        s3g::AcapellaPhoneme::K, s3g::AcapellaPhoneme::S,
        s3g::AcapellaPhoneme::IH, s3g::AcapellaPhoneme::Z,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 5u> singingPhones {{
        s3g::AcapellaPhoneme::S, s3g::AcapellaPhoneme::IH,
        s3g::AcapellaPhoneme::NG, s3g::AcapellaPhoneme::IH,
        s3g::AcapellaPhoneme::NG,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 7u> mustntPhones {{
        s3g::AcapellaPhoneme::M, s3g::AcapellaPhoneme::AH,
        s3g::AcapellaPhoneme::S, s3g::AcapellaPhoneme::T,
        s3g::AcapellaPhoneme::AX, s3g::AcapellaPhoneme::N,
        s3g::AcapellaPhoneme::T,
    }};
    const bool derivedPronunciations =
        pronunciationMatches("walked", walkedPhones)
        && pronunciationMatches("loved", lovedPhones)
        && pronunciationMatches("wanted", wantedPhones)
        && pronunciationMatches("sings", singsPhones)
        && pronunciationMatches("boxes", boxesPhones)
        && pronunciationMatches("singing", singingPhones)
        && pronunciationMatches("mustn't", mustntPhones);
    constexpr std::array<s3g::AcapellaPhoneme, 3u> liveVerb {{
        s3g::AcapellaPhoneme::L, s3g::AcapellaPhoneme::IH,
        s3g::AcapellaPhoneme::V,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 4u> liveEvent {{
        s3g::AcapellaPhoneme::L, s3g::AcapellaPhoneme::AA,
        s3g::AcapellaPhoneme::IY, s3g::AcapellaPhoneme::V,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 3u> readPresent {{
        s3g::AcapellaPhoneme::R, s3g::AcapellaPhoneme::IY,
        s3g::AcapellaPhoneme::D,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 3u> readPast {{
        s3g::AcapellaPhoneme::R, s3g::AcapellaPhoneme::EH,
        s3g::AcapellaPhoneme::D,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 4u> windNoun {{
        s3g::AcapellaPhoneme::W, s3g::AcapellaPhoneme::IH,
        s3g::AcapellaPhoneme::N, s3g::AcapellaPhoneme::D,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 5u> windVerb {{
        s3g::AcapellaPhoneme::W, s3g::AcapellaPhoneme::AA,
        s3g::AcapellaPhoneme::IY, s3g::AcapellaPhoneme::N,
        s3g::AcapellaPhoneme::D,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 3u> tearDrop {{
        s3g::AcapellaPhoneme::T, s3g::AcapellaPhoneme::IH,
        s3g::AcapellaPhoneme::R,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 3u> tearRip {{
        s3g::AcapellaPhoneme::T, s3g::AcapellaPhoneme::EH,
        s3g::AcapellaPhoneme::R,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 3u> leadVerb {{
        s3g::AcapellaPhoneme::L, s3g::AcapellaPhoneme::IY,
        s3g::AcapellaPhoneme::D,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 3u> leadMetal {{
        s3g::AcapellaPhoneme::L, s3g::AcapellaPhoneme::EH,
        s3g::AcapellaPhoneme::D,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 4u> bassMusic {{
        s3g::AcapellaPhoneme::B, s3g::AcapellaPhoneme::EH,
        s3g::AcapellaPhoneme::IY, s3g::AcapellaPhoneme::S,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 3u> bassFish {{
        s3g::AcapellaPhoneme::B, s3g::AcapellaPhoneme::AE,
        s3g::AcapellaPhoneme::S,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 5u> closeVerb {{
        s3g::AcapellaPhoneme::K, s3g::AcapellaPhoneme::L,
        s3g::AcapellaPhoneme::AO, s3g::AcapellaPhoneme::UW,
        s3g::AcapellaPhoneme::Z,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 5u> closeAdjective {{
        s3g::AcapellaPhoneme::K, s3g::AcapellaPhoneme::L,
        s3g::AcapellaPhoneme::AO, s3g::AcapellaPhoneme::UW,
        s3g::AcapellaPhoneme::S,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 3u> useVerb {{
        s3g::AcapellaPhoneme::Y, s3g::AcapellaPhoneme::UW,
        s3g::AcapellaPhoneme::Z,
    }};
    constexpr std::array<s3g::AcapellaPhoneme, 3u> useNoun {{
        s3g::AcapellaPhoneme::Y, s3g::AcapellaPhoneme::UW,
        s3g::AcapellaPhoneme::S,
    }};
    const bool contextualHomographs =
        phraseWordMatches("we live here", 1u, liveVerb)
        && phraseWordMatches("a live show", 1u, liveEvent)
        && phraseWordMatches("we will read this", 2u, readPresent)
        && phraseWordMatches("we have read this", 2u, readPast)
        && phraseWordMatches("the wind is strong", 1u, windNoun)
        && phraseWordMatches("wind the tape", 0u, windVerb)
        && phraseWordMatches("a tear fell", 1u, tearDrop)
        && phraseWordMatches("tear it down", 0u, tearRip)
        && phraseWordMatches("lead the way", 0u, leadVerb)
        && phraseWordMatches("made of lead", 2u, leadMetal)
        && phraseWordMatches("bass guitar", 0u, bassMusic)
        && phraseWordMatches("striped bass", 1u, bassFish)
        && phraseWordMatches("close the door", 0u, closeVerb)
        && phraseWordMatches("stay close", 1u, closeAdjective)
        && phraseWordMatches("use the voice", 0u, useVerb)
        && phraseWordMatches("no use", 1u, useNoun);
    const bool consonantLevelHierarchy =
        clusters.program.steps[0].amplitude <= 0.70f
        && clusters.program.steps[3].amplitude
            > clusters.program.steps[0].amplitude + 0.30f
        && clusters.program.steps[4].amplitude
            > clusters.program.steps[0].amplitude;
    const bool pronunciationLexicon = irregular.program.count >= 7u
        && irregular.program.steps[0].phoneme == s3g::AcapellaPhoneme::DH
        && irregular.program.steps[1].phoneme == s3g::AcapellaPhoneme::AX
        && irregular.program.steps[3].phoneme == s3g::AcapellaPhoneme::V
        && irregular.program.steps[4].phoneme == s3g::AcapellaPhoneme::AO
        && irregular.program.steps[5].phoneme == s3g::AcapellaPhoneme::IY
        && irregular.program.steps[6].phoneme == s3g::AcapellaPhoneme::S;
    if (clusters.program.wordCount != 3u || !clusterOrder
        || !consonantLevelHierarchy
        || !pronunciationLexicon
        || !worldsPronunciation
        || !derivedPronunciations
        || !contextualHomographs
        || clusters.program.steps[3].stress != 1u
        || (clusters.program.steps[0].flags
            & s3g::kAcapellaWordStart) == 0u
        || (clusters.program.steps[4].flags
            & s3g::kAcapellaWordEnd) == 0u) {
        std::cerr << "phoneme G2P lost a cluster, stress, connected-level "
                     "hierarchy, boundary, lexicon, inflection, or context\n";
        return false;
    }

    const auto scoredRest = s3g::compileAcapellaText(
        "hello || worlds");
    uint32_t forcedRestCount = 0u;
    uint32_t silenceEventCount = 0u;
    float forcedRestDuration = 0.0f;
    for (uint32_t index = 0u; index < scoredRest.program.count; ++index) {
        const auto& step = scoredRest.program.steps[index];
        if (step.phoneme == s3g::AcapellaPhoneme::Silence) {
            ++silenceEventCount;
        }
        if ((step.flags & s3g::kAcapellaForcedRest) != 0u) {
            ++forcedRestCount;
            forcedRestDuration = step.durationScale;
        }
    }
    if (scoredRest.program.wordCount != 2u
        || forcedRestCount != 1u
        || silenceEventCount != 1u
        || std::fabs(forcedRestDuration - 2.0f) > 1.0e-6f) {
        std::cerr << "forced score rest did not preserve its rhythmic "
                     "division multiplier\n";
        return false;
    }

    s3g::AcapellaGestureProgram clockProgram;
    clockProgram.count = 3u;
    clockProgram.revision = 0x74656d70u;
    clockProgram.steps[0] = { s3g::AcapellaPhoneme::T, 1.0f, 1.0f };
    clockProgram.steps[1] = { s3g::AcapellaPhoneme::IY, 1.0f, 1.0f };
    clockProgram.steps[2] = { s3g::AcapellaPhoneme::SH, 1.0f, 1.0f };

    const auto framesToNextStep = [&](double tempo) {
        auto params = s3g::acapellaSourcePreset(
            s3g::AcapellaSourcePreset::NeutralSung);
        params.gestureSequence = s3g::AcapellaGestureSequence::Text;
        params.gestureSync = s3g::AcapellaGestureSync::Note;
        params.gestureDivision = s3g::AcapellaGestureDivision::Quarter;
        params.gestureLoop = true;
        params.vibratoDepthCents = 0.0f;
        s3g::AcapellaSourceSynth synth;
        synth.setParams(params);
        synth.setTextGestureProgram(clockProgram);
        synth.prepare(kSampleRate);
        synth.setGestureTransport(tempo, 0.0, true, false, true);
        synth.trigger({ s3g::AcapellaVowel::Schwa,
            s3g::AcapellaOnset::None, 110.0f, 0.8f, 1000.0f });
        uint32_t frames = 0u;
        while (synth.gestureStepIndex() == 0u && frames < 100000u) {
            (void)synth.processFrame();
            ++frames;
        }
        return frames;
    };
    const uint32_t at120 = framesToNextStep(120.0);
    const uint32_t at60 = framesToNextStep(60.0);
    if (at120 < 23900u || at120 > 24100u
        || at60 < 47900u || at60 > 48100u
        || std::abs(static_cast<int64_t>(at60)
            - static_cast<int64_t>(at120) * 2) > 3) {
        std::cerr << "tempo-synced gesture duration failed: "
                  << at120 << " / " << at60 << '\n';
        return false;
    }

    auto transportParams = s3g::acapellaSourcePreset(
        s3g::AcapellaSourcePreset::NeutralSung);
    transportParams.gestureSequence = s3g::AcapellaGestureSequence::Text;
    transportParams.gestureSync = s3g::AcapellaGestureSync::Transport;
    transportParams.gestureDivision = s3g::AcapellaGestureDivision::Quarter;
    transportParams.gestureLoop = true;
    s3g::AcapellaSourceSynth transport;
    transport.setParams(transportParams);
    transport.setTextGestureProgram(clockProgram);
    transport.prepare(kSampleRate);
    transport.setGestureTransport(120.0, 1.25, true, true, true);
    transport.trigger({ s3g::AcapellaVowel::A,
        s3g::AcapellaOnset::H, 98.0f, 0.9f, 1000.0f });
    std::vector<float> signal(72000u, 0.0f);
    for (uint32_t frame = 0u; frame < signal.size(); ++frame) {
        signal[frame] = transport.processFrame();
    }
    if (transport.gestureStepIndex() != 1u
        || !(energy(signal) > 0.1)
        || !(maximumStep(signal, static_cast<uint32_t>(signal.size()))
            < 0.14)) {
        std::cerr << "transport-phase text sequence failed: step "
                  << transport.gestureStepIndex() << ", energy "
                  << energy(signal) << ", max step "
                  << maximumStep(signal,
                         static_cast<uint32_t>(signal.size())) << '\n';
        return false;
    }
    return true;
}

bool extremeVoiceProbe()
{
    const auto neutralParams = s3g::acapellaSourcePreset(
        s3g::AcapellaSourcePreset::NeutralSung);
    const auto screamParams = s3g::acapellaSourcePreset(
        s3g::AcapellaSourcePreset::HarshScream);
    const auto growlParams = s3g::acapellaSourcePreset(
        s3g::AcapellaSourcePreset::DeathGrowl);
    if (!(screamParams.voice.harshness > 0.75f)
        || !(screamParams.voice.throat > 0.70f)
        || !(growlParams.voice.falseFold > 0.80f)
        || !(growlParams.voice.chest > 0.75f)
        || !(growlParams.voice.tractScale > 1.15f)) {
        std::cerr << "extreme vocal profiles were not configured\n";
        return false;
    }

    const auto neutral = render(neutralParams, s3g::AcapellaVowel::A,
        s3g::AcapellaOnset::H, 110.0f);
    const auto scream = render(screamParams, s3g::AcapellaVowel::A,
        s3g::AcapellaOnset::H, 110.0f);
    const auto growl = render(growlParams, s3g::AcapellaVowel::O,
        s3g::AcapellaOnset::R, 73.42f);
    const double screamDifference = difference(neutral.signal, scream.signal);
    const double growlEnergy = energy(growl.signal, 4800u, 24000u);
    if (!(screamDifference > 120.0) || !(growlEnergy > 0.1)
        || !scream.finished || !growl.finished) {
        std::cerr << "extreme vocal synthesis contract failed: difference "
                  << screamDifference << ", growl energy " << growlEnergy
                  << ", finished " << scream.finished << " / "
                  << growl.finished << '\n';
        return false;
    }
    return true;
}

bool vocalEffectsProbe()
{
    s3g::AcapellaVocalFxParams invalid;
    invalid.octaveDown = -4.0f;
    invalid.octaveUp = 8.0f;
    invalid.fuzzDriveDb = 90.0f;
    invalid.fuzzToneHz = std::numeric_limits<float>::infinity();
    invalid.compression = std::numeric_limits<float>::quiet_NaN();
    invalid.echoHeads = static_cast<s3g::DrumEchoHeadMode>(999u);
    invalid.echoClock = static_cast<s3g::DrumEchoClock>(999u);
    invalid.echoTimeMs = -20.0f;
    invalid.echoFeedback = 4.0f;
    invalid.echoTone = -8.0f;
    invalid.pvoc.amount = 3.0f;
    invalid.pvoc.mode = static_cast<s3g::AcapellaPvocMode>(99u);
    invalid.pvoc.memoryMs = -20.0f;
    invalid.pvoc.speed = 9.0f;
    invalid.pvoc.heads = 99u;
    invalid.pvoc.pitchSemitones = -90.0f;
    invalid.pvoc.peakResidue = -8.0f;
    invalid.pvoc.phaseMode = static_cast<s3g::AcapellaPvocPhaseMode>(99u);
    invalid.pvoc.captureTrigger = static_cast<
        s3g::AcapellaPvocCaptureTrigger>(99u);
    invalid.pvoc.captureReleaseMs = 9000.0f;
    const auto sanitized = s3g::sanitizeAcapellaVocalFxParams(invalid);
    if (sanitized.octaveDown != 0.0f || sanitized.octaveUp != 1.0f
        || sanitized.fuzzDriveDb != 30.0f
        || sanitized.fuzzToneHz != 6500.0f
        || sanitized.compression != 0.22f
        || sanitized.echoHeads != s3g::DrumEchoHeadMode::Heads123
        || sanitized.echoClock != s3g::DrumEchoClock::Bar
        || sanitized.echoTimeMs != 20.0f
        || sanitized.echoFeedback != 0.92f
        || sanitized.echoTone != -1.0f
        || sanitized.pvoc.amount != 1.0f
        || sanitized.pvoc.mode != s3g::AcapellaPvocMode::Cloud
        || sanitized.pvoc.memoryMs != 20.0f
        || sanitized.pvoc.speed != 2.0f
        || sanitized.pvoc.heads != 8u
        || sanitized.pvoc.pitchSemitones != -24.0f
        || sanitized.pvoc.peakResidue != -1.0f
        || sanitized.pvoc.phaseMode != s3g::AcapellaPvocPhaseMode::Diffuse
        || sanitized.pvoc.captureTrigger
            != s3g::AcapellaPvocCaptureTrigger::Rest
        || sanitized.pvoc.captureReleaseMs != 5000.0f) {
        std::cerr << "acapella vocal effects sanitation failed\n";
        return false;
    }

    s3g::AcapellaVocalEffects silent;
    silent.prepare(kSampleRate);
    for (uint32_t frame = 0u; frame < 256u; ++frame) {
        const auto output = silent.processFrame(0.0f);
        if (output.left != 0.0f || output.right != 0.0f) {
            std::cerr << "acapella vocal effects generated inputless audio\n";
            return false;
        }
    }

    s3g::AcapellaVocalFxParams echoParams;
    echoParams.compression = 0.0f;
    echoParams.deEss = 0.0f;
    echoParams.echoHeads = s3g::DrumEchoHeadMode::Head1;
    echoParams.echoClock = s3g::DrumEchoClock::Free;
    echoParams.echoTimeMs = 100.0f;
    echoParams.echoFeedback = 0.0f;
    echoParams.echoWear = 0.0f;
    echoParams.echoFlutter = 0.0f;
    echoParams.echoTone = 1.0f;
    echoParams.echoSpread = 0.0f;
    echoParams.echoMix = 1.0f;
    s3g::AcapellaVocalEffects echo;
    echo.setParams(echoParams);
    echo.prepare(8000.0);
    const uint32_t effectsLatency = echo.latencySamples();
    float freePeak = 0.0f;
    for (uint32_t frame = 0u; frame < effectsLatency + 840u; ++frame) {
        const auto output = echo.processFrame(frame == 0u ? 0.5f : 0.0f);
        if (frame >= effectsLatency + 796u
            && frame <= effectsLatency + 808u) {
            freePeak = std::max(freePeak, std::abs(output.left));
        }
    }
    echo.setParams(echoParams);
    echo.prepare(8000.0);
    echoParams.echoClock = s3g::DrumEchoClock::Quarter;
    echo.setParams(echoParams);
    echo.setTempo(120.0, true);
    echo.reset();
    float syncPeak = 0.0f;
    for (uint32_t frame = 0u; frame < effectsLatency + 4040u; ++frame) {
        const auto output = echo.processFrame(frame == 0u ? 0.5f : 0.0f);
        if (frame >= effectsLatency + 3996u
            && frame <= effectsLatency + 4008u) {
            syncPeak = std::max(syncPeak, std::abs(output.left));
        }
    }
    if (!(freePeak > 0.05f) || !(syncPeak > 0.05f)
        || echo.tailSamples() == 0u) {
        std::cerr << "integrated multi-head tape timing failed: "
                  << freePeak << " / " << syncPeak << '\n';
        return false;
    }

    s3g::AcapellaVocalFxParams dryParams;
    dryParams.compression = 0.0f;
    dryParams.deEss = 0.0f;
    s3g::AcapellaVocalEffects dry;
    dry.setParams(dryParams);
    dry.prepare(kSampleRate);
    auto extremeParams = s3g::acapellaVocalFxPreset(
        s3g::AcapellaSourcePreset::DeathGrowl);
    s3g::AcapellaVocalEffects extreme;
    extreme.setParams(extremeParams);
    extreme.prepare(kSampleRate);

    double effectDifference = 0.0;
    double stereoDifference = 0.0;
    double maximumStep = 0.0;
    float previous = 0.0f;
    for (uint32_t frame = 0u; frame < 36000u; ++frame) {
        const float time = static_cast<float>(frame)
            / static_cast<float>(kSampleRate);
        const float amplitude = frame < 18000u ? 0.24f : 0.0f;
        const float input = amplitude
            * (std::sin(2.0f * s3g::kPi * 110.0f * time)
                + 0.22f * std::sin(2.0f * s3g::kPi * 220.0f * time));
        const auto clean = dry.processFrame(input);
        const auto processed = extreme.processFrame(input);
        if (!std::isfinite(processed.left)
            || !std::isfinite(processed.right)
            || std::abs(processed.left) > 0.981f
            || std::abs(processed.right) > 0.981f) {
            std::cerr << "acapella vocal effects safety bound failed\n";
            return false;
        }
        effectDifference += std::abs(static_cast<double>(processed.left)
            - clean.left);
        stereoDifference += std::abs(static_cast<double>(processed.left)
            - processed.right);
        maximumStep = std::max(maximumStep,
            std::abs(static_cast<double>(processed.left) - previous));
        previous = processed.left;
    }
    if (!(effectDifference > 250.0) || !(stereoDifference > 10.0)
        || !(maximumStep < 0.22) || !extreme.active()) {
        std::cerr << "acapella vocal effects contract failed: difference "
                  << effectDifference << ", stereo " << stereoDifference
                  << ", step " << maximumStep << ", active "
                  << extreme.active() << '\n';
        return false;
    }
    return true;
}

bool pvocFieldProbe()
{
    s3g::AcapellaPvocParams invalid;
    invalid.amount = -2.0f;
    invalid.mode = static_cast<s3g::AcapellaPvocMode>(99u);
    invalid.memoryMs = -1.0f;
    invalid.position = 4.0f;
    invalid.speed = -9.0f;
    invalid.loopLengthMs = 9000.0f;
    invalid.timeSpread = std::numeric_limits<float>::infinity();
    invalid.heads = 99u;
    invalid.feedback = 2.0f;
    invalid.pitchSemitones = 90.0f;
    invalid.formantSemitones = -90.0f;
    invalid.warp = 3.0f;
    invalid.harmonicLock = -2.0f;
    invalid.peakResidue = -4.0f;
    invalid.partialCloud = 3.0f;
    invalid.phaseMode = static_cast<s3g::AcapellaPvocPhaseMode>(99u);
    invalid.coherence = -1.0f;
    invalid.phaseDrift = 2.0f;
    invalid.transientPreserve = 3.0f;
    invalid.captureTrigger = static_cast<
        s3g::AcapellaPvocCaptureTrigger>(99u);
    invalid.captureReleaseMs = 9000.0f;
    invalid.gestureFollow = std::numeric_limits<float>::quiet_NaN();
    const auto sanitized = s3g::sanitizeAcapellaPvocParams(invalid);
    if (sanitized.amount != 0.0f
        || sanitized.mode != s3g::AcapellaPvocMode::Cloud
        || sanitized.memoryMs != 20.0f || sanitized.position != 1.0f
        || sanitized.speed != -2.0f || sanitized.loopLengthMs != 5000.0f
        || sanitized.timeSpread != 0.0f
        || sanitized.heads != s3g::kAcapellaPvocMaxHeads
        || sanitized.feedback != 0.94f
        || sanitized.pitchSemitones != 24.0f
        || sanitized.formantSemitones != -24.0f
        || sanitized.warp != 1.0f || sanitized.harmonicLock != 0.0f
        || sanitized.peakResidue != -1.0f
        || sanitized.partialCloud != 1.0f
        || sanitized.phaseMode != s3g::AcapellaPvocPhaseMode::Diffuse
        || sanitized.coherence != 0.0f || sanitized.phaseDrift != 1.0f
        || sanitized.transientPreserve != 1.0f
        || sanitized.captureTrigger
            != s3g::AcapellaPvocCaptureTrigger::Rest
        || sanitized.captureReleaseMs != 5000.0f
        || sanitized.gestureFollow != 0.55f) {
        std::cerr << "PVOC sanitation failed\n";
        return false;
    }

    constexpr uint32_t totalFrames = 28000u;
    constexpr uint32_t inputFrames = 14500u;
    std::vector<float> input(totalFrames, 0.0f);
    for (uint32_t frame = 0u; frame < inputFrames; ++frame) {
        const float time = static_cast<float>(frame)
            / static_cast<float>(kSampleRate);
        const float sweep = 112.0f + 28.0f
            * std::sin(2.0f * s3g::kPi * 0.83f * time);
        const float envelope = std::min(1.0f,
            static_cast<float>(frame) / 700.0f);
        input[frame] = envelope * (0.17f * std::sin(
            2.0f * s3g::kPi * sweep * time)
            + 0.075f * std::sin(2.0f * s3g::kPi * sweep * 3.02f * time)
            + 0.044f * std::sin(2.0f * s3g::kPi * sweep * 7.11f * time)
            + 0.018f * std::sin(2.0f * s3g::kPi * 2317.0f * time));
    }

    const auto renderPvoc = [&](s3g::AcapellaPvocParams params,
                                bool compareDry = false) {
        s3g::AcapellaPvocField field;
        field.setParams(params);
        if (!field.prepare(kSampleRate)) return std::vector<float> {};
        s3g::AcapellaPvocGesture gesture;
        gesture.phoneme = s3g::AcapellaPhoneme::AA;
        gesture.frequencyHz = 112.0f;
        gesture.stepIndex = 1u;
        gesture.stress = 1u;
        gesture.flags = s3g::kAcapellaWordStart
            | s3g::kAcapellaSyllableStart;
        gesture.active = true;
        gesture.voiceInstance = 1u;
        field.setGesture(gesture);
        std::vector<float> output(totalFrames, 0.0f);
        for (uint32_t frame = 0u; frame < totalFrames; ++frame) {
            if (frame == 5200u) {
                gesture.stepIndex = 2u;
                gesture.flags = s3g::kAcapellaSyllableStart;
                field.setGesture(gesture);
            }
            const auto value = field.processFrame(input[frame]);
            output[frame] = value.output;
            if (!std::isfinite(value.output)
                || std::abs(value.output) > 1.501f) {
                output.clear();
                return output;
            }
            if (compareDry && frame >= field.latencySamples()
                && std::abs(value.output - value.dry) > 2.0e-5f) {
                std::cerr << "PVOC dry mismatch at " << frame << ": "
                          << value.output << " / " << value.dry << " ("
                          << std::abs(value.output - value.dry) << ")\n";
                output.clear();
                return output;
            }
        }
        return output;
    };

    s3g::AcapellaPvocParams live;
    live.amount = 1.0f;
    live.mode = s3g::AcapellaPvocMode::Live;
    live.gestureFollow = 0.0f;
    const auto transparent = renderPvoc(live, true);
    if (transparent.empty()) {
        std::cerr << "neutral PVOC Live mode was not transparent\n";
        return false;
    }
    s3g::AcapellaPvocParams audibleDefault;
    audibleDefault.amount = 1.0f;
    audibleDefault.gestureFollow = 0.0f;
    const auto defaultWet = renderPvoc(audibleDefault);
    if (defaultWet.empty()
        || !(difference(defaultWet, transparent) > 2.0)) {
        std::cerr << "default PVOC Amount did not reveal an effect\n";
        return false;
    }

    s3g::AcapellaPvocParams base = live;
    base.memoryMs = 720.0f;
    base.position = 0.38f;
    base.speed = 0.57f;
    base.loopLengthMs = 210.0f;
    base.timeSpread = 0.62f;
    base.heads = 2u;
    base.transientPreserve = 0.0f;
    base.captureTrigger = s3g::AcapellaPvocCaptureTrigger::Phoneme;
    base.captureReleaseMs = 600.0f;
    constexpr std::array<s3g::AcapellaPvocMode, 6u> modes {{
        s3g::AcapellaPvocMode::Freeze,
        s3g::AcapellaPvocMode::Stretch,
        s3g::AcapellaPvocMode::Scrub,
        s3g::AcapellaPvocMode::Reverse,
        s3g::AcapellaPvocMode::Loop,
        s3g::AcapellaPvocMode::Cloud,
    }};
    std::array<std::vector<float>, modes.size()> modeOutputs;
    for (uint32_t index = 0u; index < modes.size(); ++index) {
        auto params = base;
        params.mode = modes[index];
        if (params.mode == s3g::AcapellaPvocMode::Cloud) {
            params.heads = 5u;
            params.partialCloud = 0.55f;
            params.phaseDrift = 0.32f;
        }
        modeOutputs[index] = renderPvoc(params);
        if (modeOutputs[index].empty()
            || !(difference(modeOutputs[index], transparent) > 1.0)
            || !(maximumStep(modeOutputs[index], totalFrames) < 1.1)) {
            std::cerr << "PVOC transport contract failed for mode "
                      << index + 1u << '\n';
            return false;
        }
    }
    if (!(energy(modeOutputs[0], inputFrames + 2048u) > 1.0e-4)
        || !(difference(modeOutputs[1], modeOutputs[2]) > 1.0)
        || !(difference(modeOutputs[3], modeOutputs[4]) > 1.0)) {
        std::cerr << "PVOC history transports collapsed onto one behavior\n";
        return false;
    }

    auto pitchParams = base;
    pitchParams.mode = s3g::AcapellaPvocMode::Loop;
    pitchParams.pitchSemitones = 7.0f;
    auto formantParams = pitchParams;
    formantParams.pitchSemitones = 0.0f;
    formantParams.formantSemitones = 7.0f;
    auto peakParams = base;
    peakParams.mode = s3g::AcapellaPvocMode::Cloud;
    peakParams.peakResidue = 0.82f;
    auto residueParams = peakParams;
    residueParams.peakResidue = -0.82f;
    auto lockedParams = peakParams;
    lockedParams.phaseMode = s3g::AcapellaPvocPhaseMode::PeakLocked;
    lockedParams.coherence = 0.96f;
    auto diffuseParams = lockedParams;
    diffuseParams.phaseMode = s3g::AcapellaPvocPhaseMode::Diffuse;
    diffuseParams.coherence = 0.08f;
    diffuseParams.phaseDrift = 0.78f;
    const auto pitched = renderPvoc(pitchParams);
    const auto formanted = renderPvoc(formantParams);
    const auto peaks = renderPvoc(peakParams);
    const auto residue = renderPvoc(residueParams);
    const auto locked = renderPvoc(lockedParams);
    const auto diffuse = renderPvoc(diffuseParams);
    if (pitched.empty() || formanted.empty() || peaks.empty()
        || residue.empty() || locked.empty() || diffuse.empty()
        || !(difference(pitched, formanted) > 2.0)
        || !(difference(peaks, residue) > 2.0)
        || !(difference(locked, diffuse) > 2.0)) {
        std::cerr << "PVOC frequency, partial, or phase axes collapsed\n";
        return false;
    }

    s3g::AcapellaPvocField tailField;
    auto tailParams = base;
    tailParams.feedback = 0.82f;
    tailField.setParams(tailParams);
    if (!tailField.prepare(kSampleRate)
        || tailField.tailSamples() <= tailField.latencySamples()) {
        std::cerr << "PVOC tail reporting failed\n";
        return false;
    }
    return true;
}

bool ensemblePolyphonyAndDoublingProbe()
{
    s3g::AcapellaEnsembleParams invalid;
    invalid.polyphony = 99u;
    invalid.doubleAmount = -3.0f;
    invalid.doubleDetuneCents = std::numeric_limits<float>::infinity();
    invalid.doubleTimingMs = 1000.0f;
    invalid.doubleDirt = 8.0f;
    const auto sanitized = s3g::sanitizeAcapellaEnsembleParams(invalid);
    if (sanitized.polyphony != s3g::kAcapellaMaxPolyphony
        || sanitized.doubleAmount != 0.0f
        || sanitized.doubleDetuneCents != 7.0f
        || sanitized.doubleTimingMs != 45.0f
        || sanitized.doubleDirt != 1.0f) {
        std::cerr << "acapella ensemble sanitation failed\n";
        return false;
    }

    auto source = s3g::acapellaSourcePreset(
        s3g::AcapellaSourcePreset::DeathGrowl);
    source.retriggerMs = 8.0f;
    s3g::AcapellaEnsembleParams chordParams;
    chordParams.polyphony = 4u;
    chordParams.doubleAmount = 0.0f;
    s3g::AcapellaEnsembleSynth chord;
    chord.setSourceParams(source);
    chord.setParams(chordParams);
    chord.prepare(kSampleRate);
    constexpr std::array<float, 5u> frequencies {
        73.42f, 82.41f, 98.0f, 110.0f, 130.81f,
    };
    for (uint32_t note = 0u; note < 4u; ++note) {
        chord.trigger({
            { s3g::AcapellaVowel::O, s3g::AcapellaOnset::R,
                frequencies[note], 0.72f, 500.0f },
            static_cast<int32_t>(note + 1u), 0,
            static_cast<int16_t>(40u + note),
        });
    }
    double chordEnergy = 0.0;
    s3g::AcapellaEnsembleFrame previous {};
    for (uint32_t frame = 0u; frame < 8000u; ++frame) {
        previous = chord.processFrame();
        chordEnergy += static_cast<double>(previous.left) * previous.left;
    }
    if (chord.activeVoiceCount() != 4u || !(chordEnergy > 0.1)) {
        std::cerr << "acapella fixed polyphony did not sustain four notes\n";
        return false;
    }

    chord.trigger({
        { s3g::AcapellaVowel::A, s3g::AcapellaOnset::T,
            frequencies[4], 0.48f, 300.0f },
        5, 0, 44,
    });
    const auto stolen = chord.processFrame();
    const double stealStep = std::max(
        std::abs(static_cast<double>(stolen.left) - previous.left),
        std::abs(static_cast<double>(stolen.right) - previous.right));
    if (chord.activeVoiceCount() != 4u || !(stealStep < 0.14)) {
        std::cerr << "acapella voice stealing was unsafe: "
                  << chord.activeVoiceCount() << " voices, step "
                  << stealStep << '\n';
        return false;
    }
    chord.releaseAll();
    for (uint32_t frame = 0u; frame < 96000u && chord.active(); ++frame) {
        const auto output = chord.processFrame();
        if (!std::isfinite(output.left) || !std::isfinite(output.right)) {
            return false;
        }
    }
    if (chord.active()) {
        std::cerr << "acapella polyphonic release did not finish\n";
        return false;
    }

    s3g::AcapellaEnsembleParams singleParams;
    singleParams.polyphony = 1u;
    singleParams.doubleAmount = 0.0f;
    auto doubledParams = s3g::acapellaEnsemblePreset(
        s3g::AcapellaSourcePreset::DeathGrowl);
    doubledParams.polyphony = 1u;
    s3g::AcapellaEnsembleSynth single;
    s3g::AcapellaEnsembleSynth doubled;
    single.setSourceParams(source);
    doubled.setSourceParams(source);
    single.setParams(singleParams);
    doubled.setParams(doubledParams);
    single.prepare(kSampleRate);
    doubled.prepare(kSampleRate);
    const s3g::AcapellaEnsembleNote note {
        { s3g::AcapellaVowel::A, s3g::AcapellaOnset::H,
            82.41f, 0.82f, 500.0f },
        1, 0, 40,
    };
    single.trigger(note);
    doubled.trigger(note);
    double doublingDifference = 0.0;
    double stereoDifference = 0.0;
    for (uint32_t frame = 0u; frame < 18000u; ++frame) {
        const auto dry = single.processFrame();
        const auto wide = doubled.processFrame();
        doublingDifference += std::abs(static_cast<double>(wide.left)
            - dry.left);
        stereoDifference += std::abs(static_cast<double>(wide.left)
            - wide.right);
        if (!std::isfinite(wide.left) || !std::isfinite(wide.right)
            || std::abs(wide.left) > 0.881f
            || std::abs(wide.right) > 0.881f) {
            std::cerr << "acapella procedural double exceeded safety bound\n";
            return false;
        }
    }
    if (!(doublingDifference > 100.0) || !(stereoDifference > 20.0)) {
        std::cerr << "acapella procedural doubling was ineffective: "
                  << doublingDifference << " / " << stereoDifference
                  << '\n';
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!silenceSafetyAndLifecycleProbe()
        || !deterministicResetProbe()
        || !timbreAndArticulationProbe()
        || !deliveryAndControlProbe()
        || !retriggerContinuityProbe()
        || !hybridSourceAndFreshOnsetProbe()
        || !articulatoryWaveguideProbe()
        || !phonemeGestureSequencerProbe()
        || !textCompilerAndTempoSyncProbe()
        || !extremeVoiceProbe()
        || !vocalEffectsProbe()
        || !pvocFieldProbe()
        || !ensemblePolyphonyAndDoublingProbe()) {
        return 1;
    }
    if (sizeof(s3g::AcapellaSourceSynth) > 4096u) {
        std::cerr << "acapella source unexpectedly contains a large data table\n";
        return 1;
    }
    std::cout << "Acapella source synth smoke test passed\n";
    return 0;
}
