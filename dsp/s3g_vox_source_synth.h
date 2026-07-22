#pragma once

#include "s3g_vox_builder.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace s3g {

enum class VoxSourceVocabulary : uint8_t {
    Core35,
    Full92,
};

struct VoxSourceSeedProfile {
    bool valid = false;
    float baseFrequencyHz = 150.0f;
    float formant1Scale = 1.0f;
    float formant2Scale = 1.0f;
    float formant3Scale = 1.0f;
    float breath = 0.16f;
    float roughness = 0.06f;
    float brightness = 0.50f;
    float periodicity = 0.75f;
};

struct VoxSourceSynthParams {
    int sampleRate = 48000;
    float baseFrequencyHz = 150.0f;
    float tractScale = 1.0f;
    float breath = 0.16f;
    float roughness = 0.06f;
    float articulation = 0.72f;
    float consonantStrength = 0.72f;
    float durationMs = 720.0f;
    float variation = 0.16f;
    uint32_t randomSeed = 1979u;
    bool seeded = false;
    VoxSourceSeedProfile seedProfile;
};

struct VoxGeneratedVoiceSample {
    std::string alias;
    std::vector<float> samples;
};

struct VoxGeneratedVoicebank {
    int sampleRate = 48000;
    std::vector<VoxGeneratedVoiceSample> entries;
};

inline const char* voxSourceVocabularyName(VoxSourceVocabulary vocabulary)
{
    return vocabulary == VoxSourceVocabulary::Full92 ? "FULL 92" : "CORE 35";
}

inline const std::vector<std::string>& voxSourceAliases(VoxSourceVocabulary vocabulary)
{
    return vocabulary == VoxSourceVocabulary::Full92
        ? voxBuilderAutoPhraseAliases() : voxBuilderCoreAliases();
}

namespace vox_source_detail {

constexpr float kPi = 3.14159265358979323846f;

inline float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

inline float smoothstep(float edge0, float edge1, float value)
{
    if (edge1 <= edge0) return value >= edge1 ? 1.0f : 0.0f;
    const float x = clamp01((value - edge0) / (edge1 - edge0));
    return x * x * (3.0f - 2.0f * x);
}

struct Random {
    uint32_t state = 1u;

    explicit Random(uint32_t seed)
        : state(seed == 0u ? 1u : seed)
    {
    }

    uint32_t nextU32()
    {
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        return state;
    }

    float bipolar()
    {
        return static_cast<float>(nextU32() & 0x00ffffffu) / 8388607.5f - 1.0f;
    }
};

inline uint32_t aliasHash(const std::string& alias)
{
    uint32_t hash = 2166136261u;
    for (const unsigned char ch : alias) {
        hash ^= ch;
        hash *= 16777619u;
    }
    return hash;
}

struct Biquad {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;

    void setBandpass(float frequency, float q, float sampleRate)
    {
        frequency = std::clamp(frequency, 35.0f, sampleRate * 0.45f);
        q = std::clamp(q, 0.25f, 24.0f);
        const float omega = 2.0f * kPi * frequency / sampleRate;
        const float alpha = std::sin(omega) / (2.0f * q);
        const float a0 = 1.0f + alpha;
        b0 = alpha / a0;
        b1 = 0.0f;
        b2 = -alpha / a0;
        a1 = -2.0f * std::cos(omega) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    float process(float input)
    {
        const float output = b0 * input + z1;
        z1 = b1 * input - a1 * output + z2;
        z2 = b2 * input - a2 * output;
        return output;
    }
};

struct VowelShape {
    std::array<float, 4> frequency;
    std::array<float, 4> bandwidth;
};

inline VowelShape vowelShape(char vowel)
{
    switch (vowel) {
    case 'e': return { { 460.0f, 1900.0f, 2650.0f, 3500.0f }, { 80.0f, 110.0f, 150.0f, 230.0f } };
    case 'i': return { { 300.0f, 2350.0f, 3000.0f, 3700.0f }, { 65.0f, 105.0f, 150.0f, 240.0f } };
    case 'o': return { { 520.0f, 900.0f, 2600.0f, 3400.0f }, { 80.0f, 100.0f, 150.0f, 230.0f } };
    case 'u': return { { 350.0f, 780.0f, 2300.0f, 3300.0f }, { 70.0f, 95.0f, 150.0f, 230.0f } };
    case 'a':
    default: return { { 800.0f, 1250.0f, 2850.0f, 3600.0f }, { 90.0f, 110.0f, 160.0f, 240.0f } };
    }
}

struct AliasParts {
    std::string onset;
    char vowel = 'a';
    bool standaloneNasal = false;
};

inline AliasParts splitAlias(const std::string& alias)
{
    std::string key;
    for (const unsigned char ch : alias) {
        if (std::isalpha(ch)) key.push_back(static_cast<char>(std::tolower(ch)));
    }
    if (key == "n" || key == "m") return { key, 'a', true };
    if (key == "ya") return { "y", 'a', false };
    if (!key.empty()) {
        const char last = key.back();
        if (last == 'a' || last == 'e' || last == 'i' || last == 'o' || last == 'u') {
            key.pop_back();
            return { key, last, false };
        }
    }
    return { key, 'a', false };
}

enum class OnsetKind : uint8_t {
    None,
    Stop,
    VoicedStop,
    Fricative,
    VoicedFricative,
    Nasal,
    Liquid,
};

inline OnsetKind onsetKind(const std::string& onset)
{
    if (onset.empty()) return OnsetKind::None;
    if (onset == "k" || onset == "t" || onset == "p" || onset == "ch") return OnsetKind::Stop;
    if (onset == "b" || onset == "d" || onset == "g" || onset == "j") return OnsetKind::VoicedStop;
    if (onset == "s" || onset == "sh" || onset == "f" || onset == "h") return OnsetKind::Fricative;
    if (onset == "z") return OnsetKind::VoicedFricative;
    if (onset == "m" || onset == "n") return OnsetKind::Nasal;
    return OnsetKind::Liquid;
}

inline std::array<float, 3> onsetLocus(const std::string& onset,
                                       const VowelShape& vowel)
{
    if (onset == "b" || onset == "p" || onset == "m" || onset == "w") {
        return { 300.0f, 700.0f, 2450.0f };
    }
    if (onset == "d" || onset == "t" || onset == "n" || onset == "s"
        || onset == "z" || onset == "ch" || onset == "j") {
        return { 320.0f, 1800.0f, 2750.0f };
    }
    if (onset == "g" || onset == "k") {
        const float pinch = 0.55f * vowel.frequency[1] + 0.45f * vowel.frequency[2];
        return { 340.0f, pinch, pinch + 180.0f };
    }
    if (onset == "r") return { 380.0f, 1250.0f, 1650.0f };
    if (onset == "y") return { 270.0f, 2250.0f, 3000.0f };
    if (onset == "h" || onset == "f" || onset == "sh") {
        return { vowel.frequency[0] * 0.70f, vowel.frequency[1] * 0.90f, vowel.frequency[2] };
    }
    return { vowel.frequency[0], vowel.frequency[1], vowel.frequency[2] };
}

inline float fricativeCenter(const std::string& onset)
{
    if (onset == "s" || onset == "z") return 6500.0f;
    if (onset == "sh" || onset == "ch" || onset == "j") return 3800.0f;
    if (onset == "f") return 2200.0f;
    return 3000.0f;
}

inline std::pair<float, float> estimatePitch(const std::vector<float>& samples,
                                              int sampleRate,
                                              uint64_t start,
                                              uint64_t end)
{
    if (sampleRate <= 0 || start >= samples.size()) return { 0.0f, 0.0f };
    end = std::min<uint64_t>(end, samples.size());
    if (end <= start + 255u) return { 0.0f, 0.0f };

    const uint32_t stride = std::max<uint32_t>(1u, static_cast<uint32_t>(sampleRate / 12000));
    const float reducedRate = static_cast<float>(sampleRate) / static_cast<float>(stride);
    const size_t maximum = static_cast<size_t>(reducedRate * 1.2f);
    const uint64_t span = end - start;
    const uint64_t first = start + (span > static_cast<uint64_t>(maximum * stride)
        ? (span - static_cast<uint64_t>(maximum * stride)) / 2u : 0u);
    std::vector<float> source;
    source.reserve(std::min<size_t>(maximum, static_cast<size_t>((end - first) / stride)));
    for (uint64_t sample = first; sample < end && source.size() < maximum; sample += stride) {
        source.push_back(samples[static_cast<size_t>(sample)]);
    }
    if (source.size() < 256u) return { 0.0f, 0.0f };
    double mean = 0.0;
    for (const float value : source) mean += value;
    mean /= static_cast<double>(source.size());
    for (float& value : source) value -= static_cast<float>(mean);

    const size_t minimumLag = std::max<size_t>(2u, static_cast<size_t>(reducedRate / 500.0f));
    const size_t maximumLag = std::min<size_t>(source.size() / 3u,
        static_cast<size_t>(reducedRate / 55.0f));
    float bestCorrelation = 0.0f;
    size_t bestLag = 0u;
    for (size_t lag = minimumLag; lag <= maximumLag; ++lag) {
        double product = 0.0;
        double leftEnergy = 0.0;
        double rightEnergy = 0.0;
        for (size_t i = lag; i < source.size(); ++i) {
            const double left = source[i];
            const double right = source[i - lag];
            product += left * right;
            leftEnergy += left * left;
            rightEnergy += right * right;
        }
        const double denominator = std::sqrt(leftEnergy * rightEnergy);
        const float correlation = denominator > 0.000000001
            ? static_cast<float>(product / denominator) : 0.0f;
        if (correlation > bestCorrelation) {
            bestCorrelation = correlation;
            bestLag = lag;
        }
    }
    return bestLag > 0u
        ? std::pair<float, float> { reducedRate / static_cast<float>(bestLag), bestCorrelation }
        : std::pair<float, float> { 0.0f, 0.0f };
}

inline void normalizeGenerated(std::vector<float>& samples, int sampleRate)
{
    if (samples.empty()) return;
    double mean = 0.0;
    for (const float sample : samples) mean += sample;
    mean /= static_cast<double>(samples.size());
    float peak = 0.0f;
    for (float& sample : samples) {
        sample -= static_cast<float>(mean);
        peak = std::max(peak, std::fabs(sample));
    }
    const float gain = peak > 0.000001f ? std::min(16.0f, 0.68f / peak) : 1.0f;
    const size_t fade = std::min<size_t>(samples.size() / 2u,
        std::max<size_t>(1u, static_cast<size_t>(sampleRate) / 125u));
    for (size_t i = 0u; i < samples.size(); ++i) {
        float envelope = 1.0f;
        if (i < fade) envelope = static_cast<float>(i) / static_cast<float>(fade);
        if (samples.size() - i - 1u < fade) {
            envelope = std::min(envelope, static_cast<float>(samples.size() - i - 1u)
                / static_cast<float>(fade));
        }
        samples[i] = std::clamp(samples[i] * gain * envelope, -0.98f, 0.98f);
    }

    // Remove isolated one-sample discontinuities without suppressing sustained
    // frication or the ordinary derivative of a voiced waveform.
    const float correctionRelease = std::exp(
        -1.0f / (0.0025f * static_cast<float>(sampleRate)));
    const float derivativeRelease = std::exp(
        -1.0f / (0.040f * static_cast<float>(sampleRate)));
    float outputPrevious = 0.0f;
    float derivativeEnvelope = 0.0f;
    float correction = 0.0f;
    for (float& sample : samples) {
        const float raw = sample;
        const float allowedStep = std::max(0.08f, derivativeEnvelope * 1.5f);
        float conditioned = raw + correction;
        const float outputStep = conditioned - outputPrevious;
        if (std::fabs(outputStep) > allowedStep) {
            conditioned = outputPrevious + std::copysign(allowedStep, outputStep);
            correction = conditioned - raw;
        }
        sample = std::clamp(conditioned, -0.98f, 0.98f);
        const float conditionedStep = std::fabs(sample - outputPrevious);
        if (conditionedStep > derivativeEnvelope) {
            derivativeEnvelope += (conditionedStep - derivativeEnvelope) * 0.08f;
        } else {
            derivativeEnvelope *= derivativeRelease;
        }
        correction *= correctionRelease;
        outputPrevious = sample;
    }
}

} // namespace vox_source_detail

inline VoxSourceSeedProfile voxSourceAnalyzeSeed(const std::vector<float>& samples,
                                                  int sampleRate,
                                                  uint64_t startSample,
                                                  uint64_t endSample,
                                                  const std::string& alias,
                                                  float pitchHintHz = 0.0f)
{
    VoxSourceSeedProfile profile;
    if (sampleRate <= 0 || samples.empty() || startSample >= samples.size()) return profile;
    endSample = std::min<uint64_t>(endSample, samples.size());
    if (endSample <= startSample + 255u) return profile;

    double energy = 0.0;
    double differenceEnergy = 0.0;
    double lowEnergy = 0.0;
    float low = samples[static_cast<size_t>(startSample)];
    float previous = low;
    size_t crossings = 0u;
    for (uint64_t i = startSample; i < endSample; ++i) {
        const float value = samples[static_cast<size_t>(i)];
        low += 0.025f * (value - low);
        energy += static_cast<double>(value) * value;
        lowEnergy += static_cast<double>(low) * low;
        const float difference = value - previous;
        differenceEnergy += static_cast<double>(difference) * difference;
        if ((value < 0.0f) != (previous < 0.0f)) ++crossings;
        previous = value;
    }
    if (energy < 0.0000001) return profile;

    const auto pitch = vox_source_detail::estimatePitch(
        samples, sampleRate, startSample, endSample);
    profile.baseFrequencyHz = pitchHintHz >= 40.0f
        ? pitchHintHz : (pitch.first >= 40.0f ? pitch.first : 150.0f);
    profile.periodicity = std::clamp(pitch.second, 0.0f, 1.0f);
    const float highRatio = std::clamp(static_cast<float>(differenceEnergy / energy) * 0.35f,
        0.0f, 1.0f);
    const float lowRatio = std::clamp(static_cast<float>(lowEnergy / energy), 0.0f, 1.0f);
    const float crossingRate = static_cast<float>(crossings)
        / static_cast<float>(std::max<uint64_t>(1u, endSample - startSample));
    profile.brightness = std::clamp(0.15f + 0.62f * highRatio + 0.23f * (1.0f - lowRatio),
        0.05f, 0.95f);
    profile.breath = std::clamp(0.05f + 0.56f * (1.0f - profile.periodicity)
        + 0.18f * highRatio, 0.03f, 0.72f);
    profile.roughness = std::clamp(0.02f + 0.35f * (1.0f - profile.periodicity)
        + crossingRate * 0.8f, 0.02f, 0.55f);

    const auto acoustic = voxBuilderAnalyzeAcoustics(
        samples, sampleRate, startSample, endSample);
    const auto parts = vox_source_detail::splitAlias(alias);
    const auto reference = vox_source_detail::vowelShape(parts.vowel);
    if (acoustic.formant1Hz > 100.0f) {
        profile.formant1Scale = std::clamp(acoustic.formant1Hz / reference.frequency[0], 0.68f, 1.42f);
    }
    if (acoustic.formant2Hz > acoustic.formant1Hz + 150.0f) {
        profile.formant2Scale = std::clamp(acoustic.formant2Hz / reference.frequency[1], 0.72f, 1.35f);
    }
    profile.formant3Scale = std::clamp(
        std::sqrt(profile.formant1Scale * profile.formant2Scale), 0.74f, 1.32f);
    profile.valid = true;
    return profile;
}

inline std::vector<float> voxSourceGenerateAlias(const std::string& alias,
                                                 VoxSourceSynthParams params,
                                                 uint32_t aliasIndex = 0u)
{
    using namespace vox_source_detail;
    const int sampleRate = std::clamp(params.sampleRate, 8000, 192000);
    params.baseFrequencyHz = std::clamp(params.baseFrequencyHz, 55.0f, 520.0f);
    params.tractScale = std::clamp(params.tractScale, 0.70f, 1.35f);
    params.breath = clamp01(params.breath);
    params.roughness = clamp01(params.roughness);
    params.articulation = clamp01(params.articulation);
    params.consonantStrength = clamp01(params.consonantStrength);
    params.durationMs = std::clamp(params.durationMs, 240.0f, 1800.0f);
    params.variation = clamp01(params.variation);

    const AliasParts parts = splitAlias(alias);
    const OnsetKind kind = onsetKind(parts.onset);
    const bool plainVowel = kind == OnsetKind::None;
    const float durationScale = plainVowel ? 1.08f : (parts.standaloneNasal ? 0.90f : 1.0f);
    const float durationSeconds = params.durationMs * durationScale * 0.001f;
    const size_t sampleCount = std::max<size_t>(256u,
        static_cast<size_t>(std::lround(durationSeconds * static_cast<float>(sampleRate))));
    std::vector<float> output(sampleCount, 0.0f);

    Random random(params.randomSeed ^ aliasHash(alias) ^ (aliasIndex * 0x9e3779b9u));
    const float aliasVariation = random.bipolar() * params.variation;
    const VoxSourceSeedProfile seed = params.seeded && params.seedProfile.valid
        ? params.seedProfile : VoxSourceSeedProfile {};
    const float baseFrequency = std::clamp((params.seeded && seed.valid
        ? 0.70f * params.baseFrequencyHz + 0.30f * seed.baseFrequencyHz
        : params.baseFrequencyHz) * std::pow(2.0f, aliasVariation * 0.10f), 55.0f, 520.0f);
    const float breath = clamp01(params.breath * (seed.valid ? 0.62f : 1.0f)
        + (seed.valid ? seed.breath * 0.38f : 0.0f));
    const float roughness = clamp01(params.roughness * (seed.valid ? 0.66f : 1.0f)
        + (seed.valid ? seed.roughness * 0.34f : 0.0f));
    const float brightness = seed.valid ? seed.brightness : 0.50f;
    const float tractFrequencyScale = 1.0f / params.tractScale;
    const std::array<float, 3> seedFormantScale {
        seed.valid ? seed.formant1Scale : 1.0f,
        seed.valid ? seed.formant2Scale : 1.0f,
        seed.valid ? seed.formant3Scale : 1.0f,
    };

    VowelShape vowel = vowelShape(parts.vowel);
    const auto locus = onsetLocus(parts.onset, vowel);
    const float onsetDuration = plainVowel ? 0.0f : std::clamp(
        0.155f - params.articulation * 0.070f + aliasVariation * 0.012f, 0.060f, 0.180f);
    const float burstTime = onsetDuration * 0.34f;

    std::array<Biquad, 4> formants;
    Biquad consonantFilter;
    Biquad nasalFilter;
    const float noiseCenter = std::min(fricativeCenter(parts.onset)
        * (0.82f + brightness * 0.36f), static_cast<float>(sampleRate) * 0.42f);
    consonantFilter.setBandpass(noiseCenter, parts.onset == "s" ? 1.8f : 1.1f,
        static_cast<float>(sampleRate));
    nasalFilter.setBandpass(parts.onset == "m" ? 240.0f : 300.0f, 3.2f,
        static_cast<float>(sampleRate));

    float phase = random.bipolar() * 0.5f + 0.5f;
    float previousFlow = 0.0f;
    float jitter = 0.0f;
    float jitterTarget = random.bipolar();
    float noiseLow = 0.0f;
    float glottalLow1 = 0.0f;
    float glottalLow2 = 0.0f;
    float dcX = 0.0f;
    float dcY = 0.0f;
    const float glottalCutoff = 1400.0f + brightness * 2200.0f;
    const float glottalCoefficient = 1.0f - std::exp(
        -2.0f * kPi * glottalCutoff / static_cast<float>(sampleRate));
    std::array<float, 4> formantGain {
        1.0f,
        0.78f + brightness * 0.20f,
        0.06f + brightness * 0.08f,
        0.015f + brightness * 0.025f,
    };

    for (size_t i = 0u; i < sampleCount; ++i) {
        const float time = static_cast<float>(i) / static_cast<float>(sampleRate);
        const float progress = static_cast<float>(i) / static_cast<float>(sampleCount - 1u);
        const float drift = std::sin(2.0f * kPi * (0.31f + params.variation * 0.42f) * time
            + static_cast<float>(aliasIndex) * 0.47f);
        jitter += (jitterTarget - jitter) * (0.0007f + roughness * 0.0020f);
        const float instantaneousFrequency = baseFrequency * std::max(0.72f,
            1.0f + drift * params.variation * 0.018f + jitter * roughness * 0.020f);
        phase += instantaneousFrequency / static_cast<float>(sampleRate);
        if (phase >= 1.0f) {
            phase -= std::floor(phase);
            jitterTarget = random.bipolar();
        }

        const float openQuotient = 0.54f + breath * 0.12f;
        float flow = 0.0f;
        if (phase < openQuotient) {
            const float p = phase / openQuotient;
            flow = 0.5f - 0.5f * std::cos(2.0f * kPi * p);
        } else {
            const float p = (phase - openQuotient) / (1.0f - openQuotient);
            flow = 0.22f * (1.0f - p) * (1.0f - p);
        }
        const float differentiated = (flow - previousFlow)
            * static_cast<float>(sampleRate) / std::max(55.0f, instantaneousFrequency) * 0.035f;
        previousFlow = flow;
        const float shimmer = 1.0f + roughness * 0.12f * jitter;
        const float white = random.bipolar();
        noiseLow += 0.035f * (white - noiseLow);
        const float highNoise = white - noiseLow;
        const float rawGlottal = std::tanh((0.86f * differentiated + 0.10f * (flow - 0.35f))
            * shimmer * 2.2f);
        glottalLow1 += glottalCoefficient * (rawGlottal - glottalLow1);
        glottalLow2 += glottalCoefficient * (glottalLow1 - glottalLow2);
        const float excitation = 0.72f * glottalLow2 + 0.28f * glottalLow1
            + white * breath * 0.055f;

        const float vowelBlend = plainVowel ? 1.0f
            : smoothstep(onsetDuration * 0.42f, onsetDuration, time);
        if ((i & 15u) == 0u) {
            for (size_t formant = 0u; formant < formants.size(); ++formant) {
                const float target = vowel.frequency[formant];
                const float start = formant < 3u ? locus[formant] : target * 0.92f;
                const float identity = formant < 3u ? seedFormantScale[formant]
                    : seedFormantScale[2u];
                const float frequency = (start + (target - start) * vowelBlend)
                    * tractFrequencyScale * identity;
                const float q = frequency / std::max(40.0f, vowel.bandwidth[formant]);
                formants[formant].setBandpass(frequency, q, static_cast<float>(sampleRate));
            }
        }

        float oral = 0.0f;
        for (size_t formant = 0u; formant < formants.size(); ++formant) {
            oral += formants[formant].process(excitation) * formantGain[formant];
        }
        const float shapedNoise = consonantFilter.process(
            parts.onset == "h" ? white : highNoise);
        const float consonantEnvelope = onsetDuration > 0.0f
            ? 1.0f - smoothstep(onsetDuration * 0.30f, onsetDuration, time) : 0.0f;
        float voice = oral * (0.32f + 0.68f * vowelBlend);
        const float strength = params.consonantStrength;

        if (kind == OnsetKind::Fricative || kind == OnsetKind::VoicedFricative) {
            const float frication = shapedNoise * consonantEnvelope * strength
                * (parts.onset == "h" ? 0.34f : 0.80f);
            voice += frication;
            if (kind == OnsetKind::VoicedFricative) voice += oral * consonantEnvelope * 0.28f;
        } else if (kind == OnsetKind::Stop || kind == OnsetKind::VoicedStop) {
            const float burstAge = time - burstTime;
            const float burst = burstAge >= 0.0f && burstAge < 0.055f
                ? smoothstep(0.0f, 0.0025f, burstAge)
                    * std::exp(-burstAge * (72.0f + params.articulation * 85.0f))
                : 0.0f;
            voice *= time < burstTime ? (kind == OnsetKind::VoicedStop ? 0.16f : 0.025f) : 1.0f;
            voice += shapedNoise * burst * strength * (kind == OnsetKind::VoicedStop ? 0.48f : 0.86f);
        } else if (kind == OnsetKind::Nasal) {
            const float nasal = nasalFilter.process(excitation);
            voice = voice * (0.38f + 0.62f * vowelBlend)
                + nasal * consonantEnvelope * strength * 1.25f;
        } else if (kind == OnsetKind::Liquid) {
            voice *= 0.72f + 0.28f * vowelBlend;
        }

        const float attack = smoothstep(0.0f, plainVowel ? 0.018f : 0.010f, time);
        const float release = 1.0f - smoothstep(0.86f, 1.0f, progress);
        const float amplitudeDrift = 1.0f + params.variation * 0.08f
            * std::sin(2.0f * kPi * 0.73f * time + aliasVariation * 3.0f);
        float sample = voice * attack * release * amplitudeDrift;
        const float dcBlocked = sample - dcX + 0.995f * dcY;
        dcX = sample;
        dcY = dcBlocked;
        output[i] = dcBlocked;
    }

    normalizeGenerated(output, sampleRate);
    return output;
}

inline VoxGeneratedVoicebank voxSourceGenerateBank(VoxSourceSynthParams params,
                                                    VoxSourceVocabulary vocabulary)
{
    VoxGeneratedVoicebank bank;
    bank.sampleRate = std::clamp(params.sampleRate, 8000, 192000);
    params.sampleRate = bank.sampleRate;
    const auto& aliases = voxSourceAliases(vocabulary);
    bank.entries.reserve(aliases.size());
    for (size_t i = 0u; i < aliases.size(); ++i) {
        VoxGeneratedVoiceSample entry;
        entry.alias = aliases[i];
        entry.samples = voxSourceGenerateAlias(entry.alias, params, static_cast<uint32_t>(i));
        bank.entries.push_back(std::move(entry));
    }
    return bank;
}

} // namespace s3g
