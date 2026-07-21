#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace s3g {

struct VoxBuilderSettings {
    float thresholdDb = -36.0f;
    float minimumGapMs = 60.0f;
    float edgePaddingMs = 25.0f;
};

enum class VoxBuilderAliasAssignment : uint8_t {
    Unknown,
    Ordered,
    Filename,
    Acoustic,
    Manual,
};

struct VoxBuilderAcousticFeatures {
    float formant1Hz = 0.0f;
    float formant2Hz = 0.0f;
    float vowelConfidence = 0.0f;
    float onsetRmsRatio = 0.0f;
    float onsetPeriodicity = 0.0f;
    float onsetHighness = 0.0f;
    float onsetTransient = 0.0f;
};

struct VoxBuilderAliasGuess {
    std::string alias;
    float confidence = 0.0f;
    float matchCost = std::numeric_limits<float>::max();
};

struct VoxBuilderSegment {
    std::string alias;
    uint64_t startSample = 0u;
    uint64_t endSample = 1u;
    float fixedMs = 60.0f;
    float preutterMs = 60.0f;
    float overlapMs = 21.0f;
    int baseMidi = 60;
    float voicedRatio = 0.0f;
    float aliasConfidence = 1.0f;
    VoxBuilderAliasAssignment aliasAssignment = VoxBuilderAliasAssignment::Unknown;
    VoxBuilderAcousticFeatures acoustic;
    float normalizationGainDb = 0.0f;
    float sourcePeakDb = -120.0f;
    float sourceClippedRatio = 0.0f;
    bool normalizationLimited = false;
};

struct VoxBuilderLevelReport {
    bool usable = false;
    float sourcePeakDb = -120.0f;
    float activeRmsDb = -120.0f;
    float normalizationGainDb = 0.0f;
    float outputPeakDb = -120.0f;
    float dcOffset = 0.0f;
    float clippedRatio = 0.0f;
    bool boostLimited = false;
};

inline float voxBuilderAmplitudeToDb(float amplitude)
{
    return 20.0f * std::log10(std::max(0.000001f, amplitude));
}

inline std::vector<std::string> voxBuilderParseAliases(const std::string& text)
{
    std::vector<std::string> aliases;
    std::string token;
    const auto finishToken = [&]() {
        if (!token.empty()) {
            aliases.push_back(token);
            token.clear();
        }
    };
    for (const unsigned char ch : text) {
        if (std::isspace(ch) || ch == ',' || ch == ';' || ch == '|') {
            finishToken();
        } else {
            token.push_back(static_cast<char>(ch));
        }
    }
    finishToken();
    return aliases;
}

inline const std::vector<std::string>& voxBuilderCoreAliases()
{
    static const std::vector<std::string> aliases = voxBuilderParseAliases(
        "a e i o u "
        "ka ke ki ko ku "
        "sa se si so su "
        "ta te ti to tu "
        "na ne ni no nu "
        "ma me mi mo mu "
        "ra re ri ro ru");
    return aliases;
}

// These are the complete CV tokens emitted by Ambi Vox's built-in English
// phrase mapper. A bank may still define any custom alias and address it
// directly or through s3g-pronunciations.txt.
inline const std::vector<std::string>& voxBuilderAutoPhraseAliases()
{
    static const std::vector<std::string> aliases = [] {
        constexpr std::array<const char*, 5> vowels { "a", "e", "i", "o", "u" };
        constexpr std::array<const char*, 17> onsets {
            "b", "ch", "d", "f", "g", "h", "j", "k", "m", "n", "p",
            "r", "s", "sh", "t", "w", "z"
        };
        std::vector<std::string> result;
        result.reserve(vowels.size() + onsets.size() * vowels.size() + 2u);
        for (const char* vowel : vowels) result.emplace_back(vowel);
        for (const char* onset : onsets) {
            for (const char* vowel : vowels) result.emplace_back(std::string(onset) + vowel);
        }
        result.emplace_back("n");
        result.emplace_back("ya");
        return result;
    }();
    return aliases;
}

inline const char* voxBuilderAliasAssignmentName(VoxBuilderAliasAssignment assignment)
{
    switch (assignment) {
    case VoxBuilderAliasAssignment::Ordered: return "order";
    case VoxBuilderAliasAssignment::Filename: return "filename";
    case VoxBuilderAliasAssignment::Acoustic: return "acoustic";
    case VoxBuilderAliasAssignment::Manual: return "manual";
    case VoxBuilderAliasAssignment::Unknown: break;
    }
    return "unknown";
}

inline std::string voxBuilderSafeStem(const std::string& text)
{
    std::string result;
    result.reserve(text.size());
    bool previousUnderscore = false;
    for (const unsigned char ch : text) {
        const bool keep = std::isalnum(ch) || ch == '-' || ch == '_';
        const char next = keep ? static_cast<char>(ch) : '_';
        if (next == '_' && previousUnderscore) continue;
        result.push_back(next);
        previousUnderscore = next == '_';
    }
    while (!result.empty() && result.front() == '_') result.erase(result.begin());
    while (!result.empty() && result.back() == '_') result.pop_back();
    return result.empty() ? std::string("voice") : result;
}

inline std::string voxBuilderNormalizedAlias(const std::string& text)
{
    std::string result;
    result.reserve(text.size());
    for (const unsigned char ch : text) {
        if (std::isalnum(ch)) result.push_back(static_cast<char>(std::tolower(ch)));
    }
    return result;
}

inline int voxBuilderAliasMatchIndex(const std::string& fileStem,
                                     const std::vector<std::string>& inventory)
{
    if (inventory.empty()) return -1;
    const std::string normalizedStem = voxBuilderNormalizedAlias(fileStem);
    for (size_t i = 0u; i < inventory.size(); ++i) {
        if (!normalizedStem.empty()
            && normalizedStem == voxBuilderNormalizedAlias(inventory[i])) {
            return static_cast<int>(i);
        }
    }

    std::vector<std::string> tokens;
    std::string token;
    const auto finishToken = [&]() {
        if (!token.empty()) {
            tokens.push_back(token);
            token.clear();
        }
    };
    for (const unsigned char ch : fileStem) {
        if (std::isalnum(ch)) token.push_back(static_cast<char>(std::tolower(ch)));
        else finishToken();
    }
    finishToken();
    for (const auto& candidate : tokens) {
        for (size_t i = 0u; i < inventory.size(); ++i) {
            if (candidate == voxBuilderNormalizedAlias(inventory[i])) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

inline bool voxBuilderVowelAlias(const std::string& alias)
{
    std::string key;
    for (const unsigned char ch : alias) {
        if (std::isalpha(ch)) key.push_back(static_cast<char>(std::tolower(ch)));
    }
    return key == "a" || key == "e" || key == "i" || key == "o"
        || key == "u" || key == "n" || key == "m";
}

inline void voxBuilderSetSuggestedTiming(VoxBuilderSegment& segment, int sampleRate)
{
    const double durationMs = sampleRate > 0
        ? static_cast<double>(segment.endSample - segment.startSample) * 1000.0
            / static_cast<double>(sampleRate)
        : 1.0;
    const double proportion = voxBuilderVowelAlias(segment.alias) ? 0.16 : 0.28;
    const double maximum = std::max(8.0, durationMs * 0.45);
    segment.fixedMs = static_cast<float>(std::clamp(durationMs * proportion, 18.0, maximum));
    segment.preutterMs = segment.fixedMs;
    segment.overlapMs = segment.fixedMs * 0.35f;
}

inline std::vector<float> voxBuilderRmsEnvelope(const std::vector<float>& samples,
                                                 uint32_t window,
                                                 uint32_t hop)
{
    if (samples.empty()) return {};
    window = std::max<uint32_t>(1u, window);
    hop = std::max<uint32_t>(1u, hop);
    std::vector<double> prefix(samples.size() + 1u, 0.0);
    for (size_t i = 0u; i < samples.size(); ++i) {
        const double value = static_cast<double>(samples[i]);
        prefix[i + 1u] = prefix[i] + value * value;
    }
    const size_t frameCount = std::max<size_t>(
        1u, (samples.size() + static_cast<size_t>(hop) - 1u) / static_cast<size_t>(hop));
    std::vector<float> envelope(frameCount, 0.0f);
    for (size_t frame = 0u; frame < frameCount; ++frame) {
        const size_t start = std::min(samples.size(), frame * static_cast<size_t>(hop));
        const size_t end = std::min(samples.size(), start + static_cast<size_t>(window));
        const double energy = prefix[end] - prefix[start];
        envelope[frame] = static_cast<float>(
            std::sqrt(std::max(0.0, energy) / static_cast<double>(std::max<size_t>(1u, end - start))));
    }
    return envelope;
}

inline VoxBuilderLevelReport voxBuilderConditionAudio(std::vector<float>& samples,
                                                       int sampleRate,
                                                       float targetActiveRmsDb = -18.0f,
                                                       float peakCeilingDb = -3.0f,
                                                       float maximumBoostDb = 24.0f)
{
    VoxBuilderLevelReport report {};
    if (samples.empty() || sampleRate <= 0) return report;

    double sum = 0.0;
    uint64_t clipped = 0u;
    for (const float sample : samples) {
        sum += sample;
        if (std::fabs(sample) >= 0.999f) ++clipped;
    }
    report.dcOffset = static_cast<float>(sum / static_cast<double>(samples.size()));
    report.clippedRatio = static_cast<float>(clipped) / static_cast<float>(samples.size());

    float peak = 0.0f;
    for (float& sample : samples) {
        sample = std::clamp(sample - report.dcOffset, -1.0f, 1.0f);
        peak = std::max(peak, std::fabs(sample));
    }
    report.sourcePeakDb = voxBuilderAmplitudeToDb(peak);
    if (peak < 0.0001f) return report;

    const uint32_t window = std::max<uint32_t>(64u,
        static_cast<uint32_t>(std::lround(static_cast<double>(sampleRate) * 0.020)));
    const uint32_t hop = std::max<uint32_t>(16u,
        static_cast<uint32_t>(std::lround(static_cast<double>(sampleRate) * 0.005)));
    auto envelope = voxBuilderRmsEnvelope(samples, window, hop);
    const float envelopePeak = envelope.empty()
        ? 0.0f : *std::max_element(envelope.begin(), envelope.end());
    const float activeThreshold = std::max(0.0001f, envelopePeak * 0.10f);
    std::vector<float> active;
    active.reserve(envelope.size());
    for (const float value : envelope) {
        if (value >= activeThreshold) active.push_back(value);
    }
    if (active.empty()) return report;
    const auto middle = active.begin() + static_cast<std::ptrdiff_t>(active.size() / 2u);
    std::nth_element(active.begin(), middle, active.end());
    const float activeRms = *middle;
    report.activeRmsDb = voxBuilderAmplitudeToDb(activeRms);
    if (activeRms < 0.0001f) return report;

    const float requestedGainDb = targetActiveRmsDb - report.activeRmsDb;
    const float limitedGainDb = std::min(requestedGainDb, maximumBoostDb);
    report.boostLimited = requestedGainDb > maximumBoostDb + 0.01f;
    float gain = std::pow(10.0f, limitedGainDb / 20.0f);
    const float peakCeiling = std::pow(10.0f, peakCeilingDb / 20.0f);
    gain = std::min(gain, peakCeiling / peak);
    report.normalizationGainDb = voxBuilderAmplitudeToDb(gain);
    for (float& sample : samples) sample = std::clamp(sample * gain, -1.0f, 1.0f);
    report.outputPeakDb = voxBuilderAmplitudeToDb(peak * gain);
    report.usable = true;
    return report;
}

inline uint64_t voxBuilderSnapToZeroCrossing(const std::vector<float>& samples,
                                             uint64_t targetSample,
                                             uint64_t searchRadius,
                                             uint64_t minimumSample = 1u,
                                             uint64_t maximumSample = std::numeric_limits<uint64_t>::max())
{
    if (samples.size() < 2u) return 0u;
    const uint64_t last = static_cast<uint64_t>(samples.size() - 1u);
    minimumSample = std::clamp<uint64_t>(minimumSample, 1u, last);
    maximumSample = std::clamp<uint64_t>(maximumSample, minimumSample, last);
    targetSample = std::clamp(targetSample, minimumSample, maximumSample);
    const uint64_t firstCandidate = std::max<uint64_t>(minimumSample,
        targetSample > searchRadius ? targetSample - searchRadius : minimumSample);
    const uint64_t lastCandidate = std::min<uint64_t>(maximumSample,
        targetSample > std::numeric_limits<uint64_t>::max() - searchRadius
            ? maximumSample : targetSample + searchRadius);

    uint64_t bestCrossing = targetSample;
    double bestDistance = std::numeric_limits<double>::max();
    float bestResidual = std::numeric_limits<float>::max();
    uint64_t quietestSample = targetSample;
    float quietestValue = std::fabs(samples[static_cast<size_t>(targetSample)]);
    for (uint64_t sample = firstCandidate; sample <= lastCandidate; ++sample) {
        const float right = samples[static_cast<size_t>(sample)];
        const float absolute = std::fabs(right);
        const uint64_t sampleDistance = sample > targetSample
            ? sample - targetSample : targetSample - sample;
        const uint64_t quietestDistance = quietestSample > targetSample
            ? quietestSample - targetSample : targetSample - quietestSample;
        if (absolute < quietestValue - 0.0000001f
            || (std::fabs(absolute - quietestValue) <= 0.0000001f
                && sampleDistance < quietestDistance)) {
            quietestValue = absolute;
            quietestSample = sample;
        }
        const float left = samples[static_cast<size_t>(sample - 1u)];
        const bool crosses = left == 0.0f || right == 0.0f
            || (left < 0.0f && right > 0.0f) || (left > 0.0f && right < 0.0f);
        if (!crosses) continue;
        const double denominator = static_cast<double>(std::fabs(left) + std::fabs(right));
        const double fraction = denominator > 0.000000001
            ? static_cast<double>(std::fabs(left)) / denominator : 1.0;
        const double crossing = static_cast<double>(sample - 1u) + fraction;
        const double distance = std::fabs(crossing - static_cast<double>(targetSample));
        const float residual = std::min(std::fabs(left), std::fabs(right));
        if (distance < bestDistance - 0.0000001
            || (std::fabs(distance - bestDistance) <= 0.0000001 && residual < bestResidual)) {
            bestDistance = distance;
            bestResidual = residual;
            bestCrossing = sample;
        }
    }
    return bestDistance < std::numeric_limits<double>::max() ? bestCrossing : quietestSample;
}

inline VoxBuilderAcousticFeatures voxBuilderAnalyzeAcoustics(
    const std::vector<float>& samples,
    int sampleRate,
    uint64_t startSample,
    uint64_t endSample)
{
    VoxBuilderAcousticFeatures features {};
    if (samples.empty() || sampleRate <= 0 || startSample >= samples.size()) return features;
    endSample = std::min<uint64_t>(endSample, samples.size());
    if (endSample <= startSample + 127u) return features;
    const size_t start = static_cast<size_t>(startSample);
    const size_t end = static_cast<size_t>(endSample);
    const size_t span = end - start;

    const auto rms = [&](size_t first, size_t last) {
        first = std::min(first, samples.size());
        last = std::clamp(last, first, samples.size());
        if (last <= first) return 0.0f;
        double energy = 0.0;
        for (size_t i = first; i < last; ++i) {
            const double value = samples[i];
            energy += value * value;
        }
        return static_cast<float>(std::sqrt(energy / static_cast<double>(last - first)));
    };

    const size_t onsetEnd = std::min(end, start + std::max<size_t>(64u,
        std::min<size_t>(span / 3u, static_cast<size_t>(sampleRate) * 70u / 1000u)));
    const size_t vowelStart = std::min(end - 1u, start + span * 42u / 100u);
    const size_t vowelEnd = std::max(vowelStart + 1u, start + span * 90u / 100u);
    const float onsetRms = rms(start, onsetEnd);
    const float vowelRms = rms(vowelStart, std::min(end, vowelEnd));
    features.onsetRmsRatio = std::clamp(onsetRms / std::max(0.000001f, vowelRms), 0.0f, 1.0f);

    double onsetMean = 0.0;
    for (size_t i = start; i < onsetEnd; ++i) onsetMean += samples[i];
    const size_t onsetCount = std::max<size_t>(1u, onsetEnd - start);
    onsetMean /= static_cast<double>(onsetCount);
    double onsetEnergy = 0.0;
    double differenceEnergy = 0.0;
    double onsetPeak = 0.0;
    double previous = static_cast<double>(samples[start]) - onsetMean;
    for (size_t i = start; i < onsetEnd; ++i) {
        const double value = static_cast<double>(samples[i]) - onsetMean;
        onsetEnergy += value * value;
        onsetPeak = std::max(onsetPeak, std::fabs(value));
        if (i > start) {
            const double difference = value - previous;
            differenceEnergy += difference * difference;
        }
        previous = value;
    }
    features.onsetHighness = static_cast<float>(std::clamp(
        differenceEnergy / std::max(0.000000001, onsetEnergy * 2.0), 0.0, 1.0));
    const double onsetCenteredRms = std::sqrt(onsetEnergy / static_cast<double>(onsetCount));
    features.onsetTransient = static_cast<float>(std::clamp(
        (onsetPeak / std::max(0.000001, onsetCenteredRms) - 1.5) / 6.0, 0.0, 1.0));

    const size_t minimumLag = std::max<size_t>(1u, static_cast<size_t>(sampleRate) / 500u);
    const size_t maximumLag = std::min<size_t>(onsetCount > 1u ? onsetCount - 1u : 0u,
        static_cast<size_t>(sampleRate) / 60u);
    double maximumCorrelation = 0.0;
    if (maximumLag >= minimumLag && onsetEnergy > 0.000000001) {
        for (size_t lag = minimumLag; lag <= maximumLag; ++lag) {
            double correlation = 0.0;
            double leftEnergy = 0.0;
            double rightEnergy = 0.0;
            for (size_t i = lag; i < onsetCount; ++i) {
                const double left = static_cast<double>(samples[start + i]) - onsetMean;
                const double right = static_cast<double>(samples[start + i - lag]) - onsetMean;
                correlation += left * right;
                leftEnergy += left * left;
                rightEnergy += right * right;
            }
            maximumCorrelation = std::max(maximumCorrelation,
                correlation / std::sqrt(std::max(0.000000001, leftEnergy * rightEnergy)));
        }
    }
    features.onsetPeriodicity = static_cast<float>(std::clamp(maximumCorrelation, 0.0, 1.0));

    const size_t frameLength = std::min<size_t>(span,
        std::clamp<size_t>(static_cast<size_t>(sampleRate) * 30u / 1000u, 256u, 2048u));
    const size_t lpcOrder = std::min<size_t>(frameLength > 2u ? frameLength - 2u : 0u,
        std::clamp<size_t>(12u + static_cast<size_t>(sampleRate) / 10000u, 12u, 20u));
    if (frameLength < 128u || lpcOrder < 4u) return features;
    std::vector<double> autocorrelation(lpcOrder + 1u, 0.0);
    constexpr size_t frameCount = 6u;
    constexpr double pi = 3.14159265358979323846;
    for (size_t frame = 0u; frame < frameCount; ++frame) {
        const double fraction = 0.42 + 0.09 * static_cast<double>(frame);
        const size_t available = span > frameLength ? span - frameLength : 0u;
        const size_t frameStart = start + std::min(available,
            static_cast<size_t>(std::lround(fraction * static_cast<double>(available))));
        std::vector<double> window(frameLength, 0.0);
        double mean = 0.0;
        for (size_t i = 0u; i < frameLength; ++i) mean += samples[frameStart + i];
        mean /= static_cast<double>(frameLength);
        double prior = static_cast<double>(samples[frameStart]) - mean;
        for (size_t i = 0u; i < frameLength; ++i) {
            const double centered = static_cast<double>(samples[frameStart + i]) - mean;
            const double emphasized = i == 0u ? centered : centered - 0.97 * prior;
            prior = centered;
            const double hann = 0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i)
                / static_cast<double>(frameLength - 1u));
            window[i] = emphasized * hann;
        }
        for (size_t lag = 0u; lag <= lpcOrder; ++lag) {
            double value = 0.0;
            for (size_t i = lag; i < frameLength; ++i) value += window[i] * window[i - lag];
            autocorrelation[lag] += value;
        }
    }
    if (autocorrelation[0] < 0.000000001) return features;

    std::vector<double> coefficients(lpcOrder + 1u, 0.0);
    std::vector<double> previousCoefficients(lpcOrder + 1u, 0.0);
    coefficients[0] = 1.0;
    double error = autocorrelation[0];
    for (size_t order = 1u; order <= lpcOrder; ++order) {
        double residual = autocorrelation[order];
        for (size_t i = 1u; i < order; ++i) {
            residual += coefficients[i] * autocorrelation[order - i];
        }
        const double reflection = std::clamp(-residual / std::max(0.000000001, error), -0.995, 0.995);
        previousCoefficients = coefficients;
        coefficients[order] = reflection;
        for (size_t i = 1u; i < order; ++i) {
            coefficients[i] = previousCoefficients[i] + reflection * previousCoefficients[order - i];
        }
        error *= std::max(0.0001, 1.0 - reflection * reflection);
    }

    constexpr float frequencyStep = 25.0f;
    const float highestFrequency = std::min(3500.0f, static_cast<float>(sampleRate) * 0.45f);
    const size_t frequencyCount = highestFrequency > 150.0f
        ? static_cast<size_t>((highestFrequency - 150.0f) / frequencyStep) + 1u : 0u;
    if (frequencyCount < 5u) return features;
    std::vector<double> response(frequencyCount, 0.0);
    for (size_t bin = 0u; bin < frequencyCount; ++bin) {
        const double frequency = 150.0 + static_cast<double>(bin) * frequencyStep;
        const double omega = 2.0 * pi * frequency / static_cast<double>(sampleRate);
        double real = 1.0;
        double imaginary = 0.0;
        for (size_t order = 1u; order <= lpcOrder; ++order) {
            real += coefficients[order] * std::cos(omega * static_cast<double>(order));
            imaginary -= coefficients[order] * std::sin(omega * static_cast<double>(order));
        }
        response[bin] = 1.0 / std::max(0.000000001, real * real + imaginary * imaginary);
    }
    for (size_t pass = 0u; pass < 2u; ++pass) {
        const auto source = response;
        for (size_t i = 1u; i + 1u < response.size(); ++i) {
            response[i] = (source[i - 1u] + source[i] * 2.0 + source[i + 1u]) * 0.25;
        }
    }
    const auto strongestPeak = [&](float minimumHz, float maximumHz) {
        float frequency = 0.0f;
        double strength = -1.0;
        for (size_t i = 1u; i + 1u < response.size(); ++i) {
            const float candidateHz = 150.0f + static_cast<float>(i) * frequencyStep;
            if (candidateHz < minimumHz || candidateHz > maximumHz) continue;
            if (response[i] >= response[i - 1u] && response[i] >= response[i + 1u]
                && response[i] > strength) {
                frequency = candidateHz;
                strength = response[i];
            }
        }
        if (frequency > 0.0f) return frequency;
        for (size_t i = 0u; i < response.size(); ++i) {
            const float candidateHz = 150.0f + static_cast<float>(i) * frequencyStep;
            if (candidateHz >= minimumHz && candidateHz <= maximumHz && response[i] > strength) {
                frequency = candidateHz;
                strength = response[i];
            }
        }
        return frequency;
    };
    features.formant1Hz = strongestPeak(200.0f, std::min(1100.0f, highestFrequency));
    features.formant2Hz = strongestPeak(
        std::max(700.0f, features.formant1Hz + 350.0f), highestFrequency);

    struct VowelPrototype { float first; float second; };
    constexpr std::array<VowelPrototype, 5> prototypes {{
        { 850.0f, 1300.0f }, { 400.0f, 1900.0f }, { 250.0f, 2350.0f },
        { 600.0f, 950.0f }, { 275.0f, 750.0f }
    }};
    std::array<float, 5> costs {};
    for (size_t i = 0u; i < prototypes.size(); ++i) {
        const float first = std::log(std::max(1.0f, features.formant1Hz)
            / prototypes[i].first) / 1.20f;
        const float second = std::log(std::max(1.0f, features.formant2Hz)
            / prototypes[i].second) / 0.28f;
        costs[i] = first * first + second * second;
    }
    std::sort(costs.begin(), costs.end());
    features.vowelConfidence = features.formant1Hz > 0.0f && features.formant2Hz > 0.0f
        ? std::clamp(0.22f + 0.78f * (1.0f - std::exp(-(costs[1] - costs[0]))), 0.22f, 0.96f)
        : 0.0f;
    return features;
}

inline std::vector<VoxBuilderAliasGuess> voxBuilderRankCoreAliases(
    const VoxBuilderAcousticFeatures& features)
{
    if (features.formant1Hz <= 0.0f || features.formant2Hz <= 0.0f) return {};
    constexpr std::array<const char*, 5> vowels { "a", "e", "i", "o", "u" };
    struct VowelPrototype { float first; float second; };
    constexpr std::array<VowelPrototype, 5> vowelPrototypes {{
        { 850.0f, 1300.0f }, { 400.0f, 1900.0f }, { 250.0f, 2350.0f },
        { 600.0f, 950.0f }, { 275.0f, 750.0f }
    }};
    std::array<float, 5> vowelCosts {};
    for (size_t i = 0u; i < vowelPrototypes.size(); ++i) {
        const float first = std::log(std::max(1.0f, features.formant1Hz)
            / vowelPrototypes[i].first) / 1.20f;
        const float second = std::log(std::max(1.0f, features.formant2Hz)
            / vowelPrototypes[i].second) / 0.28f;
        vowelCosts[i] = first * first + second * second;
    }

    struct OnsetPrototype {
        const char* text;
        float rmsRatio;
        float periodicity;
        float highness;
        float transient;
    };
    constexpr std::array<OnsetPrototype, 7> onsetPrototypes {{
        { "", 0.52f, 0.90f, 0.00f, 0.42f },
        { "k", 0.16f, 0.32f, 0.58f, 0.58f },
        { "s", 0.30f, 0.18f, 0.90f, 0.52f },
        { "t", 0.16f, 0.32f, 0.72f, 0.58f },
        { "n", 0.35f, 0.72f, 0.10f, 0.35f },
        { "m", 0.24f, 0.76f, 0.00f, 0.60f },
        { "r", 0.27f, 0.76f, 0.00f, 0.44f },
    }};
    std::array<float, 7> onsetCosts {};
    for (size_t i = 0u; i < onsetPrototypes.size(); ++i) {
        const float rmsDifference = (features.onsetRmsRatio - onsetPrototypes[i].rmsRatio) / 0.26f;
        const float periodicDifference = (features.onsetPeriodicity - onsetPrototypes[i].periodicity) / 0.24f;
        const float highDifference = (features.onsetHighness - onsetPrototypes[i].highness) / 0.25f;
        const float transientDifference = (features.onsetTransient - onsetPrototypes[i].transient) / 0.28f;
        onsetCosts[i] = rmsDifference * rmsDifference + periodicDifference * periodicDifference
            + highDifference * highDifference + transientDifference * transientDifference;
    }
    auto orderedOnsetCosts = onsetCosts;
    std::sort(orderedOnsetCosts.begin(), orderedOnsetCosts.end());
    const float bestOnsetCost = orderedOnsetCosts[0u];
    const float secondOnsetCost = orderedOnsetCosts[1u];
    const float onsetConfidence = std::clamp(
        0.18f + 0.82f * (1.0f - std::exp(-(secondOnsetCost - bestOnsetCost) * 0.35f)),
        0.18f, 0.92f);
    const float bestConfidence = std::clamp(0.24f + 0.64f
        * std::sqrt(std::max(0.0f, features.vowelConfidence * onsetConfidence)), 0.24f, 0.88f);
    std::vector<VoxBuilderAliasGuess> guesses;
    guesses.reserve(onsetPrototypes.size() * vowels.size());
    for (size_t onset = 0u; onset < onsetPrototypes.size(); ++onset) {
        for (size_t vowel = 0u; vowel < vowels.size(); ++vowel) {
            VoxBuilderAliasGuess guess;
            guess.alias = std::string(onsetPrototypes[onset].text) + vowels[vowel];
            guess.matchCost = onsetCosts[onset] + vowelCosts[vowel];
            guesses.push_back(std::move(guess));
        }
    }
    std::sort(guesses.begin(), guesses.end(), [](const auto& left, const auto& right) {
        if (std::fabs(left.matchCost - right.matchCost) > 0.000001f) {
            return left.matchCost < right.matchCost;
        }
        return left.alias < right.alias;
    });
    const float bestCost = guesses.front().matchCost;
    for (auto& guess : guesses) {
        guess.confidence = std::clamp(bestConfidence
            * std::exp(-(guess.matchCost - bestCost) * 0.20f), 0.03f, bestConfidence);
    }
    return guesses;
}

inline VoxBuilderAliasGuess voxBuilderGuessCoreAlias(const VoxBuilderAcousticFeatures& features)
{
    auto guesses = voxBuilderRankCoreAliases(features);
    return guesses.empty() ? VoxBuilderAliasGuess {} : std::move(guesses.front());
}

inline std::vector<VoxBuilderAliasGuess> voxBuilderChooseUniqueAliases(
    const std::vector<std::vector<VoxBuilderAliasGuess>>& rankedCandidates,
    const std::vector<std::string>& reservedAliases = {})
{
    std::unordered_set<std::string> used;
    for (const auto& alias : reservedAliases) used.insert(voxBuilderNormalizedAlias(alias));
    std::vector<size_t> order;
    order.reserve(rankedCandidates.size());
    for (size_t i = 0u; i < rankedCandidates.size(); ++i) {
        if (!rankedCandidates[i].empty()) order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](size_t left, size_t right) {
        return rankedCandidates[left].front().confidence
            > rankedCandidates[right].front().confidence;
    });
    std::vector<VoxBuilderAliasGuess> result(rankedCandidates.size());
    for (const size_t index : order) {
        const auto match = std::find_if(rankedCandidates[index].begin(),
            rankedCandidates[index].end(), [&](const auto& candidate) {
                const std::string key = voxBuilderNormalizedAlias(candidate.alias);
                return !key.empty() && used.count(key) == 0u;
            });
        if (match == rankedCandidates[index].end()) continue;
        result[index] = *match;
        used.insert(voxBuilderNormalizedAlias(match->alias));
    }
    return result;
}

inline std::vector<VoxBuilderSegment> voxBuilderDetectSegments(
    const std::vector<float>& samples,
    int sampleRate,
    const std::vector<std::string>& aliases,
    VoxBuilderSettings settings = {})
{
    std::vector<VoxBuilderSegment> segments;
    if (samples.empty() || sampleRate <= 0 || aliases.empty()) return segments;

    const uint32_t window = std::max<uint32_t>(64u,
        static_cast<uint32_t>(std::lround(static_cast<double>(sampleRate) * 0.020)));
    const uint32_t hop = std::max<uint32_t>(16u,
        static_cast<uint32_t>(std::lround(static_cast<double>(sampleRate) * 0.005)));
    const auto envelope = voxBuilderRmsEnvelope(samples, window, hop);
    const float peak = *std::max_element(envelope.begin(), envelope.end());
    const float threshold = peak * std::pow(10.0f,
        std::clamp(settings.thresholdDb, -90.0f, -1.0f) / 20.0f);
    const uint32_t gapFrames = std::max<uint32_t>(1u,
        static_cast<uint32_t>(std::lround(settings.minimumGapMs * 0.001f
            * static_cast<float>(sampleRate) / static_cast<float>(hop))));
    const uint64_t padding = static_cast<uint64_t>(std::max(0.0f,
        settings.edgePaddingMs) * 0.001f * static_cast<float>(sampleRate));

    std::vector<std::pair<size_t, size_t>> activeRegions;
    size_t regionStart = 0u;
    size_t lastActive = 0u;
    bool inRegion = false;
    for (size_t frame = 0u; frame < envelope.size(); ++frame) {
        if (envelope[frame] >= threshold && peak > 0.000001f) {
            if (!inRegion) regionStart = frame;
            inRegion = true;
            lastActive = frame;
        } else if (inRegion && frame > lastActive + gapFrames) {
            activeRegions.emplace_back(regionStart, lastActive);
            inRegion = false;
        }
    }
    if (inRegion) activeRegions.emplace_back(regionStart, lastActive);

    uint64_t activeStart = 0u;
    uint64_t activeEnd = samples.size();
    if (!activeRegions.empty()) {
        activeStart = static_cast<uint64_t>(activeRegions.front().first) * hop;
        activeStart = activeStart > padding ? activeStart - padding : 0u;
        activeEnd = std::min<uint64_t>(samples.size(),
            static_cast<uint64_t>(activeRegions.back().second) * hop + window + padding);
    }
    if (activeEnd <= activeStart) {
        activeStart = 0u;
        activeEnd = samples.size();
    }

    const size_t count = aliases.size();
    std::vector<uint64_t> boundaries(count + 1u, activeStart);
    boundaries.front() = activeStart;
    boundaries.back() = activeEnd;
    const uint64_t minimumSpan = std::max<uint64_t>(1u,
        static_cast<uint64_t>(sampleRate) / 100u);
    if (activeRegions.size() == count) {
        for (size_t i = 1u; i < count; ++i) {
            const uint64_t previousEnd = static_cast<uint64_t>(activeRegions[i - 1u].second) * hop + window;
            const uint64_t nextStart = static_cast<uint64_t>(activeRegions[i].first) * hop;
            boundaries[i] = std::clamp<uint64_t>(
                (previousEnd + nextStart) / 2u, boundaries[i - 1u] + 1u, activeEnd - 1u);
        }
    } else if (count > 1u) {
        const uint64_t span = activeEnd - activeStart;
        for (size_t i = 1u; i < count; ++i) {
            const uint64_t expected = activeStart + span * i / count;
            const uint64_t radius = std::max<uint64_t>(minimumSpan, span / count * 2u / 5u);
            const uint64_t lowerLimit = boundaries[i - 1u] + minimumSpan;
            const uint64_t upperLimit = activeEnd > minimumSpan * (count - i)
                ? activeEnd - minimumSpan * (count - i) : activeEnd - 1u;
            const uint64_t searchStart = std::max<uint64_t>(lowerLimit,
                expected > radius ? expected - radius : activeStart);
            const uint64_t searchEnd = std::min<uint64_t>(upperLimit, expected + radius);
            size_t firstFrame = std::min(envelope.size() - 1u,
                static_cast<size_t>(searchStart / hop));
            size_t lastFrame = std::min(envelope.size() - 1u,
                static_cast<size_t>(searchEnd / hop));
            if (lastFrame < firstFrame) std::swap(firstFrame, lastFrame);
            size_t bestFrame = firstFrame;
            float bestValue = std::numeric_limits<float>::max();
            uint64_t bestDistance = std::numeric_limits<uint64_t>::max();
            for (size_t frame = firstFrame; frame <= lastFrame; ++frame) {
                const uint64_t sample = static_cast<uint64_t>(frame) * hop;
                const uint64_t distance = sample > expected ? sample - expected : expected - sample;
                if (envelope[frame] < bestValue - 0.000001f
                    || (std::fabs(envelope[frame] - bestValue) <= 0.000001f
                        && distance < bestDistance)) {
                    bestValue = envelope[frame];
                    bestDistance = distance;
                    bestFrame = frame;
                }
            }
            boundaries[i] = std::clamp<uint64_t>(
                static_cast<uint64_t>(bestFrame) * hop, lowerLimit, upperLimit);
        }
    }
    const uint64_t zeroCrossingRadius = std::max<uint64_t>(1u,
        static_cast<uint64_t>(sampleRate) / 100u);
    for (size_t i = 1u; i < count; ++i) {
        const uint64_t lower = boundaries[i - 1u] + minimumSpan;
        const uint64_t remaining = minimumSpan * static_cast<uint64_t>(count - i);
        const uint64_t upper = activeEnd > remaining ? activeEnd - remaining : lower;
        if (upper < lower) continue;
        boundaries[i] = voxBuilderSnapToZeroCrossing(
            samples, boundaries[i], zeroCrossingRadius, lower, upper);
    }

    segments.reserve(count);
    const float aliasConfidence = activeRegions.size() == count ? 0.95f : 0.60f;
    for (size_t i = 0u; i < count; ++i) {
        VoxBuilderSegment segment {};
        segment.alias = aliases[i];
        segment.startSample = std::min<uint64_t>(boundaries[i], samples.size() - 1u);
        segment.endSample = std::clamp<uint64_t>(boundaries[i + 1u],
            segment.startSample + 1u, samples.size());
        segment.aliasConfidence = aliasConfidence;
        segment.aliasAssignment = VoxBuilderAliasAssignment::Ordered;
        voxBuilderSetSuggestedTiming(segment, sampleRate);
        segments.push_back(std::move(segment));
    }
    return segments;
}

inline bool voxBuilderMoveBoundary(std::vector<VoxBuilderSegment>& segments,
                                   size_t boundaryIndex,
                                   uint64_t sample,
                                   uint64_t minimumSpan = 1u)
{
    if (boundaryIndex == 0u || boundaryIndex >= segments.size()) return false;
    minimumSpan = std::max<uint64_t>(1u, minimumSpan);
    const uint64_t lower = segments[boundaryIndex - 1u].startSample + minimumSpan;
    const uint64_t upper = segments[boundaryIndex].endSample > minimumSpan
        ? segments[boundaryIndex].endSample - minimumSpan : lower;
    if (upper < lower) return false;
    const uint64_t boundary = std::clamp(sample, lower, upper);
    segments[boundaryIndex - 1u].endSample = boundary;
    segments[boundaryIndex].startSample = boundary;
    return true;
}

} // namespace s3g
