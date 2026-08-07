#include "s3g_drum_primitives.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

bool randomProbe()
{
    s3g::DrumRandom first(s3g::drumMixedSeed(3u, 91u, 0x1234u));
    s3g::DrumRandom second(s3g::drumMixedSeed(3u, 91u, 0x1234u));
    s3g::DrumRandom different(s3g::drumMixedSeed(4u, 91u, 0x1234u));
    bool diverged = false;
    for (uint32_t sample = 0u; sample < 4096u; ++sample) {
        const uint32_t left = first.nextU32();
        const uint32_t right = second.nextU32();
        const uint32_t other = different.nextU32();
        if (left != right || left == 0u) {
            std::cerr << "drum RNG was not deterministic/nonzero\n";
            return false;
        }
        diverged = diverged || left != other;
    }
    if (!diverged) {
        std::cerr << "different drum RNG seeds did not diverge\n";
        return false;
    }

    s3g::DrumRandom bounded(7u);
    for (uint32_t sample = 0u; sample < 4096u; ++sample) {
        const float value = bounded.bipolar();
        if (!std::isfinite(value) || value < -1.0f || value > 1.0f) {
            std::cerr << "drum RNG left its bipolar range\n";
            return false;
        }
    }
    return true;
}

bool envelopeProbe()
{
    for (float sampleRate : { 44100.0f, 48000.0f, 96000.0f }) {
        s3g::DrumExponentialEnvelope envelope;
        envelope.configure(0.25f, sampleRate, 0.001f);
        envelope.trigger();
        const uint32_t samples = static_cast<uint32_t>(sampleRate * 0.25f);
        float previous = 2.0f;
        for (uint32_t sample = 0u; sample < samples; ++sample) {
            const float value = envelope.process();
            if (!std::isfinite(value) || value > previous || value < 0.0f) {
                std::cerr << "drum envelope was not finite/monotonic\n";
                return false;
            }
            previous = value;
        }
        if (!(envelope.value() > 0.00097f
                && envelope.value() < 0.00103f)) {
            std::cerr << "drum envelope time was inaccurate at "
                      << sampleRate << ": " << envelope.value() << "\n";
            return false;
        }
        envelope.reset();
        if (envelope.active() || envelope.process() != 0.0f) {
            std::cerr << "drum envelope reset failed\n";
            return false;
        }
    }
    return true;
}

double estimateFrequency(const std::vector<float>& signal,
    double sampleRate, uint32_t begin, uint32_t end)
{
    end = std::min<uint32_t>(end, static_cast<uint32_t>(signal.size()));
    uint32_t crossings = 0u;
    for (uint32_t sample = std::max<uint32_t>(begin, 1u);
         sample < end; ++sample) {
        if (signal[sample - 1u] <= 0.0f && signal[sample] > 0.0f) {
            ++crossings;
        }
    }
    const double seconds = static_cast<double>(end - begin) / sampleRate;
    return seconds > 0.0 ? static_cast<double>(crossings) / seconds : 0.0;
}

bool resonatorProbe()
{
    constexpr float sampleRate = 48000.0f;
    s3g::DrumModalResonator resonator;
    resonator.configure(731.0f, 0.5f, sampleRate);
    resonator.strike(1.0f);
    std::vector<float> rendered(24000u);
    float peak = 0.0f;
    for (float& sample : rendered) {
        sample = resonator.process();
        peak = std::max(peak, std::abs(sample));
        if (!std::isfinite(sample) || std::abs(sample) > 1.01f) {
            std::cerr << "modal resonator became unstable\n";
            return false;
        }
    }
    const double frequency = estimateFrequency(rendered, sampleRate,
        2400u, 12000u);
    if (!(peak > 0.90f) || !(frequency > 725.0 && frequency < 737.0)
        || !(resonator.magnitudeSquared() > 0.9e-6f
            && resonator.magnitudeSquared() < 1.1e-6f)) {
        std::cerr << "modal resonator frequency/decay failed: "
                  << peak << ", " << frequency << ", "
                  << resonator.magnitudeSquared() << "\n";
        return false;
    }
    resonator.reset();
    if (resonator.active() || resonator.process() != 0.0f) {
        std::cerr << "modal resonator reset failed\n";
        return false;
    }
    return true;
}

bool coefficientAndTailProbe()
{
    const float low = s3g::drumOnePoleFrequencyCoefficient(100.0f, 48000.0f);
    const float high = s3g::drumOnePoleFrequencyCoefficient(10000.0f, 48000.0f);
    const float fast = s3g::drumOnePoleTimeCoefficient(0.001f, 48000.0f);
    const float slow = s3g::drumOnePoleTimeCoefficient(0.1f, 48000.0f);
    if (!(low > 0.0f && low < high && high < 1.0f)
        || !(slow > 0.0f && slow < fast && fast < 1.0f)) {
        std::cerr << "drum one-pole coefficient ordering failed\n";
        return false;
    }

    const uint32_t tail = s3g::drumLongestTailSamples(48000.0,
        { 0.2, 1.75, 0.9 }, 0.05, 40.0);
    const uint32_t invalid = s3g::drumLongestTailSamples(
        std::numeric_limits<double>::quiet_NaN(),
        { std::numeric_limits<double>::quiet_NaN(), 0.25 });
    if (tail != 84000u || invalid != 12000u
        || s3g::drumSafeSampleRate(
            std::numeric_limits<double>::infinity()) != 48000.0) {
        std::cerr << "longest-tail/sample-rate sanitation failed\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!randomProbe() || !envelopeProbe() || !resonatorProbe()
        || !coefficientAndTailProbe()) {
        return 1;
    }
    std::cout << "drum primitives smoke passed\n";
    return 0;
}
