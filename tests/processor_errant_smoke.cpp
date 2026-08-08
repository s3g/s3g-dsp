#include "s3g_processor_errant.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

struct Render {
    std::vector<float> left;
    std::vector<float> right;
};

Render render(const s3g::ProcessorErrantParams& params, int note,
    double seconds, bool sendNoteOff = false, double noteOffSeconds = 0.5)
{
    constexpr double sampleRate = 48000.0;
    s3g::ProcessorErrant engine;
    engine.prepare(sampleRate);
    engine.setParams(params);
    engine.noteOn(note, 0.87f, 12, 0);
    const uint32_t frames = static_cast<uint32_t>(seconds * sampleRate);
    Render result;
    result.left.resize(frames);
    result.right.resize(frames);
    const uint32_t releaseFrame = static_cast<uint32_t>(
        noteOffSeconds * sampleRate);
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        if (sendNoteOff && frame == releaseFrame) {
            engine.noteOff(note, 12, 0);
        }
        engine.processFrame(result.left[frame], result.right[frame]);
    }
    return result;
}

double energy(const std::vector<float>& signal, uint32_t begin = 0u)
{
    double result = 0.0;
    for (uint32_t i = std::min<uint32_t>(begin, signal.size());
         i < signal.size(); ++i) {
        result += static_cast<double>(signal[i]) * signal[i];
    }
    return result;
}

double difference(const Render& a, const Render& b)
{
    const size_t frames = std::min(a.left.size(), b.left.size());
    double result = 0.0;
    for (size_t i = 0u; i < frames; ++i) {
        result += std::fabs(static_cast<double>(a.left[i]) - b.left[i]);
        result += std::fabs(static_cast<double>(a.right[i]) - b.right[i]);
    }
    return result;
}

double stereoDifference(const Render& signal)
{
    double result = 0.0;
    for (size_t i = 0u; i < signal.left.size(); ++i) {
        const double side = static_cast<double>(signal.left[i])
            - signal.right[i];
        result += side * side;
    }
    return result;
}

double maximumStep(const Render& signal)
{
    double result = 0.0;
    for (size_t i = 1u; i < signal.left.size(); ++i) {
        result = std::max(result, std::fabs(
            static_cast<double>(signal.left[i]) - signal.left[i - 1u]));
        result = std::max(result, std::fabs(
            static_cast<double>(signal.right[i]) - signal.right[i - 1u]));
    }
    return result;
}

uint32_t isolatedStepCount(const std::vector<float>& signal,
    double absoluteThreshold, double localRatio)
{
    if (signal.size() < 130u) return 0u;
    std::vector<double> derivative(signal.size(), 0.0);
    for (size_t i = 1u; i < signal.size(); ++i) {
        derivative[i] = std::fabs(static_cast<double>(signal[i])
            - signal[i - 1u]);
    }
    double local = 0.0;
    for (size_t i = 1u; i < 129u; ++i) local += derivative[i];
    uint32_t result = 0u;
    for (size_t i = 65u; i + 64u < signal.size(); ++i) {
        const double neighborhood = std::max(1.0e-9,
            (local - derivative[i]) / 127.0);
        if (derivative[i] > absoluteThreshold
            && derivative[i] > neighborhood * localRatio) {
            ++result;
        }
        local -= derivative[i - 64u];
        local += derivative[i + 64u];
    }
    return result;
}

double differenceEnergy(const std::vector<float>& signal)
{
    double result = 0.0;
    for (size_t i = 1u; i < signal.size(); ++i) {
        const double delta = static_cast<double>(signal[i]) - signal[i - 1u];
        result += delta * delta;
    }
    return result;
}

double minimumWindowEnergy(const std::vector<float>& signal,
    uint32_t begin, uint32_t window)
{
    begin = std::min<uint32_t>(begin, signal.size());
    window = std::max<uint32_t>(1u, window);
    if (begin + window > signal.size()) return 0.0;
    double minimum = std::numeric_limits<double>::infinity();
    double running = 0.0;
    for (uint32_t i = begin; i < signal.size(); ++i) {
        running += static_cast<double>(signal[i]) * signal[i];
        if (i >= begin + window) {
            running -= static_cast<double>(signal[i - window])
                * signal[i - window];
        }
        if (i + 1u >= begin + window) minimum = std::min(minimum, running);
    }
    return std::isfinite(minimum) ? minimum : 0.0;
}

double sinusoidMagnitude(const std::vector<float>& signal, double frequency,
    uint32_t begin, uint32_t end)
{
    constexpr double sampleRate = 48000.0;
    begin = std::min<uint32_t>(begin, signal.size());
    end = std::min<uint32_t>(end, signal.size());
    if (end <= begin) return 0.0;
    constexpr double twoPi = 6.28318530717958647692;
    double real = 0.0;
    double imaginary = 0.0;
    for (uint32_t i = begin; i < end; ++i) {
        const double phase = twoPi * frequency
            * static_cast<double>(i) / sampleRate;
        real += signal[i] * std::cos(phase);
        imaginary -= signal[i] * std::sin(phase);
    }
    return std::sqrt(real * real + imaginary * imaginary)
        / static_cast<double>(end - begin);
}

double zeroCrossingRate(const std::vector<float>& signal,
    uint32_t begin, uint32_t end)
{
    begin = std::min<uint32_t>(begin, signal.size());
    end = std::min<uint32_t>(end, signal.size());
    if (end <= begin + 1u) return 0.0;
    uint32_t crossings = 0u;
    for (uint32_t i = begin + 1u; i < end; ++i) {
        if ((signal[i - 1u] < 0.0f) != (signal[i] < 0.0f)) ++crossings;
    }
    return static_cast<double>(crossings)
        / static_cast<double>(end - begin - 1u);
}

bool finiteAndBounded(const Render& render)
{
    for (size_t i = 0u; i < render.left.size(); ++i) {
        if (!std::isfinite(render.left[i]) || !std::isfinite(render.right[i])
            || std::fabs(render.left[i]) > 1.001f
            || std::fabs(render.right[i]) > 1.001f) {
            return false;
        }
    }
    return true;
}

bool defaultAndDeterminismProbe()
{
    s3g::ProcessorErrantParams params;
    const auto a = render(params, 60, 2.0);
    const auto b = render(params, 60, 2.0);
    if (!finiteAndBounded(a) || energy(a.left) < 1.0
        || energy(a.right) < 1.0) {
        std::cerr << "Errant default render was silent or unsafe\n";
        return false;
    }
    const double defaultStep = maximumStep(a);
    const uint32_t isolatedSteps = isolatedStepCount(a.left, 0.018, 7.0)
        + isolatedStepCount(a.right, 0.018, 7.0);
    if (defaultStep > 0.040 || isolatedSteps > 24u) {
        std::cerr << "Errant default contained click-like discontinuities: "
                  << defaultStep << ", isolated=" << isolatedSteps << '\n';
        return false;
    }
    if (difference(a, b) != 0.0) {
        std::cerr << "Errant fixed seed was not sample deterministic\n";
        return false;
    }
    params.seed += 1u;
    const auto c = render(params, 60, 2.0);
    if (difference(a, c) < 10.0) {
        std::cerr << "Errant adjacent seeds did not produce distinct phrases\n";
        return false;
    }
    return true;
}

bool durationProbe()
{
    s3g::ProcessorErrantParams params;
    params.mode = s3g::ErrantMode::Cell;
    params.span = 0.0f;
    const auto cell = render(params, 60, 1.0);
    if (!(energy(cell.left, 24000u) < 1.0e-5)) {
        std::cerr << "Errant short Cell did not finish autonomously\n";
        return false;
    }

    params.mode = s3g::ErrantMode::Phrase;
    params.span = 0.65f;
    const auto phrase = render(params, 60, 2.0);
    if (!(energy(phrase.left, 48000u) > 0.1)) {
        std::cerr << "Errant Phrase did not retain multi-second activity\n";
        return false;
    }

    params.mode = s3g::ErrantMode::Field;
    params.span = 0.3f;
    const auto field = render(params, 60, 3.0, true, 1.0);
    if (!(energy(field.left, 48000u) > 0.01
        && energy(field.left, 132000u) < 0.05)) {
        std::cerr << "Errant Field release did not decay to silence\n";
        return false;
    }
    return true;
}

bool ancestryAndTopologyProbe()
{
    s3g::ProcessorErrantParams direct;
    direct.mode = s3g::ErrantMode::Phrase;
    direct.span = 0.7f;
    direct.density = 0.9f;
    direct.ancestry = 0.0f;
    direct.mutation = 0.8f;
    direct.repeat = 0.8f;
    const auto root = render(direct, 55, 3.0);
    direct.ancestry = 1.0f;
    const auto descendants = render(direct, 55, 3.0);
    if (difference(root, descendants) < 20.0) {
        std::cerr << "Errant ancestry did not change descendant behavior\n";
        return false;
    }

    direct.topology = s3g::ErrantTopology::Spine;
    direct.width = 0.0f;
    const auto spine = render(direct, 55, 2.0);
    direct.topology = s3g::ErrantTopology::Side;
    direct.width = 1.0f;
    const auto side = render(direct, 55, 2.0);
    if (!(stereoDifference(spine) < 1.0e-8
        && stereoDifference(side) > 1.0)) {
        std::cerr << "Errant structural stereo topology was ineffective\n";
        return false;
    }
    return true;
}

bool pitchAndStressProbe()
{
    s3g::ProcessorErrantParams body;
    body.mode = s3g::ErrantMode::Field;
    body.material = 0.0f;
    body.mutation = 0.0f;
    body.ancestry = 0.0f;
    body.drive = 0.0f;
    body.tone = 1.0f;
    body.topology = s3g::ErrantTopology::Spine;
    body.width = 0.0f;
    body.keyRole = s3g::ErrantKeyRole::Pitch;
    const auto low = render(body, 48, 0.25);
    const auto high = render(body, 72, 0.25);
    const double lowRate = zeroCrossingRate(low.left, 3000u, 11000u);
    const double highRate = zeroCrossingRate(high.left, 3000u, 11000u);
    if (!(highRate > lowRate * 2.2)) {
        std::cerr << "Errant MIDI note tracking did not raise register: low="
                  << lowRate << " high=" << highRate << '\n';
        return false;
    }

    body.keyRole = s3g::ErrantKeyRole::Clock;
    body.span = 1.0f;
    body.density = 1.0f;
    const auto slowClock = render(body, 48, 0.25);
    const auto fastClock = render(body, 72, 0.25);
    const double slowClockRate = zeroCrossingRate(
        slowClock.left, 3000u, 11000u);
    const double fastClockRate = zeroCrossingRate(
        fastClock.left, 3000u, 11000u);
    const double clockPitchRatio = fastClockRate
        / std::max(1.0e-9, slowClockRate);
    if (!(clockPitchRatio > 0.62 && clockPitchRatio < 1.62
        && difference(slowClock, fastClock) > 5.0)) {
        std::cerr << "Errant Clock key role did not preserve body register "
                  << "while changing event time: ratio=" << clockPitchRatio
                  << '\n';
        return false;
    }

    s3g::ProcessorErrantParams stress;
    stress.mode = s3g::ErrantMode::Field;
    stress.material = 1.0f;
    stress.span = 1.0f;
    stress.density = 1.0f;
    stress.ancestry = 1.0f;
    stress.mutation = 1.0f;
    stress.repeat = 1.0f;
    stress.coherence = 0.0f;
    stress.drive = 1.0f;
    stress.topology = s3g::ErrantTopology::Side;
    stress.width = 1.0f;
    stress.outputGainDb = 6.0f;
    const auto stressed = render(stress, 96, 4.0);
    if (!finiteAndBounded(stressed) || energy(stressed.left) < 1.0) {
        std::cerr << "Errant high-mutation stress escaped bounds or fell silent\n";
        return false;
    }
    return true;
}

bool midiLineageProbe()
{
    constexpr double sampleRate = 48000.0;
    s3g::ProcessorErrant inherited;
    s3g::ProcessorErrant fresh;
    inherited.prepare(sampleRate);
    fresh.prepare(sampleRate);
    s3g::ProcessorErrantParams params;
    params.mode = s3g::ErrantMode::Field;
    params.ancestry = 0.0f;
    params.mutation = 0.58f;
    params.repeat = 0.72f;
    inherited.setParams(params);
    fresh.setParams(params);
    inherited.noteOn(60, 0.36f, 1, 0);
    fresh.noteOn(60, 0.36f, 1, 0);
    for (uint32_t frame = 0u; frame < 24000u; ++frame) {
        float aL = 0.0f;
        float aR = 0.0f;
        float bL = 0.0f;
        float bR = 0.0f;
        inherited.processFrame(aL, aR);
        fresh.processFrame(bL, bR);
        if (aL != bL || aR != bR) {
            std::cerr << "Errant identical ancestors diverged before inheritance\n";
            return false;
        }
    }
    inherited.noteOff(60, 1, 0);
    fresh.noteOff(60, 1, 0);
    for (uint32_t frame = 0u; frame < 60000u; ++frame) {
        float aL = 0.0f;
        float aR = 0.0f;
        float bL = 0.0f;
        float bR = 0.0f;
        inherited.processFrame(aL, aR);
        fresh.processFrame(bL, bR);
    }
    if (inherited.lineageFrames() == 0u
        || inherited.lineageFrames() != fresh.lineageFrames()) {
        std::cerr << "Errant family archive did not retain the first note\n";
        return false;
    }

    auto inheritedParams = params;
    inheritedParams.ancestry = 1.0f;
    inherited.setParams(inheritedParams);
    inherited.noteOn(60, 0.28f, 2, 0);
    fresh.noteOn(60, 0.28f, 2, 0);
    if (inherited.lastNoteInterval() != 0
        || inherited.lineageGeneration() != 2u) {
        std::cerr << "Errant repeated-note relationship was not recorded\n";
        return false;
    }
    double branchDifference = 0.0;
    for (uint32_t frame = 0u; frame < 36000u; ++frame) {
        float aL = 0.0f;
        float aR = 0.0f;
        float bL = 0.0f;
        float bR = 0.0f;
        inherited.processFrame(aL, aR);
        fresh.processFrame(bL, bR);
        branchDifference += std::fabs(static_cast<double>(aL) - bL)
            + std::fabs(static_cast<double>(aR) - bR);
    }
    if (branchDifference < 30.0) {
        std::cerr << "Errant repeated note did not inherit the family archive\n";
        return false;
    }

    s3g::ProcessorErrant chord;
    chord.prepare(sampleRate);
    chord.setParams(inheritedParams);
    for (int note : { 48, 55, 60, 64, 67, 72, 76, 79 }) {
        chord.noteOn(note, 0.72f);
    }
    double chordEnergy = 0.0;
    for (uint32_t frame = 0u; frame < 12000u; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        chord.processFrame(left, right);
        chordEnergy += static_cast<double>(left) * left
            + static_cast<double>(right) * right;
    }
    if (s3g::ProcessorErrant::kVoiceCount != 8u || chordEnergy < 10.0) {
        std::cerr << "Errant eight-voice MIDI chord was not audible\n";
        return false;
    }
    return true;
}

bool mutantBassAndTransientProbe()
{
    s3g::ProcessorErrantParams bass;
    bass.mode = s3g::ErrantMode::Field;
    bass.material = 0.0f;
    bass.span = 0.72f;
    bass.density = 1.0f;
    bass.ancestry = 0.0f;
    bass.mutation = 0.0f;
    bass.repeat = 0.0f;
    bass.coherence = 1.0f;
    bass.keyRole = s3g::ErrantKeyRole::Pitch;
    bass.tone = -0.05f;
    bass.resonance = 0.46f;
    bass.filterContour = 0.35f;
    bass.crosswire = 0.0f;
    bass.drive = 0.28f;
    bass.topology = s3g::ErrantTopology::Spine;
    bass.width = 0.0f;
    bass.sub = 0.0f;
    const auto noSub = render(bass, 48, 1.0);
    bass.sub = 1.0f;
    const auto fullSub = render(bass, 48, 1.0);
    const double noSubMagnitude = sinusoidMagnitude(
        noSub.left, 32.7032, 12000u, 45000u);
    const double fullSubMagnitude = sinusoidMagnitude(
        fullSub.left, 32.7032, 12000u, 45000u);
    if (!(fullSubMagnitude > noSubMagnitude * 1.45)) {
        std::cerr << "Errant sub oscillator did not establish the bass octave: "
                  << noSubMagnitude << " -> " << fullSubMagnitude << '\n';
        return false;
    }

    bass.sub = 0.62f;
    bass.tone = -0.85f;
    const auto dark = render(bass, 48, 0.8);
    bass.tone = 0.85f;
    const auto bright = render(bass, 48, 0.8);
    if (!(differenceEnergy(bright.left) > differenceEnergy(dark.left) * 2.0)) {
        std::cerr << "Errant ladder cutoff did not open the bass circuit\n";
        return false;
    }

    s3g::ProcessorErrantParams damaged;
    damaged.mode = s3g::ErrantMode::Field;
    damaged.material = 1.0f;
    damaged.span = 0.15f;
    damaged.density = 1.0f;
    damaged.ancestry = 1.0f;
    damaged.mutation = 1.0f;
    damaged.repeat = 1.0f;
    damaged.coherence = 0.0f;
    damaged.tone = 1.0f;
    damaged.resonance = 0.82f;
    damaged.filterContour = 1.0f;
    damaged.crosswire = 1.0f;
    damaged.drive = 0.8f;
    damaged.outputGainDb = 0.0f;
    const auto smoothedDamage = render(damaged, 43, 3.0);
    const double step = maximumStep(smoothedDamage);
    if (!finiteAndBounded(smoothedDamage) || step > 0.10) {
        std::cerr << "Errant mutation path produced a DSP-like discontinuity: "
                  << step << '\n';
        return false;
    }
    return true;
}

bool bassSpineClarityProbe()
{
    s3g::ProcessorErrantParams stable;
    stable.mode = s3g::ErrantMode::Field;
    stable.material = 0.64f;
    stable.span = 0.42f;
    stable.density = 0.28f;
    stable.ancestry = 0.0f;
    stable.mutation = 0.0f;
    stable.repeat = 0.0f;
    stable.coherence = 1.0f;
    stable.keyRole = s3g::ErrantKeyRole::Pitch;
    stable.noteTracking = 1.0f;
    stable.tone = 0.05f;
    stable.resonance = 0.48f;
    stable.filterContour = 0.38f;
    stable.crosswire = 0.0f;
    stable.drive = 0.58f;
    stable.sub = 0.82f;
    stable.topology = s3g::ErrantTopology::Spine;
    stable.width = 0.0f;
    const auto clean = render(stable, 48, 2.0);

    auto crossed = stable;
    crossed.material = 1.0f;
    crossed.density = 1.0f;
    crossed.ancestry = 1.0f;
    crossed.mutation = 1.0f;
    crossed.repeat = 1.0f;
    crossed.coherence = 0.0f;
    crossed.crosswire = 1.0f;
    crossed.drive = 0.9f;
    const auto damaged = render(crossed, 48, 2.0);

    const double cleanRoot = sinusoidMagnitude(
        clean.left, 65.4064, 24000u, 90000u);
    const double damagedRoot = sinusoidMagnitude(
        damaged.left, 65.4064, 24000u, 90000u);
    const double cleanSub = sinusoidMagnitude(
        clean.left, 32.7032, 24000u, 90000u);
    const double damagedSub = sinusoidMagnitude(
        damaged.left, 32.7032, 24000u, 90000u);
    const double quietestBassWindow = minimumWindowEnergy(
        damaged.left, 12000u, 2048u);
    if (!(damagedRoot + damagedSub > (cleanRoot + cleanSub) * 0.16
        && damagedRoot + damagedSub > 0.006
        && quietestBassWindow > 0.001)) {
        std::cerr << "Errant Crosswire displaced its MIDI-rooted bass spine: "
                  << "clean=" << cleanRoot + cleanSub
                  << " damaged=" << damagedRoot + damagedSub
                  << " minimum window=" << quietestBassWindow << '\n';
        return false;
    }
    if (!(difference(clean, damaged) > 100.0
        && finiteAndBounded(damaged))) {
        std::cerr << "Errant genealogy did not remain distinct around the "
                  << "protected bass body\n";
        return false;
    }
    return true;
}

bool rapidRetriggerSanitationProbe()
{
    constexpr double sampleRate = 48000.0;
    constexpr uint32_t frames = 96000u;
    constexpr uint32_t noteSpacing = 2400u;
    s3g::ProcessorErrant engine;
    engine.prepare(sampleRate);
    s3g::ProcessorErrantParams params;
    params.mode = s3g::ErrantMode::Field;
    params.span = 0.45f;
    params.material = 0.72f;
    params.mutation = 0.62f;
    params.ancestry = 0.78f;
    params.repeat = 0.68f;
    params.crosswire = 0.58f;
    params.drive = 0.62f;
    engine.setParams(params);

    Render sequence;
    sequence.left.resize(frames);
    sequence.right.resize(frames);
    int previousNote = -1;
    int32_t previousId = -1;
    int32_t nextId = 1;
    const std::array<int, 8u> notes {{ 36, 36, 39, 36, 43, 41, 36, 34 }};
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        if (frame % noteSpacing == 0u) {
            if (previousNote >= 0) {
                engine.noteOff(previousNote, previousId, 0);
            }
            const int note = notes[(frame / noteSpacing) % notes.size()];
            previousNote = note;
            previousId = nextId++;
            engine.noteOn(note, 0.84f, previousId, 0);
        }
        engine.processFrame(sequence.left[frame], sequence.right[frame]);
    }
    const double step = maximumStep(sequence);
    if (!finiteAndBounded(sequence) || energy(sequence.left) < 10.0
        || step > 0.060) {
        std::cerr << "Errant rapid retrigger/voice stealing clicked: "
                  << step << '\n';
        return false;
    }
    return true;
}

} // namespace

int main()
{
    const bool ok = defaultAndDeterminismProbe()
        && durationProbe()
        && ancestryAndTopologyProbe()
        && pitchAndStressProbe()
        && midiLineageProbe()
        && mutantBassAndTransientProbe()
        && bassSpineClarityProbe()
        && rapidRetriggerSanitationProbe();
    if (!ok) return 1;
    std::cout << "Processor Errant smoke passed\n";
    return 0;
}
