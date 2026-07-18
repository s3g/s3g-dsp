#pragma once

#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace s3g {

constexpr uint32_t kAmbiVotMaxVoices = 64;
constexpr uint32_t kAmbiVotMaxOrder = 7;
constexpr uint32_t kAmbiVotMaxChannels = 64;
constexpr uint32_t kAmbiVotTableCount = 16;
constexpr uint32_t kAmbiVotTableSize = 256;
constexpr uint32_t kAmbiVotGridSize = 4;
constexpr uint32_t kAmbiVotAtlasSampleCount = kAmbiVotTableCount * kAmbiVotTableSize;
constexpr uint32_t kAmbiVotBandCount = 8;
constexpr uint32_t kAmbiVotMaxScoreNodes = 16;

constexpr std::array<uint32_t, kAmbiVotBandCount> kAmbiVotBandHarmonics {{
    1u, 2u, 4u, 8u, 16u, 32u, 64u, (kAmbiVotTableSize / 2u) - 1u,
}};

enum class AmbiVotMode : uint32_t {
    Free = 0,
    Midi = 1,
    Both = 2,
};

enum class AmbiVotPreset : uint32_t {
    Sine = 0,
    Classic = 1,
    Digital = 2,
    Formant = 3,
    User = 4,
};

enum class AmbiVotMotionScene : uint32_t {
    Manual = 0,
    Orbit = 1,
    Flow = 2,
    Path = 3,
    Pulse = 4,
};

enum class AmbiVotMotionClock : uint32_t {
    Free = 0,
    Sync = 1,
};

enum class AmbiVotScale : uint32_t {
    Chromatic = 0,
    Major = 1,
    Minor = 2,
    Pentatonic = 3,
    WholeTone = 4,
    HarmonicMinor = 5,
};

enum class AmbiVotScoreMode : uint32_t {
    Off = 0,
    OneShot = 1,
    Loop = 2,
    PingPong = 3,
};

enum class AmbiVotScoreCurve : uint32_t {
    Linear = 0,
    Smooth = 1,
    Exponential = 2,
    Hold = 3,
};

struct AmbiVotParams {
    uint32_t order = 3;
    uint32_t voices = 8;
    AmbiVotMode mode = AmbiVotMode::Free;
    AmbiVotPreset preset = AmbiVotPreset::Classic;
    float baseNote = 48.0f;
    float tuneCents = 0.0f;
    float vectorX = 0.20f;
    float vectorY = 0.55f;
    float scan = 0.30f;
    float scanRate = 1.0f;
    float morph = 1.0f;
    float detune = 0.10f;
    AmbiVotScale scale = AmbiVotScale::Chromatic;
    float pitchSpread = 1.0f;
    float harmonicAmount = 0.0f;
    float subharmonicAmount = 0.0f;
    float motionSpread = 0.65f;
    float motionRateHz = 0.045f;
    float attackMs = 35.0f;
    float decayMs = 220.0f;
    float sustain = 0.68f;
    float releaseMs = 650.0f;
    float outputGainDb = -18.0f;
    AmbiVotMotionScene motionScene = AmbiVotMotionScene::Flow;
    AmbiVotMotionClock motionClock = AmbiVotMotionClock::Free;
    float syncDivisionBeats = 8.0f;
    float motionAmount = 0.72f;
    float motionCoherence = 0.62f;
    float motionChaos = 0.18f;
    float motionLink = 0.72f;
    float motionSmooth = 0.72f;
    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    float neighborRadius = 0.90f;
    uint32_t requiredNeighbors = 1;
    AmbiVotScoreMode scoreMode = AmbiVotScoreMode::Loop;
    float scoreDurationSec = 8.0f;
    float scoreDepth = 1.0f;
};

using AmbiVotTable = std::array<float, kAmbiVotTableSize>;
using AmbiVotTableSet = std::array<AmbiVotTable, kAmbiVotTableCount>;

struct AmbiVotTableBank {
    AmbiVotTableSet tables {};
    std::array<AmbiVotTableSet, kAmbiVotBandCount> bandTables {};
    bool user = false;
    bool exactAtlas = false;
};

struct AmbiVotVectorWeights {
    std::array<float, kAmbiVotTableCount> values {};
    float sigmaCells = 0.0f;
};

struct AmbiVotBandSelection {
    uint32_t lower = 0u;
    uint32_t upper = 0u;
    float interpolation = 0.0f;
};

struct AmbiVotMotionPoint {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
    float u = 0.5f;
    float v = 0.5f;
};

struct AmbiVotScoreNode {
    float time = 0.0f;
    float u = 0.5f;
    float v = 0.5f;
    AmbiVotScoreCurve curve = AmbiVotScoreCurve::Smooth;
};

struct AmbiVotVectorScore {
    uint32_t nodeCount = 8;
    uint32_t sustainStart = 2;
    uint32_t sustainEnd = 6;
    std::array<AmbiVotScoreNode, kAmbiVotMaxScoreNodes> nodes {};
};

inline AmbiVotVectorScore ambiVotDefaultScore()
{
    AmbiVotVectorScore score {};
    constexpr std::array<std::array<float, 2>, 8> route {{
        {{ 0.125f, 0.125f }}, {{ 0.875f, 0.125f }},
        {{ 0.625f, 0.375f }}, {{ 0.375f, 0.375f }},
        {{ 0.125f, 0.625f }}, {{ 0.875f, 0.625f }},
        {{ 0.875f, 0.875f }}, {{ 0.125f, 0.875f }},
    }};
    for (uint32_t i = 0; i < score.nodeCount; ++i) {
        score.nodes[i].time = static_cast<float>(i) / static_cast<float>(score.nodeCount - 1u);
        score.nodes[i].u = route[i][0];
        score.nodes[i].v = route[i][1];
        score.nodes[i].curve = AmbiVotScoreCurve::Smooth;
    }
    for (uint32_t i = score.nodeCount; i < kAmbiVotMaxScoreNodes; ++i) {
        score.nodes[i].time = static_cast<float>(i) / static_cast<float>(kAmbiVotMaxScoreNodes - 1u);
        score.nodes[i].u = (static_cast<float>(i % kAmbiVotGridSize) + 0.5f)
            / static_cast<float>(kAmbiVotGridSize);
        score.nodes[i].v = (static_cast<float>(i / kAmbiVotGridSize) + 0.5f)
            / static_cast<float>(kAmbiVotGridSize);
        score.nodes[i].curve = AmbiVotScoreCurve::Smooth;
    }
    return score;
}

inline void ambiVotNormalizeScore(AmbiVotVectorScore& score)
{
    score.nodeCount = std::clamp<uint32_t>(score.nodeCount, 2u, kAmbiVotMaxScoreNodes);
    score.nodes[0].time = 0.0f;
    score.nodes[score.nodeCount - 1u].time = 1.0f;
    for (uint32_t i = 0; i < score.nodeCount; ++i) {
        score.nodes[i].u = clamp(score.nodes[i].u, 0.0f, 1.0f);
        score.nodes[i].v = clamp(score.nodes[i].v, 0.0f, 1.0f);
        score.nodes[i].curve = static_cast<AmbiVotScoreCurve>(std::clamp<uint32_t>(
            static_cast<uint32_t>(score.nodes[i].curve), 0u, 3u));
        if (i > 0u && i + 1u < score.nodeCount) {
            const float minimum = score.nodes[i - 1u].time + 0.01f;
            const float maximum = 1.0f - 0.01f * static_cast<float>(score.nodeCount - 1u - i);
            score.nodes[i].time = clamp(score.nodes[i].time, minimum, maximum);
        }
    }
    score.sustainStart = std::clamp<uint32_t>(score.sustainStart, 0u, score.nodeCount - 2u);
    score.sustainEnd = std::clamp<uint32_t>(score.sustainEnd, score.sustainStart + 1u, score.nodeCount - 1u);
}

inline float ambiVotScoreCurveValue(AmbiVotScoreCurve curve, float value)
{
    value = clamp(value, 0.0f, 1.0f);
    switch (curve) {
    case AmbiVotScoreCurve::Linear: return value;
    case AmbiVotScoreCurve::Exponential: return value * value;
    case AmbiVotScoreCurve::Hold: return value >= 1.0f ? 1.0f : 0.0f;
    case AmbiVotScoreCurve::Smooth:
    default: return 0.5f - 0.5f * std::cos(kPi * value);
    }
}

inline std::array<float, 2> ambiVotScorePoint(const AmbiVotVectorScore& score, float phase)
{
    const uint32_t count = std::clamp<uint32_t>(score.nodeCount, 2u, kAmbiVotMaxScoreNodes);
    phase = clamp(phase, 0.0f, 1.0f);
    uint32_t segment = count - 2u;
    for (uint32_t i = 0; i + 1u < count; ++i) {
        if (phase <= score.nodes[i + 1u].time) {
            segment = i;
            break;
        }
    }
    const auto& from = score.nodes[segment];
    const auto& to = score.nodes[segment + 1u];
    const float local = (phase - from.time) / std::max(0.0001f, to.time - from.time);
    const float interpolation = ambiVotScoreCurveValue(from.curve, local);
    return {{ lerp(from.u, to.u, interpolation), lerp(from.v, to.v, interpolation) }};
}

enum class AmbiVotEnvelopeStage : uint8_t {
    Idle,
    Attack,
    Decay,
    Sustain,
    Release,
};

inline float ambiVotEnvelopeCoefficient(float timeMs, double sampleRate)
{
    const float samples = std::max(1.0f, timeMs * 0.001f * static_cast<float>(sampleRate));
    return 1.0f - std::exp(-6.90775528f / samples);
}

struct AmbiVotEnvelope {
    AmbiVotEnvelopeStage stage = AmbiVotEnvelopeStage::Idle;
    float value = 0.0f;
    bool gate = false;

    void trigger()
    {
        gate = true;
        stage = AmbiVotEnvelopeStage::Attack;
    }

    void releaseGate()
    {
        gate = false;
        stage = value > 0.00001f ? AmbiVotEnvelopeStage::Release : AmbiVotEnvelopeStage::Idle;
    }

    void setGate(bool nextGate)
    {
        if (nextGate == gate) return;
        if (nextGate) trigger();
        else releaseGate();
    }

    float process(float attackCoefficient,
                  float decayCoefficient,
                  float sustainLevel,
                  float releaseCoefficient)
    {
        sustainLevel = clamp(sustainLevel, 0.0f, 1.0f);
        switch (stage) {
        case AmbiVotEnvelopeStage::Attack:
            value += attackCoefficient * (1.0f - value);
            if (value >= 0.999f) {
                value = 1.0f;
                stage = AmbiVotEnvelopeStage::Decay;
            }
            break;
        case AmbiVotEnvelopeStage::Decay:
            value += decayCoefficient * (sustainLevel - value);
            if (std::fabs(value - sustainLevel) <= 0.001f) {
                value = sustainLevel;
                stage = AmbiVotEnvelopeStage::Sustain;
            }
            break;
        case AmbiVotEnvelopeStage::Sustain:
            value += decayCoefficient * (sustainLevel - value);
            break;
        case AmbiVotEnvelopeStage::Release:
            value += releaseCoefficient * -value;
            if (value <= 0.00001f) {
                value = 0.0f;
                stage = AmbiVotEnvelopeStage::Idle;
            }
            break;
        case AmbiVotEnvelopeStage::Idle:
        default:
            value = 0.0f;
            break;
        }
        value = flushDenormal(value);
        return value;
    }
};

inline float ambiVotMidiToHz(float note)
{
    return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

inline int ambiVotScaleDegreeSemitones(AmbiVotScale scale, int degree)
{
    static constexpr std::array<int, 12> chromatic {{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }};
    static constexpr std::array<int, 7> major {{ 0, 2, 4, 5, 7, 9, 11 }};
    static constexpr std::array<int, 7> minor {{ 0, 2, 3, 5, 7, 8, 10 }};
    static constexpr std::array<int, 5> pentatonic {{ 0, 2, 4, 7, 9 }};
    static constexpr std::array<int, 6> wholeTone {{ 0, 2, 4, 6, 8, 10 }};
    static constexpr std::array<int, 7> harmonicMinor {{ 0, 2, 3, 5, 7, 8, 11 }};

    const int* values = chromatic.data();
    int count = static_cast<int>(chromatic.size());
    switch (scale) {
    case AmbiVotScale::Major: values = major.data(); count = static_cast<int>(major.size()); break;
    case AmbiVotScale::Minor: values = minor.data(); count = static_cast<int>(minor.size()); break;
    case AmbiVotScale::Pentatonic: values = pentatonic.data(); count = static_cast<int>(pentatonic.size()); break;
    case AmbiVotScale::WholeTone: values = wholeTone.data(); count = static_cast<int>(wholeTone.size()); break;
    case AmbiVotScale::HarmonicMinor: values = harmonicMinor.data(); count = static_cast<int>(harmonicMinor.size()); break;
    case AmbiVotScale::Chromatic:
    default: break;
    }

    int octave = degree / count;
    int index = degree % count;
    if (index < 0) {
        index += count;
        --octave;
    }
    return octave * 12 + values[index];
}

inline int ambiVotScaleSize(AmbiVotScale scale)
{
    switch (scale) {
    case AmbiVotScale::Pentatonic: return 5;
    case AmbiVotScale::WholeTone: return 6;
    case AmbiVotScale::Major:
    case AmbiVotScale::Minor:
    case AmbiVotScale::HarmonicMinor: return 7;
    case AmbiVotScale::Chromatic:
    default: return 12;
    }
}

inline float ambiVotQuantizeToScale(float note, float root, AmbiVotScale scale, int* degreeOut = nullptr)
{
    const float relative = note - root;
    const int scaleSize = ambiVotScaleSize(scale);
    const int approximateOctave = static_cast<int>(std::floor(relative / 12.0f));
    float bestNote = root;
    float bestDistance = 100000.0f;
    int bestDegree = 0;
    for (int octave = approximateOctave - 1; octave <= approximateOctave + 1; ++octave) {
        for (int index = 0; index < scaleSize; ++index) {
            const int degree = octave * scaleSize + index;
            const float candidate = root + static_cast<float>(ambiVotScaleDegreeSemitones(scale, degree));
            const float distance = std::fabs(candidate - note);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestNote = candidate;
                bestDegree = degree;
            }
        }
    }
    if (degreeOut) *degreeOut = bestDegree;
    return bestNote;
}

inline float ambiVotPitchDeviation(uint32_t voice, int note, float amount)
{
    const float seed = static_cast<float>((voice + 1u) * 131u + static_cast<uint32_t>(std::max(0, note + 128)) * 17u);
    const float raw = std::sin(seed * 0.01745329252f) * 43758.5453f;
    const float random = (raw - std::floor(raw)) * 2.0f - 1.0f;
    return random * clamp(amount, 0.0f, 1.0f) * 100.0f;
}

inline float ambiVotTunedNote(const AmbiVotParams& params,
                              float sourceNote,
                              uint32_t voice,
                              bool midiVoice)
{
    int relationshipDegree = 0;
    float interval = 0.0f;
    if (midiVoice) {
        const float quantized = ambiVotQuantizeToScale(sourceNote, params.baseNote, params.scale, &relationshipDegree);
        interval = (quantized - params.baseNote) * params.pitchSpread;
    } else {
        relationshipDegree = static_cast<int>(voice) - static_cast<int>(params.voices / 2u);
        interval = static_cast<float>(ambiVotScaleDegreeSemitones(params.scale, relationshipDegree))
            * params.pitchSpread;
    }

    const uint32_t relationship = 1u + static_cast<uint32_t>(std::abs(relationshipDegree)) % 8u;
    const float harmonicInterval = 12.0f * std::log2(static_cast<float>(relationship));
    if (relationshipDegree > 0) interval = lerp(interval, harmonicInterval, params.harmonicAmount);
    else if (relationshipDegree < 0) interval = lerp(interval, -harmonicInterval, params.subharmonicAmount);
    const float cents = params.tuneCents
        + ambiVotPitchDeviation(voice, static_cast<int>(std::lround(sourceNote)), params.detune);
    return clamp(params.baseNote + interval + cents / 100.0f, -24.0f, 132.0f);
}

inline float ambiVotFract(float x)
{
    return x - std::floor(x);
}

inline float ambiVotWrapSignedDeg(float value)
{
    while (value > 180.0f) value -= 360.0f;
    while (value <= -180.0f) value += 360.0f;
    return value;
}

inline float ambiVotTriangle(float phase)
{
    return 4.0f * std::fabs(phase - 0.5f) - 1.0f;
}

inline float ambiVotSaw(float phase)
{
    return phase * 2.0f - 1.0f;
}

inline float ambiVotPulse(float phase, float width)
{
    return phase < width ? 1.0f : -1.0f;
}

struct AmbiVotDftBasis {
    std::array<AmbiVotTable, kAmbiVotTableSize / 2u> cosine {};
    std::array<AmbiVotTable, kAmbiVotTableSize / 2u> sine {};

    AmbiVotDftBasis()
    {
        for (uint32_t harmonic = 0; harmonic < kAmbiVotTableSize / 2u; ++harmonic) {
            for (uint32_t sample = 0; sample < kAmbiVotTableSize; ++sample) {
                const float angle = 2.0f * kPi * static_cast<float>(harmonic * sample)
                    / static_cast<float>(kAmbiVotTableSize);
                cosine[harmonic][sample] = std::cos(angle);
                sine[harmonic][sample] = std::sin(angle);
            }
        }
    }
};

inline const AmbiVotDftBasis& ambiVotDftBasis()
{
    static const AmbiVotDftBasis basis;
    return basis;
}

inline void ambiVotBuildBandTables(AmbiVotTableBank& bank)
{
    const auto& basis = ambiVotDftBasis();
    constexpr uint32_t harmonicCount = kAmbiVotTableSize / 2u;
    constexpr float coefficientScale = 2.0f / static_cast<float>(kAmbiVotTableSize);
    for (uint32_t table = 0; table < kAmbiVotTableCount; ++table) {
        std::array<float, harmonicCount> cosineCoefficients {};
        std::array<float, harmonicCount> sineCoefficients {};
        for (uint32_t harmonic = 1; harmonic < harmonicCount; ++harmonic) {
            float cosineSum = 0.0f;
            float sineSum = 0.0f;
            for (uint32_t sample = 0; sample < kAmbiVotTableSize; ++sample) {
                cosineSum += bank.tables[table][sample] * basis.cosine[harmonic][sample];
                sineSum += bank.tables[table][sample] * basis.sine[harmonic][sample];
            }
            cosineCoefficients[harmonic] = cosineSum * coefficientScale;
            sineCoefficients[harmonic] = sineSum * coefficientScale;
        }

        for (uint32_t band = 0; band + 1u < kAmbiVotBandCount; ++band) {
            const uint32_t maximumHarmonic = kAmbiVotBandHarmonics[band];
            for (uint32_t sample = 0; sample < kAmbiVotTableSize; ++sample) {
                float value = 0.0f;
                for (uint32_t harmonic = 1; harmonic <= maximumHarmonic; ++harmonic) {
                    value += cosineCoefficients[harmonic] * basis.cosine[harmonic][sample]
                        + sineCoefficients[harmonic] * basis.sine[harmonic][sample];
                }
                bank.bandTables[band][table][sample] = value;
            }
        }
        bank.bandTables[kAmbiVotBandCount - 1u][table] = bank.tables[table];
    }
}

inline void ambiVotNormalizeBank(AmbiVotTableBank& bank)
{
    for (auto& table : bank.tables) {
        float mean = 0.0f;
        for (float value : table) mean += value;
        mean /= static_cast<float>(kAmbiVotTableSize);
        for (float& value : table) value -= mean;
    }
    float peak = 0.000001f;
    for (const auto& table : bank.tables) {
        for (float v : table) peak = std::max(peak, std::fabs(v));
    }
    const float gain = peak > 0.0f ? 0.95f / peak : 1.0f;
    for (auto& table : bank.tables) {
        for (float& v : table) v *= gain;
    }
}

inline AmbiVotTableBank ambiVotPresetBank(AmbiVotPreset preset)
{
    AmbiVotTableBank bank {};
    for (uint32_t t = 0; t < kAmbiVotTableCount; ++t) {
        const float u = static_cast<float>(t) / static_cast<float>(kAmbiVotTableCount - 1u);
        for (uint32_t i = 0; i < kAmbiVotTableSize; ++i) {
            const float ph = static_cast<float>(i) / static_cast<float>(kAmbiVotTableSize);
            const float sine = std::sin(2.0f * kPi * ph);
            const float tri = ambiVotTriangle(ph);
            const float saw = ambiVotSaw(ph);
            const float pulse = ambiVotPulse(ph, clamp(0.12f + 0.70f * u, 0.05f, 0.95f));
            float v = sine;
            switch (preset) {
            case AmbiVotPreset::Sine:
                v = std::sin(2.0f * kPi * ph * (1.0f + 0.10f * std::floor(u * 5.0f)));
                break;
            case AmbiVotPreset::Digital: {
                const float fold = std::sin(2.0f * kPi * (ph + 0.17f * std::sin(2.0f * kPi * ph * (2.0f + u * 9.0f))));
                const float stair = std::floor((saw * 0.5f + 0.5f) * (3.0f + u * 21.0f)) / (3.0f + u * 21.0f) * 2.0f - 1.0f;
                v = lerp(fold, stair, 0.45f + 0.45f * u);
                break;
            }
            case AmbiVotPreset::Formant: {
                const float f1 = 2.0f + 10.0f * u;
                const float f2 = 5.0f + 17.0f * (1.0f - u);
                v = 0.55f * sine
                    + 0.30f * std::sin(2.0f * kPi * ph * f1)
                    + 0.18f * std::sin(2.0f * kPi * ph * f2 + u * 3.0f);
                break;
            }
            case AmbiVotPreset::Classic:
            default:
                if (u < 0.333333f) v = lerp(sine, tri, u / 0.333333f);
                else if (u < 0.666667f) v = lerp(tri, saw, (u - 0.333333f) / 0.333334f);
                else v = lerp(saw, pulse, (u - 0.666667f) / 0.333333f);
                break;
            }
            bank.tables[t][i] = v;
        }
    }
    ambiVotNormalizeBank(bank);
    ambiVotBuildBandTables(bank);
    return bank;
}

inline AmbiVotTableBank ambiVotBankFromWave(const std::vector<float>& wave)
{
    AmbiVotTableBank bank {};
    bank.user = true;
    if (wave.size() < 8u) return ambiVotPresetBank(AmbiVotPreset::Classic);

    const float segment = static_cast<float>(wave.size()) / static_cast<float>(kAmbiVotTableCount);
    bank.exactAtlas = wave.size() == kAmbiVotAtlasSampleCount;
    for (uint32_t t = 0; t < kAmbiVotTableCount; ++t) {
        const float start = segment * static_cast<float>(t);
        const float len = std::max(2.0f, segment);
        for (uint32_t i = 0; i < kAmbiVotTableSize; ++i) {
            const float p = start + (static_cast<float>(i) / static_cast<float>(kAmbiVotTableSize)) * len;
            const uint32_t i0 = std::min<uint32_t>(static_cast<uint32_t>(std::floor(p)), static_cast<uint32_t>(wave.size() - 1u));
            const uint32_t i1 = std::min<uint32_t>(i0 + 1u, static_cast<uint32_t>(wave.size() - 1u));
            bank.tables[t][i] = lerp(wave[i0], wave[i1], p - std::floor(p));
        }
        if (!bank.exactAtlas) {
            const float first = bank.tables[t].front();
            const float last = bank.tables[t].back();
            for (uint32_t i = 0; i < kAmbiVotTableSize; ++i) {
                const float u = static_cast<float>(i) / static_cast<float>(kAmbiVotTableSize - 1u);
                bank.tables[t][i] -= lerp(first, last, u);
            }
        }
    }
    ambiVotNormalizeBank(bank);
    ambiVotBuildBandTables(bank);
    return bank;
}

inline AmbiVotBandSelection ambiVotBandSelectionForFrequency(float frequency, double sampleRate)
{
    const float safeFrequency = std::max(1.0f, frequency);
    const float maximumHarmonic = static_cast<float>(sampleRate) * 0.45f / safeFrequency;
    for (uint32_t band = 1u; band < kAmbiVotBandCount; ++band) {
        const float threshold = static_cast<float>(kAmbiVotBandHarmonics[band]);
        if (maximumHarmonic >= threshold) continue;
        const float transitionStart = threshold * 0.92f;
        if (maximumHarmonic <= transitionStart) return { band - 1u, band - 1u, 0.0f };
        const float amount = clamp((maximumHarmonic - transitionStart)
                                       / std::max(0.000001f, threshold - transitionStart),
                                   0.0f,
                                   1.0f);
        const float smoothAmount = amount * amount * (3.0f - 2.0f * amount);
        return { band - 1u, band, smoothAmount };
    }
    return { kAmbiVotBandCount - 1u, kAmbiVotBandCount - 1u, 0.0f };
}

inline uint32_t ambiVotBandForFrequency(float frequency, double sampleRate)
{
    const auto selection = ambiVotBandSelectionForFrequency(frequency, sampleRate);
    return selection.interpolation >= 0.5f ? selection.upper : selection.lower;
}

inline float ambiVotTableSample(const AmbiVotTableBank& bank,
                                uint32_t table,
                                float phase,
                                uint32_t band = kAmbiVotBandCount - 1u)
{
    table = std::min<uint32_t>(table, kAmbiVotTableCount - 1u);
    band = std::min<uint32_t>(band, kAmbiVotBandCount - 1u);
    phase = ambiVotFract(phase);
    const float p = phase * static_cast<float>(kAmbiVotTableSize);
    const uint32_t i0 = static_cast<uint32_t>(std::floor(p)) & (kAmbiVotTableSize - 1u);
    const uint32_t i1 = (i0 + 1u) & (kAmbiVotTableSize - 1u);
    return lerp(bank.bandTables[band][table][i0],
                bank.bandTables[band][table][i1],
                p - std::floor(p));
}

inline float ambiVotTableSample(const AmbiVotTableBank& bank,
                                uint32_t table,
                                float phase,
                                const AmbiVotBandSelection& selection)
{
    const float lower = ambiVotTableSample(bank, table, phase, selection.lower);
    if (selection.lower == selection.upper || selection.interpolation <= 0.0f) return lower;
    const float upper = ambiVotTableSample(bank, table, phase, selection.upper);
    return lerp(lower, upper, selection.interpolation);
}

inline AmbiVotVectorWeights ambiVotVectorWeights(float x, float y, float morph)
{
    x = clamp(x, 0.0f, 1.0f);
    y = clamp(y, 0.0f, 1.0f);
    morph = clamp(morph, 0.0f, 1.0f);
    const float shapedMorph = morph * morph * (3.0f - 2.0f * morph);
    AmbiVotVectorWeights result {};
    result.sigmaCells = lerp(0.28f, 0.58f, shapedMorph);
    const float inverseTwoSigmaSquared = 0.5f / (result.sigmaCells * result.sigmaCells);
    const float gridX = x * static_cast<float>(kAmbiVotGridSize);
    const float gridY = y * static_cast<float>(kAmbiVotGridSize);
    std::array<float, kAmbiVotGridSize> xWeights {};
    std::array<float, kAmbiVotGridSize> yWeights {};
    for (uint32_t i = 0; i < kAmbiVotGridSize; ++i) {
        const float center = static_cast<float>(i) + 0.5f;
        const float dx = gridX - center;
        const float dy = gridY - center;
        xWeights[i] = std::exp(-(dx * dx) * inverseTwoSigmaSquared);
        yWeights[i] = std::exp(-(dy * dy) * inverseTwoSigmaSquared);
    }
    float sum = 0.0f;
    for (uint32_t row = 0; row < kAmbiVotGridSize; ++row) {
        for (uint32_t column = 0; column < kAmbiVotGridSize; ++column) {
            const uint32_t table = row * kAmbiVotGridSize + column;
            result.values[table] = xWeights[column] * yWeights[row];
            sum += result.values[table];
        }
    }
    const float inverseSum = 1.0f / std::max(sum, 0.000001f);
    for (float& weight : result.values) weight *= inverseSum;
    return result;
}

inline float ambiVotVectorSample(const AmbiVotTableBank& bank,
                                 const AmbiVotVectorWeights& weights,
                                 float phase,
                                 uint32_t band = kAmbiVotBandCount - 1u)
{
    float sample = 0.0f;
    for (uint32_t table = 0; table < kAmbiVotTableCount; ++table) {
        sample += weights.values[table] * ambiVotTableSample(bank, table, phase, band);
    }
    return sample;
}

inline float ambiVotVectorSample(const AmbiVotTableBank& bank,
                                 const AmbiVotVectorWeights& from,
                                 const AmbiVotVectorWeights& to,
                                 float interpolation,
                                 float phase,
                                 uint32_t band = kAmbiVotBandCount - 1u)
{
    interpolation = clamp(interpolation, 0.0f, 1.0f);
    float sample = 0.0f;
    for (uint32_t table = 0; table < kAmbiVotTableCount; ++table) {
        const float weight = lerp(from.values[table], to.values[table], interpolation);
        sample += weight * ambiVotTableSample(bank, table, phase, band);
    }
    return sample;
}

inline float ambiVotVectorSample(const AmbiVotTableBank& bank,
                                 const AmbiVotVectorWeights& from,
                                 const AmbiVotVectorWeights& to,
                                 float interpolation,
                                 float phase,
                                 const AmbiVotBandSelection& selection)
{
    interpolation = clamp(interpolation, 0.0f, 1.0f);
    float sample = 0.0f;
    for (uint32_t table = 0; table < kAmbiVotTableCount; ++table) {
        const float weight = lerp(from.values[table], to.values[table], interpolation);
        sample += weight * ambiVotTableSample(bank, table, phase, selection);
    }
    return sample;
}

inline float ambiVotVectorSample(const AmbiVotTableBank& bank, float x, float y, float phase, float morph)
{
    return ambiVotVectorSample(bank, ambiVotVectorWeights(x, y, morph), phase);
}

struct AmbiVotVoice {
    bool gate = false;
    int note = -1;
    float velocity = 0.0f;
    float phase = 0.0f;
    AmbiVotEnvelope envelope {};
    float age = 0.0f;
};

struct AmbiVotScorePlayback {
    float phase = 0.0f;
    float direction = 1.0f;
    float releaseProgress = 0.0f;
    float releaseU = 0.5f;
    float releaseV = 0.5f;
    bool gate = false;
    bool releasing = false;

    void trigger()
    {
        phase = 0.0f;
        direction = 1.0f;
        releaseProgress = 0.0f;
        gate = true;
        releasing = false;
    }

    void release(const AmbiVotVectorScore& score)
    {
        if (!gate || releasing) return;
        const auto point = ambiVotScorePoint(score, phase);
        releaseU = point[0];
        releaseV = point[1];
        releaseProgress = 0.0f;
        gate = false;
        releasing = true;
    }

    void setGate(bool nextGate, const AmbiVotVectorScore& score)
    {
        if (nextGate == gate && !(!nextGate && releasing)) return;
        if (nextGate) trigger();
        else release(score);
    }

    void advance(float dt,
                 float durationSec,
                 float releaseSec,
                 AmbiVotScoreMode mode,
                 const AmbiVotVectorScore& score)
    {
        if (releasing) {
            releaseProgress = clamp(releaseProgress + dt / std::max(0.005f, releaseSec), 0.0f, 1.0f);
            if (releaseProgress >= 1.0f) {
                phase = 1.0f;
                releasing = false;
            }
            return;
        }
        if (!gate || mode == AmbiVotScoreMode::Off) return;

        const float step = dt / std::max(0.05f, durationSec);
        if (mode == AmbiVotScoreMode::OneShot) {
            phase = std::min(1.0f, phase + step);
            return;
        }

        const float loopStart = score.nodes[score.sustainStart].time;
        const float loopEnd = score.nodes[score.sustainEnd].time;
        const float loopLength = std::max(0.01f, loopEnd - loopStart);
        phase += step * direction;
        if (mode == AmbiVotScoreMode::Loop) {
            if (phase > loopEnd) phase = loopStart + std::fmod(phase - loopStart, loopLength);
        } else if (mode == AmbiVotScoreMode::PingPong) {
            if (phase > loopEnd) {
                phase = loopEnd - (phase - loopEnd);
                direction = -1.0f;
            } else if (phase < loopStart) {
                phase = loopStart + (loopStart - phase);
                direction = 1.0f;
            }
        }
        phase = clamp(phase, 0.0f, 1.0f);
    }

    std::array<float, 2> point(const AmbiVotVectorScore& score) const
    {
        if (!releasing) return ambiVotScorePoint(score, phase);
        const auto destination = ambiVotScorePoint(score, 1.0f);
        const float interpolation = 0.5f - 0.5f * std::cos(kPi * clamp(releaseProgress, 0.0f, 1.0f));
        return {{ lerp(releaseU, destination[0], interpolation),
                  lerp(releaseV, destination[1], interpolation) }};
    }
};

class AmbiVotEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        reset();
    }

    void reset()
    {
        for (uint32_t i = 0; i < kAmbiVotMaxVoices; ++i) {
            voices_[i] = {};
            freePhases_[i] = ambiVotFract(static_cast<float>(i) * 0.6180339887f);
            freeEnvelopes_[i] = {};
            scorePlayback_[i] = {};
            motionPoints_[i] = motionTarget(i, 0.0f);
            neighborCounts_[i] = 0u;
            neighborGates_[i] = 0u;
        }
        motionPhase_ = 0.0f;
        externalPhase_ = 0.0f;
        externalPhaseActive_ = false;
        motionPrimed_ = false;
        updateNeighborGraph();
    }

    void setParams(AmbiVotParams params)
    {
        params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiVotMaxOrder);
        params.voices = std::clamp<uint32_t>(params.voices, 1u, kAmbiVotMaxVoices);
        params.mode = static_cast<AmbiVotMode>(std::clamp<uint32_t>(static_cast<uint32_t>(params.mode), 0u, 2u));
        params.preset = static_cast<AmbiVotPreset>(std::clamp<uint32_t>(static_cast<uint32_t>(params.preset), 0u, 4u));
        params.baseNote = clamp(params.baseNote, 12.0f, 96.0f);
        params.tuneCents = clamp(params.tuneCents, -1200.0f, 1200.0f);
        params.vectorX = clamp(params.vectorX, 0.0f, 1.0f);
        params.vectorY = clamp(params.vectorY, 0.0f, 1.0f);
        params.scan = clamp(params.scan, 0.0f, 1.0f);
        params.scanRate = clamp(params.scanRate, -4.0f, 4.0f);
        params.morph = clamp(params.morph, 0.0f, 1.0f);
        params.detune = clamp(params.detune, 0.0f, 1.0f);
        params.scale = static_cast<AmbiVotScale>(std::clamp<uint32_t>(static_cast<uint32_t>(params.scale), 0u, 5u));
        params.pitchSpread = clamp(params.pitchSpread, 0.0f, 2.0f);
        params.harmonicAmount = clamp(params.harmonicAmount, 0.0f, 1.0f);
        params.subharmonicAmount = clamp(params.subharmonicAmount, 0.0f, 1.0f);
        params.motionSpread = clamp(params.motionSpread, 0.0f, 1.0f);
        params.motionRateHz = clamp(params.motionRateHz, 0.001f, 2.0f);
        params.attackMs = clamp(params.attackMs, 1.0f, 2000.0f);
        params.decayMs = clamp(params.decayMs, 5.0f, 4000.0f);
        params.sustain = clamp(params.sustain, 0.0f, 1.0f);
        params.releaseMs = clamp(params.releaseMs, 5.0f, 8000.0f);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
        params.motionScene = static_cast<AmbiVotMotionScene>(std::clamp<uint32_t>(static_cast<uint32_t>(params.motionScene), 0u, 4u));
        params.motionClock = static_cast<AmbiVotMotionClock>(std::clamp<uint32_t>(static_cast<uint32_t>(params.motionClock), 0u, 1u));
        params.syncDivisionBeats = clamp(params.syncDivisionBeats, 0.25f, 64.0f);
        params.motionAmount = clamp(params.motionAmount, 0.0f, 1.0f);
        params.motionCoherence = clamp(params.motionCoherence, 0.0f, 1.0f);
        params.motionChaos = clamp(params.motionChaos, 0.0f, 1.0f);
        params.motionLink = clamp(params.motionLink, 0.0f, 1.0f);
        params.motionSmooth = clamp(params.motionSmooth, 0.0f, 1.0f);
        params.centerAzimuthDeg = ambiVotWrapSignedDeg(params.centerAzimuthDeg);
        params.centerElevationDeg = clamp(params.centerElevationDeg, -90.0f, 90.0f);
        params.centerDistance = clamp(params.centerDistance, 0.15f, 2.0f);
        params.neighborRadius = clamp(params.neighborRadius, 0.05f, 4.0f);
        params.requiredNeighbors = std::clamp<uint32_t>(params.requiredNeighbors, 1u, kAmbiVotMaxVoices - 1u);
        params.scoreMode = static_cast<AmbiVotScoreMode>(std::clamp<uint32_t>(static_cast<uint32_t>(params.scoreMode), 0u, 3u));
        params.scoreDurationSec = clamp(params.scoreDurationSec, 0.25f, 60.0f);
        params.scoreDepth = clamp(params.scoreDepth, 0.0f, 1.0f);
        params_ = params;
    }

    void setScore(AmbiVotVectorScore score)
    {
        ambiVotNormalizeScore(score);
        score_ = score;
    }

    AmbiVotParams params() const { return params_; }
    const std::array<AmbiVotMotionPoint, kAmbiVotMaxVoices>& motionPoints() const { return motionPoints_; }
    const std::array<uint32_t, kAmbiVotMaxVoices>& neighborCounts() const { return neighborCounts_; }
    const std::array<uint8_t, kAmbiVotMaxVoices>& neighborGates() const { return neighborGates_; }
    float motionPhase() const { return motionPhase_; }
    const AmbiVotVectorScore& score() const { return score_; }

    void setExternalPhase(float phase)
    {
        externalPhaseActive_ = true;
        externalPhase_ = ambiVotFract(phase);
    }

    void useFreePhase()
    {
        externalPhaseActive_ = false;
    }

    void noteOn(int note, float velocity)
    {
        uint32_t best = 0;
        float oldest = -1.0f;
        for (uint32_t i = 0; i < params_.voices; ++i) {
            if (!voices_[i].gate && voices_[i].envelope.value < 0.001f) {
                best = i;
                break;
            }
            if (voices_[i].age > oldest) {
                oldest = voices_[i].age;
                best = i;
            }
        }
        voices_[best].gate = true;
        voices_[best].note = note;
        voices_[best].velocity = clamp(velocity, 0.0f, 1.0f);
        voices_[best].age = 0.0f;
        voices_[best].envelope.trigger();
    }

    void noteOff(int note)
    {
        for (auto& voice : voices_) {
            if (voice.note == note) {
                voice.gate = false;
                voice.envelope.releaseGate();
            }
        }
    }

    void process(const AmbiVotTableBank& bank, float* const* outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiVotMaxChannels);
        for (uint32_t ch = 0; ch < outputChannels; ++ch) {
            if (outputs[ch]) std::fill(outputs[ch], outputs[ch] + frames, 0.0f);
        }

        const uint32_t ambiChannels = std::min<uint32_t>(ambiChannelsForOrder(params_.order), outputChannels);
        const float outGain = dbToGain(params_.outputGainDb);
        const bool freeMode = params_.mode == AmbiVotMode::Free || params_.mode == AmbiVotMode::Both;
        const bool midiMode = params_.mode == AmbiVotMode::Midi || params_.mode == AmbiVotMode::Both;
        const float layerGain = params_.mode == AmbiVotMode::Both ? 0.70710678f : 1.0f;
        const float invVoices = layerGain / std::sqrt(static_cast<float>(std::max<uint32_t>(1u, params_.voices)));
        const float attackCoef = ambiVotEnvelopeCoefficient(params_.attackMs, sampleRate_);
        const float decayCoef = ambiVotEnvelopeCoefficient(params_.decayMs, sampleRate_);
        const float releaseCoef = ambiVotEnvelopeCoefficient(params_.releaseMs, sampleRate_);
        std::array<float, kAmbiVotMaxVoices> freeNotes {};
        std::array<float, kAmbiVotMaxVoices> midiNotes {};
        std::array<float, kAmbiVotMaxVoices> freeFrequencies {};
        std::array<float, kAmbiVotMaxVoices> midiFrequencies {};
        std::array<AmbiVotBandSelection, kAmbiVotMaxVoices> freeBands {};
        std::array<AmbiVotBandSelection, kAmbiVotMaxVoices> midiBands {};
        for (uint32_t i = 0; i < params_.voices; ++i) {
            freeNotes[i] = ambiVotTunedNote(params_, params_.baseNote, i, false);
            midiNotes[i] = ambiVotTunedNote(params_, static_cast<float>(voices_[i].note), i, true);
            freeFrequencies[i] = std::min(ambiVotMidiToHz(freeNotes[i]), static_cast<float>(sampleRate_) * 0.45f);
            midiFrequencies[i] = std::min(ambiVotMidiToHz(midiNotes[i]), static_cast<float>(sampleRate_) * 0.45f);
            freeBands[i] = ambiVotBandSelectionForFrequency(freeFrequencies[i], sampleRate_);
            midiBands[i] = ambiVotBandSelectionForFrequency(midiFrequencies[i], sampleRate_);
        }
        constexpr uint32_t kMotionChunkFrames = 16;

        for (uint32_t chunkStart = 0; chunkStart < frames; chunkStart += kMotionChunkFrames) {
            const uint32_t chunkFrames = std::min<uint32_t>(kMotionChunkFrames, frames - chunkStart);
            const float chunkSeconds = static_cast<float>(chunkFrames) / static_cast<float>(sampleRate_);
            std::array<AmbiVotVectorWeights, kAmbiVotMaxVoices> vectorWeightsFrom {};
            std::array<AmbiVotVectorWeights, kAmbiVotMaxVoices> vectorWeightsTo {};
            for (uint32_t i = 0; i < params_.voices; ++i) {
                vectorWeightsFrom[i] = ambiVotVectorWeights(
                    motionPoints_[i].u, motionPoints_[i].v, params_.morph);
            }
            updateScorePlayback(chunkSeconds, freeMode, midiMode);
            advanceMotion(chunkSeconds);
            updateNeighborGraph();
            std::array<std::array<float, kAmbiVotMaxChannels>, kAmbiVotMaxVoices> basis {};
            std::array<float, kAmbiVotMaxVoices> distanceGain {};
            for (uint32_t i = 0; i < params_.voices; ++i) {
                const auto& point = motionPoints_[i];
                vectorWeightsTo[i] = ambiVotVectorWeights(point.u, point.v, params_.morph);
                basis[i] = acnSn3dBasis7(directionFromAed(point.azimuthDeg, point.elevationDeg));
                distanceGain[i] = 1.0f / std::max(0.5f, point.distance);
            }

            for (uint32_t frame = chunkStart; frame < chunkStart + chunkFrames; ++frame) {
                const float vectorInterpolation = static_cast<float>(frame - chunkStart + 1u)
                    / static_cast<float>(chunkFrames);
                auto renderVoice = [&](uint32_t i,
                                       float& phase,
                                       float envelope,
                                       float frequency,
                                       const AmbiVotBandSelection& band,
                                       float velocity) {
                    const float freq = frequency;
                    phase = ambiVotFract(phase + freq / static_cast<float>(sampleRate_));
                    float sample = ambiVotVectorSample(
                        bank, vectorWeightsFrom[i], vectorWeightsTo[i], vectorInterpolation, phase, band);
                    sample = softSat(sample * 1.05f);
                    const float amp = sample * envelope * velocity * outGain * invVoices * distanceGain[i];
                    if (std::fabs(amp) < 0.0000001f) return;
                    for (uint32_t ch = 0; ch < ambiChannels; ++ch) {
                        if (outputs[ch]) outputs[ch][frame] = flushDenormal(outputs[ch][frame] + amp * basis[i][ch]);
                    }
                };

                if (freeMode) {
                    for (uint32_t i = 0; i < params_.voices; ++i) {
                        freeEnvelopes_[i].setGate(neighborGates_[i] != 0u);
                        const float envelope = freeEnvelopes_[i].process(
                            attackCoef, decayCoef, params_.sustain, releaseCoef);
                        renderVoice(i, freePhases_[i], envelope, freeFrequencies[i], freeBands[i], 0.70f);
                    }
                }
                if (midiMode) {
                    for (uint32_t i = 0; i < params_.voices; ++i) {
                        auto& voice = voices_[i];
                        const float envelope = voice.envelope.process(
                            attackCoef, decayCoef, params_.sustain, releaseCoef);
                        if (voice.envelope.stage == AmbiVotEnvelopeStage::Idle) continue;
                        voice.age += 1.0f / static_cast<float>(sampleRate_);
                        renderVoice(i,
                                    voice.phase,
                                    envelope,
                                    midiFrequencies[i],
                                    midiBands[i],
                                    std::max(0.05f, voice.velocity));
                    }
                }
            }
        }

        for (uint32_t ch = 0; ch < ambiChannels; ++ch) {
            if (!outputs[ch]) continue;
            for (uint32_t frame = 0; frame < frames; ++frame) outputs[ch][frame] = softSat(outputs[ch][frame]);
        }
    }

private:
    void updateScorePlayback(float dt, bool freeMode, bool midiMode)
    {
        const float releaseSec = params_.releaseMs * 0.001f;
        for (uint32_t i = 0; i < params_.voices; ++i) {
            const bool freeGate = freeMode && neighborGates_[i] != 0u;
            const bool midiGate = midiMode && voices_[i].gate;
            scorePlayback_[i].setGate(freeGate || midiGate, score_);
            scorePlayback_[i].advance(
                dt, params_.scoreDurationSec, releaseSec, params_.scoreMode, score_);
        }
        for (uint32_t i = params_.voices; i < kAmbiVotMaxVoices; ++i) {
            scorePlayback_[i].setGate(false, score_);
        }
    }

    AmbiVotMotionPoint baseFormationPoint(uint32_t index) const
    {
        const float count = static_cast<float>(std::max<uint32_t>(1u, params_.voices));
        const float lane = static_cast<float>(index) / count;
        const float spread = params_.motionSpread;
        AmbiVotMotionPoint point {};
        point.azimuthDeg = ambiVotWrapSignedDeg(params_.centerAzimuthDeg - lane * 360.0f * spread);
        point.elevationDeg = clamp(params_.centerElevationDeg
            + std::sin(static_cast<float>(index) * 2.39996323f) * 38.0f * spread, -90.0f, 90.0f);
        point.distance = params_.centerDistance;
        point.u = params_.vectorX;
        point.v = params_.vectorY;
        return point;
    }

    AmbiVotMotionPoint motionTarget(uint32_t index, float phase) const
    {
        AmbiVotMotionPoint point = baseFormationPoint(index);
        const float amount = params_.motionAmount;
        const float lane = static_cast<float>(index) / static_cast<float>(std::max<uint32_t>(1u, params_.voices));
        const float phaseOffset = lane * params_.motionSpread * (1.0f - params_.motionCoherence);
        const float p = ambiVotFract(phase + phaseOffset);
        const float t = p * 2.0f * kPi;
        const float seed = static_cast<float>(index) * 1.61803398875f;
        const float chaos = params_.motionChaos;

        switch (params_.motionScene) {
        case AmbiVotMotionScene::Orbit:
            point.azimuthDeg = ambiVotWrapSignedDeg(point.azimuthDeg
                + std::sin(t + seed * 0.17f) * 125.0f * amount);
            point.elevationDeg = clamp(point.elevationDeg
                + std::cos(t + seed * 0.13f) * 58.0f * amount, -90.0f, 90.0f);
            point.distance = clamp(params_.centerDistance
                * (1.0f + std::sin(t * 2.0f + seed) * 0.20f * amount), 0.15f, 2.0f);
            break;
        case AmbiVotMotionScene::Flow: {
            const float a = std::sin(t + seed) * 0.68f
                + std::sin(t * 2.0f + seed * 0.31f) * (0.22f + chaos * 0.18f);
            const float e = std::sin(t * 2.0f + seed * 0.57f) * 0.62f
                + std::cos(t * 3.0f + seed * 0.23f) * (0.18f + chaos * 0.20f);
            point.azimuthDeg = ambiVotWrapSignedDeg(point.azimuthDeg + a * 150.0f * amount);
            point.elevationDeg = clamp(point.elevationDeg + e * 62.0f * amount, -90.0f, 90.0f);
            point.distance = clamp(params_.centerDistance
                * (1.0f + std::sin(t * 3.0f + seed * 0.91f) * (0.28f + chaos * 0.18f) * amount), 0.15f, 2.0f);
            break;
        }
        case AmbiVotMotionScene::Path:
            if (amount <= 0.0001f) break;
            point.azimuthDeg = ambiVotWrapSignedDeg(params_.centerAzimuthDeg
                - p * 360.0f
                + std::sin(t * 2.0f + seed * 0.09f) * 28.0f * amount);
            point.elevationDeg = clamp(params_.centerElevationDeg
                + (std::sin(t) * 52.0f + std::sin(t * 3.0f + 0.7f) * 17.0f) * amount, -90.0f, 90.0f);
            point.distance = clamp(params_.centerDistance
                * (1.0f + (0.24f * std::sin(t * 2.0f + 0.4f) + 0.10f * std::sin(t * 5.0f)) * amount), 0.15f, 2.0f);
            break;
        case AmbiVotMotionScene::Pulse: {
            const float pulse = 0.5f - 0.5f * std::cos(t);
            point.azimuthDeg = ambiVotWrapSignedDeg(point.azimuthDeg
                + std::sin(t + seed * 0.15f) * 24.0f * amount);
            point.elevationDeg = clamp(point.elevationDeg
                + std::sin(t * 2.0f + seed * 0.11f) * 16.0f * amount, -90.0f, 90.0f);
            point.distance = clamp(params_.centerDistance * (1.0f + (pulse * 0.70f - 0.25f) * amount), 0.15f, 2.0f);
            break;
        }
        case AmbiVotMotionScene::Manual:
        default:
            break;
        }

        const bool vectorMoving = params_.motionScene != AmbiVotMotionScene::Manual;
        const float uvPhase = vectorMoving ? p * params_.scanRate : 0.0f;
        const float uvAngle = 2.0f * kPi * uvPhase;
        float pathU = 0.5f;
        float pathV = 0.5f;
        switch (params_.motionScene) {
        case AmbiVotMotionScene::Orbit:
            pathU = 0.5f + 0.5f * std::sin(
                uvAngle + seed * 0.19f + chaos * 0.42f * std::sin(uvAngle * 2.0f + seed));
            pathV = 0.5f + 0.5f * std::cos(
                uvAngle + seed * 0.27f + chaos * 0.46f * std::sin(uvAngle * 3.0f + seed * 0.61f));
            break;
        case AmbiVotMotionScene::Flow:
            pathU = 0.5f + 0.5f * std::sin(
                uvAngle * 3.0f + seed * 0.19f + chaos * 0.55f * std::sin(uvAngle * 2.0f + seed));
            pathV = 0.5f + 0.5f * std::sin(
                uvAngle * 4.0f + seed * 0.27f + 0.5f * kPi
                + chaos * 0.62f * std::sin(uvAngle * 3.0f + seed * 0.61f));
            break;
        case AmbiVotMotionScene::Path: {
            const float pathPhase = ambiVotFract(uvPhase
                + chaos * 0.025f * std::sin(uvAngle * 3.0f + seed));
            const float tablePosition = pathPhase * static_cast<float>(kAmbiVotTableCount);
            const uint32_t step0 = static_cast<uint32_t>(std::floor(tablePosition)) % kAmbiVotTableCount;
            const uint32_t step1 = (step0 + 1u) % kAmbiVotTableCount;
            const float localPhase = tablePosition - std::floor(tablePosition);
            const float interpolation = 0.5f - 0.5f * std::cos(kPi * localPhase);
            const auto tableCenter = [](uint32_t step, float& u, float& v) {
                const uint32_t row = step / kAmbiVotGridSize;
                const uint32_t positionInRow = step % kAmbiVotGridSize;
                const uint32_t column = (row & 1u) != 0u
                    ? kAmbiVotGridSize - 1u - positionInRow
                    : positionInRow;
                u = (static_cast<float>(column) + 0.5f) / static_cast<float>(kAmbiVotGridSize);
                v = (static_cast<float>(row) + 0.5f) / static_cast<float>(kAmbiVotGridSize);
            };
            float u0 = 0.5f;
            float v0 = 0.5f;
            float u1 = 0.5f;
            float v1 = 0.5f;
            tableCenter(step0, u0, v0);
            tableCenter(step1, u1, v1);
            pathU = lerp(u0, u1, interpolation);
            pathV = lerp(v0, v1, interpolation);
            break;
        }
        case AmbiVotMotionScene::Pulse: {
            const float radius = 0.5f * (0.5f - 0.5f * std::cos(uvAngle));
            const float direction = seed * 0.31f + chaos * std::sin(uvAngle * 2.0f + seed);
            pathU = 0.5f + std::cos(direction) * radius;
            pathV = 0.5f + std::sin(direction) * radius;
            break;
        }
        case AmbiVotMotionScene::Manual:
        default:
            break;
        }
        const float linkedU = 0.5f + 0.5f * std::sin(point.azimuthDeg * kPi / 180.0f);
        const float linkedV = clamp((point.elevationDeg + 90.0f) / 180.0f, 0.0f, 1.0f);
        const float centerU = lerp(params_.vectorX, linkedU, params_.motionLink);
        const float centerV = lerp(params_.vectorY, linkedV, params_.motionLink);
        const float vectorCoverage = vectorMoving
            ? params_.scan
            : 0.0f;
        point.u = lerp(centerU, pathU, vectorCoverage);
        point.v = lerp(centerV, pathV, vectorCoverage);
        if (params_.scoreMode != AmbiVotScoreMode::Off && params_.scoreDepth > 0.0001f) {
            const auto scorePoint = scorePlayback_[index].point(score_);
            point.u = lerp(point.u, scorePoint[0], params_.scoreDepth);
            point.v = lerp(point.v, scorePoint[1], params_.scoreDepth);
        }
        return point;
    }

    void advanceMotion(float dt)
    {
        if (externalPhaseActive_) motionPhase_ = externalPhase_;
        else motionPhase_ = ambiVotFract(motionPhase_ + params_.motionRateHz * std::max(0.0f, dt));

        const float tau = lerp(0.012f, 0.42f, params_.motionSmooth);
        const float alpha = motionPrimed_ ? 1.0f - std::exp(-std::max(0.0f, dt) / std::max(0.001f, tau)) : 1.0f;
        for (uint32_t i = 0; i < kAmbiVotMaxVoices; ++i) {
            const AmbiVotMotionPoint target = motionTarget(i, motionPhase_);
            float azDelta = ambiVotWrapSignedDeg(target.azimuthDeg - motionPoints_[i].azimuthDeg);
            motionPoints_[i].azimuthDeg = ambiVotWrapSignedDeg(motionPoints_[i].azimuthDeg + azDelta * alpha);
            motionPoints_[i].elevationDeg += (target.elevationDeg - motionPoints_[i].elevationDeg) * alpha;
            motionPoints_[i].distance += (target.distance - motionPoints_[i].distance) * alpha;
            motionPoints_[i].u += (target.u - motionPoints_[i].u) * alpha;
            motionPoints_[i].v += (target.v - motionPoints_[i].v) * alpha;
        }
        motionPrimed_ = true;
    }

    void updateNeighborGraph()
    {
        std::array<Vec3, kAmbiVotMaxVoices> positions {};
        std::array<uint32_t, kAmbiVotMaxVoices> releaseCounts {};
        neighborCounts_.fill(0u);
        const uint32_t voices = params_.voices;
        for (uint32_t i = 0; i < voices; ++i) {
            const auto& point = motionPoints_[i];
            const Vec3 direction = directionFromAed(point.azimuthDeg, point.elevationDeg);
            positions[i] = { direction.x * point.distance, direction.y * point.distance, direction.z * point.distance };
        }
        const float attackRadiusSquared = params_.neighborRadius * params_.neighborRadius;
        const float releaseRadius = params_.neighborRadius * 1.12f;
        const float releaseRadiusSquared = releaseRadius * releaseRadius;
        for (uint32_t a = 0; a < voices; ++a) {
            for (uint32_t b = a + 1u; b < voices; ++b) {
                const float dx = positions[a].x - positions[b].x;
                const float dy = positions[a].y - positions[b].y;
                const float dz = positions[a].z - positions[b].z;
                const float distanceSquared = dx * dx + dy * dy + dz * dz;
                if (distanceSquared <= attackRadiusSquared) {
                    ++neighborCounts_[a];
                    ++neighborCounts_[b];
                }
                if (distanceSquared <= releaseRadiusSquared) {
                    ++releaseCounts[a];
                    ++releaseCounts[b];
                }
            }
        }
        for (uint32_t i = 0; i < voices; ++i) {
            const uint32_t count = neighborGates_[i] != 0u ? releaseCounts[i] : neighborCounts_[i];
            neighborGates_[i] = count >= params_.requiredNeighbors ? 1u : 0u;
        }
        for (uint32_t i = voices; i < kAmbiVotMaxVoices; ++i) {
            neighborCounts_[i] = 0u;
            neighborGates_[i] = 0u;
        }
    }

    double sampleRate_ = 48000.0;
    AmbiVotParams params_ {};
    std::array<AmbiVotVoice, kAmbiVotMaxVoices> voices_ {};
    std::array<float, kAmbiVotMaxVoices> freePhases_ {};
    std::array<AmbiVotEnvelope, kAmbiVotMaxVoices> freeEnvelopes_ {};
    std::array<AmbiVotScorePlayback, kAmbiVotMaxVoices> scorePlayback_ {};
    std::array<AmbiVotMotionPoint, kAmbiVotMaxVoices> motionPoints_ {};
    std::array<uint32_t, kAmbiVotMaxVoices> neighborCounts_ {};
    std::array<uint8_t, kAmbiVotMaxVoices> neighborGates_ {};
    float motionPhase_ = 0.0f;
    float externalPhase_ = 0.0f;
    bool externalPhaseActive_ = false;
    bool motionPrimed_ = false;
    AmbiVotVectorScore score_ = ambiVotDefaultScore();
};

inline const char* ambiVotModeName(AmbiVotMode mode)
{
    switch (mode) {
    case AmbiVotMode::Midi: return "MIDI";
    case AmbiVotMode::Both: return "BOTH";
    case AmbiVotMode::Free:
    default: return "FREE";
    }
}

inline const char* ambiVotPresetName(AmbiVotPreset preset)
{
    switch (preset) {
    case AmbiVotPreset::Sine: return "SINE";
    case AmbiVotPreset::Digital: return "DIGITAL";
    case AmbiVotPreset::Formant: return "FORMANT";
    case AmbiVotPreset::User: return "USER";
    case AmbiVotPreset::Classic:
    default: return "CLASSIC";
    }
}

inline const char* ambiVotMotionSceneName(AmbiVotMotionScene scene)
{
    switch (scene) {
    case AmbiVotMotionScene::Orbit: return "ORBIT";
    case AmbiVotMotionScene::Flow: return "FLOW";
    case AmbiVotMotionScene::Path: return "PATH";
    case AmbiVotMotionScene::Pulse: return "PULSE";
    case AmbiVotMotionScene::Manual:
    default: return "MANUAL";
    }
}

inline const char* ambiVotMotionClockName(AmbiVotMotionClock clock)
{
    return clock == AmbiVotMotionClock::Sync ? "SYNC" : "FREE";
}

inline const char* ambiVotScaleName(AmbiVotScale scale)
{
    switch (scale) {
    case AmbiVotScale::Major: return "MAJOR";
    case AmbiVotScale::Minor: return "MINOR";
    case AmbiVotScale::Pentatonic: return "PENTA";
    case AmbiVotScale::WholeTone: return "WHOLE";
    case AmbiVotScale::HarmonicMinor: return "HARM MIN";
    case AmbiVotScale::Chromatic:
    default: return "CHROM";
    }
}

inline const char* ambiVotScoreModeName(AmbiVotScoreMode mode)
{
    switch (mode) {
    case AmbiVotScoreMode::OneShot: return "ONE";
    case AmbiVotScoreMode::Loop: return "LOOP";
    case AmbiVotScoreMode::PingPong: return "PING";
    case AmbiVotScoreMode::Off:
    default: return "OFF";
    }
}

inline const char* ambiVotScoreCurveName(AmbiVotScoreCurve curve)
{
    switch (curve) {
    case AmbiVotScoreCurve::Linear: return "LINEAR";
    case AmbiVotScoreCurve::Exponential: return "EXP";
    case AmbiVotScoreCurve::Hold: return "HOLD";
    case AmbiVotScoreCurve::Smooth:
    default: return "SMOOTH";
    }
}

} // namespace s3g
