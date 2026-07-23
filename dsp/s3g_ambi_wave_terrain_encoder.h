#pragma once

#include "s3g_ambi_field_listener.h"
#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#if defined(__APPLE__)
#include <Accelerate/Accelerate.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiWaveTerrainMaxVoices = 64;
constexpr uint32_t kAmbiWaveTerrainMaxOrder = 7;
constexpr uint32_t kAmbiWaveTerrainMaxChannels = 64;
constexpr uint32_t kAmbiWaveTerrainTableSize = 512;
constexpr uint32_t kAmbiWaveTerrainMipLevels = 5;
constexpr float kAmbiWaveTerrainTwoPi = 6.28318530717958647692f;

enum class AmbiWaveTerrainMode : uint32_t { Free = 0, Midi = 1, Both = 2 };
enum class AmbiWaveTerrainPitchMode : uint32_t { Note = 0, Traversal = 1 };
enum class AmbiWaveTerrainMotionMode : uint32_t { Field = 0, Rotate = 1 };
enum class AmbiWaveTerrainPitchScale : uint32_t {
    Free = 0, Chromatic = 1, Major = 2, Minor = 3,
    Pentatonic = 4, WholeTone = 5, HarmonicMinor = 6,
};
enum class AmbiWaveTerrainForm : uint32_t {
    Sphere = 0, Tetra = 1, Cube = 2, Octa = 3, Dodeca = 4, Icosa = 5,
};
enum class AmbiWaveTerrainSkin : uint32_t {
    Harmonic = 0, Fbm = 1, Cellular = 2, Vot = 3,
    Ridges = 4, Dunes = 5, Craters = 6, Tectonic = 7,
};
enum class AmbiWaveTerrainTrace : uint32_t { Orbit = 0, Lissajous = 1, Rosette = 2, Fold = 3, Polygon = 4 };
enum class AmbiWaveTerrainSelection : uint32_t { Random = 0, Series = 1, Weight = 2, Tendency = 3, Markov = 4, Walk = 5 };
enum class AmbiWaveTerrainTransition : uint32_t { Link = 0, Merge = 1, Vary = 2 };
enum class AmbiWaveTerrainInterpretation : uint32_t {
    Height = 0, Edge = 1, Curvature = 2, Blend = 3, Gradient = 4,
    Ridge = 5, Valley = 6, Normal = 7, Cross = 8, Vector = 9,
};

struct AmbiWaveTerrainParams {
    uint32_t order = 3;
    uint32_t voices = 12;
    AmbiWaveTerrainMode mode = AmbiWaveTerrainMode::Free;
    float baseNote = 40.0f;
    float pitchSpreadSemitones = 19.0f;
    float tuneCents = 0.0f;
    float detuneCents = 9.0f;

    AmbiWaveTerrainSkin skin = AmbiWaveTerrainSkin::Fbm;
    float terrainDepth = 0.82f;
    float terrainRoughness = 0.58f;
    float terrainFold = 0.24f;
    float terrainRelief = 0.62f;

    AmbiWaveTerrainTrace trace = AmbiWaveTerrainTrace::Lissajous;
    AmbiWaveTerrainInterpretation interpretation = AmbiWaveTerrainInterpretation::Height;
    float interpretationMix = 0.32f;
    float scanRadius = 0.16f;
    float scanAspect = 0.68f;
    float scanRotation = 0.0f;
    float scanWarp = 0.18f;

    AmbiWaveTerrainSelection selection = AmbiWaveTerrainSelection::Walk;
    AmbiWaveTerrainTransition transition = AmbiWaveTerrainTransition::Link;
    float fieldDensity = 0.82f;
    float fieldDurationSeconds = 1.8f;
    float fieldRestSeconds = 0.24f;
    float fieldContrast = 0.72f;
    float selectionMemory = 0.74f;
    float regionDeviation = 0.36f;
    float neighborTransfer = 0.28f;
    float macroDurationSeconds = 24.0f;
    float tableXfadeMs = 180.0f;

    float attackMs = 12.0f;
    float decayMs = 180.0f;
    float sustain = 0.76f;
    float releaseMs = 420.0f;

    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    float spatialSpread = 0.82f;
    float spatialFollow = 0.90f;
    float outputGainDb = -22.0f;

    AmbiWaveTerrainPitchMode pitchMode = AmbiWaveTerrainPitchMode::Note;
    AmbiWaveTerrainMotionMode motionMode = AmbiWaveTerrainMotionMode::Rotate;
    float azimuthRateRpm = 0.70f;
    float elevationRateRpm = 0.43f;
    float rotationRateDeviation = 0.28f;
    AmbiWaveTerrainPitchScale pitchScale = AmbiWaveTerrainPitchScale::Free;

    AmbiWaveTerrainForm terrainForm = AmbiWaveTerrainForm::Sphere;
    float terrainFacet = 0.0f;
    float terrainBevel = 0.18f;
    float terrainOrientation = 0.0f;
    float terrainTerrace = 0.0f;
    uint32_t terrainTerraceSteps = 8u;
    float terrainRidge = 0.0f;
    float terrainErosion = 0.0f;
    float terrainDomainWarp = 0.0f;
    float terrainTwist = 0.0f;

    uint32_t polygonSides = 6u;
    float polygonRound = 0.18f;
    float polygonStar = 0.0f;
    float polygonSkew = 0.0f;
    AmbiFieldListenMode fieldListenMode = AmbiFieldListenMode::Off;
};

inline int ambiWaveTerrainScaleDegreeSemitones(AmbiWaveTerrainPitchScale scale, int degree)
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
    case AmbiWaveTerrainPitchScale::Major: values = major.data(); count = static_cast<int>(major.size()); break;
    case AmbiWaveTerrainPitchScale::Minor: values = minor.data(); count = static_cast<int>(minor.size()); break;
    case AmbiWaveTerrainPitchScale::Pentatonic: values = pentatonic.data(); count = static_cast<int>(pentatonic.size()); break;
    case AmbiWaveTerrainPitchScale::WholeTone: values = wholeTone.data(); count = static_cast<int>(wholeTone.size()); break;
    case AmbiWaveTerrainPitchScale::HarmonicMinor: values = harmonicMinor.data(); count = static_cast<int>(harmonicMinor.size()); break;
    case AmbiWaveTerrainPitchScale::Free:
    case AmbiWaveTerrainPitchScale::Chromatic:
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

inline int ambiWaveTerrainScaleSize(AmbiWaveTerrainPitchScale scale)
{
    switch (scale) {
    case AmbiWaveTerrainPitchScale::Pentatonic: return 5;
    case AmbiWaveTerrainPitchScale::WholeTone: return 6;
    case AmbiWaveTerrainPitchScale::Major:
    case AmbiWaveTerrainPitchScale::Minor:
    case AmbiWaveTerrainPitchScale::HarmonicMinor: return 7;
    case AmbiWaveTerrainPitchScale::Free:
    case AmbiWaveTerrainPitchScale::Chromatic:
    default: return 12;
    }
}

inline float ambiWaveTerrainQuantizeToScale(float note, float root, AmbiWaveTerrainPitchScale scale)
{
    if (scale == AmbiWaveTerrainPitchScale::Free) return note;
    const float relative = note - root;
    const int scaleSize = ambiWaveTerrainScaleSize(scale);
    const int approximateOctave = static_cast<int>(std::floor(relative / 12.0f));
    float bestNote = root;
    float bestDistance = 100000.0f;
    for (int octave = approximateOctave - 1; octave <= approximateOctave + 1; ++octave) {
        for (int index = 0; index < scaleSize; ++index) {
            const int degree = octave * scaleSize + index;
            const float candidate = root + static_cast<float>(ambiWaveTerrainScaleDegreeSemitones(scale, degree));
            const float distance = std::fabs(candidate - note);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestNote = candidate;
            }
        }
    }
    return bestNote;
}

struct AmbiWaveTerrainPoint {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
    float terrain = 0.0f;
};

struct AmbiWaveTerrainRegion {
    float u = 0.5f;
    float v = 0.5f;
    float radius = 0.16f;
    float aspect = 0.68f;
    float rotation = 0.0f;
    AmbiWaveTerrainTrace trace = AmbiWaveTerrainTrace::Lissajous;
};

inline std::array<float, 2> ambiWaveTerrainRotationUv(float azimuthPhase, float elevationPhase)
{
    azimuthPhase -= std::floor(azimuthPhase);
    elevationPhase -= std::floor(elevationPhase);
    const float azimuthAngle = (azimuthPhase - 0.5f) * kAmbiWaveTerrainTwoPi;
    const float elevationAngle = elevationPhase * kAmbiWaveTerrainTwoPi;
    const float ce = std::cos(elevationAngle);
    const Vec3 direction {
        ce * std::cos(azimuthAngle),
        ce * std::sin(azimuthAngle),
        std::sin(elevationAngle),
    };
    float u = std::atan2(direction.y, direction.x) / kAmbiWaveTerrainTwoPi + 0.5f;
    u -= std::floor(u);
    const float v = 0.5f + std::asin(clamp(direction.z, -1.0f, 1.0f)) / kPi;
    return { u, clamp(v, 0.0f, 1.0f) };
}

enum class AmbiWaveTerrainEnvelopeStage : uint8_t { Idle = 0, Attack, Decay, Sustain, Release };

struct AmbiWaveTerrainEnvelope {
    AmbiWaveTerrainEnvelopeStage stage = AmbiWaveTerrainEnvelopeStage::Idle;
    float value = 0.0f;
    bool gate = false;

    void setGate(bool next)
    {
        if (next && !gate) stage = AmbiWaveTerrainEnvelopeStage::Attack;
        else if (!next && gate && stage != AmbiWaveTerrainEnvelopeStage::Idle) stage = AmbiWaveTerrainEnvelopeStage::Release;
        gate = next;
    }

    float process(float attack, float decay, float sustain, float release)
    {
        switch (stage) {
        case AmbiWaveTerrainEnvelopeStage::Attack:
            value += (1.0f - value) * attack;
            if (value >= 0.999f) { value = 1.0f; stage = AmbiWaveTerrainEnvelopeStage::Decay; }
            break;
        case AmbiWaveTerrainEnvelopeStage::Decay:
            value += (sustain - value) * decay;
            if (std::fabs(value - sustain) < 0.0005f) { value = sustain; stage = AmbiWaveTerrainEnvelopeStage::Sustain; }
            break;
        case AmbiWaveTerrainEnvelopeStage::Sustain:
            value = sustain;
            if (!gate) stage = AmbiWaveTerrainEnvelopeStage::Release;
            break;
        case AmbiWaveTerrainEnvelopeStage::Release:
            value += (0.0f - value) * release;
            if (value < 0.00005f) { value = 0.0f; stage = AmbiWaveTerrainEnvelopeStage::Idle; }
            break;
        case AmbiWaveTerrainEnvelopeStage::Idle:
        default: value = 0.0f; break;
        }
        return value;
    }
};

struct AmbiWaveTerrainTable {
    std::array<std::array<float, kAmbiWaveTerrainTableSize>, kAmbiWaveTerrainMipLevels> levels {};
    std::array<float, kAmbiWaveTerrainTableSize> terrainProfile {};
    float traversalLength = 4.0f;
    AmbiWaveTerrainRegion region {};
};

struct AmbiWaveTerrainVoice {
    AmbiWaveTerrainTable current {};
    AmbiWaveTerrainTable next {};
    AmbiWaveTerrainEnvelope freeEnvelope {};
    AmbiWaveTerrainEnvelope midiEnvelope {};
    float freePhase = 0.0f;
    float midiPhase = 0.0f;
    float transition = 1.0f;
    float transitionStep = 1.0f;
    float eventSamples = 1.0f;
    float energy = 0.0f;
    float age = 0.0f;
    float velocity = 0.8f;
    float azimuthPhase = 0.0f;
    float elevationPhase = 0.0f;
    std::array<float, kAmbiWaveTerrainMaxChannels> freeSpatial {};
    std::array<float, kAmbiWaveTerrainMaxChannels> midiSpatial {};
    std::array<float, kAmbiWaveTerrainMaxChannels> centerSpatial {};
    uint32_t randomState = 1u;
    uint32_t seriesCursor = 0u;
    int midiNote = 60;
    bool midiGate = false;
    bool fieldActive = true;
};

class AmbiWaveTerrainEncoder {
public:
    ~AmbiWaveTerrainEncoder()
    {
#if defined(__APPLE__)
        if (fftSetup_) vDSP_destroy_fftsetup(fftSetup_);
#endif
    }

    AmbiWaveTerrainEncoder() = default;
    AmbiWaveTerrainEncoder(const AmbiWaveTerrainEncoder&) = delete;
    AmbiWaveTerrainEncoder& operator=(const AmbiWaveTerrainEncoder&) = delete;

    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        fieldListener_.prepare(sampleRate_);
        fieldListener_.setMemorySeconds(0.48f);
        const auto& directions = ambiFieldListenerCubeDirections();
        fieldListener_.setDirections(directions.data(), static_cast<uint32_t>(directions.size()));
#if defined(__APPLE__)
        if (!fftSetup_) fftSetup_ = vDSP_create_fftsetup(9u, kFFTRadix2);
#endif
        reset();
    }

    void reset()
    {
        fieldListener_.reset();
        macroU_ = 0.5f;
        macroV_ = 0.5f;
        macroStartU_ = macroU_;
        macroStartV_ = macroV_;
        macroTargetU_ = 0.67f;
        macroTargetV_ = 0.38f;
        macroPhase_ = 0.0f;
        dirtyVoice_ = kAmbiWaveTerrainMaxVoices;
        for (uint32_t voice = 0; voice < kAmbiWaveTerrainMaxVoices; ++voice) {
            auto& v = voices_[voice];
            v = {};
            v.randomState = hash(voice + 1u);
            v.freePhase = fract(static_cast<float>(voice) * 0.61803398875f);
            v.midiPhase = fract(static_cast<float>(voice) * 0.38196601125f);
            v.azimuthPhase = fract(static_cast<float>(voice) * 0.754877666f);
            v.elevationPhase = fract(static_cast<float>(voice) * 0.569840296f + 0.25f);
            v.midiNote = static_cast<int>(params_.baseNote);
            const AmbiWaveTerrainRegion region = initialRegion(voice);
            renderTable(v.current, region);
            v.next = v.current;
            v.eventSamples = params_.motionMode == AmbiWaveTerrainMotionMode::Rotate
                ? 1.0f + static_cast<float>(voice) * 0.003f * static_cast<float>(sampleRate_)
                : eventDurationSamples(v, true) + static_cast<float>(voice) * 0.017f * static_cast<float>(sampleRate_);
            points_[voice] = pointForRegion(region);
            updateSpatialTarget(v, points_[voice]);
            previousPoints_[voice] = points_[voice];
            fieldActive_[voice] = 1u;
            neighbor_[voice] = (voice + 1u) % kAmbiWaveTerrainMaxVoices;
        }
    }

    void setParams(AmbiWaveTerrainParams params)
    {
        params.order = std::clamp<uint32_t>(params.order, 1u, kAmbiWaveTerrainMaxOrder);
        params.voices = std::clamp<uint32_t>(params.voices, 1u, kAmbiWaveTerrainMaxVoices);
        params.mode = static_cast<AmbiWaveTerrainMode>(std::clamp<uint32_t>(static_cast<uint32_t>(params.mode), 0u, 2u));
        params.baseNote = clamp(params.baseNote, 12.0f, 96.0f);
        params.pitchSpreadSemitones = clamp(params.pitchSpreadSemitones, 0.0f, 48.0f);
        params.tuneCents = clamp(params.tuneCents, -1200.0f, 1200.0f);
        params.detuneCents = clamp(params.detuneCents, 0.0f, 100.0f);
        params.skin = static_cast<AmbiWaveTerrainSkin>(std::clamp<uint32_t>(static_cast<uint32_t>(params.skin), 0u, 7u));
        params.terrainDepth = clamp(params.terrainDepth, 0.0f, 1.0f);
        params.terrainRoughness = clamp(params.terrainRoughness, 0.0f, 1.0f);
        params.terrainFold = clamp(params.terrainFold, 0.0f, 1.0f);
        params.terrainRelief = clamp(params.terrainRelief, 0.0f, 1.0f);
        params.trace = static_cast<AmbiWaveTerrainTrace>(std::clamp<uint32_t>(static_cast<uint32_t>(params.trace), 0u, 4u));
        params.interpretation = static_cast<AmbiWaveTerrainInterpretation>(std::clamp<uint32_t>(static_cast<uint32_t>(params.interpretation), 0u, 9u));
        params.interpretationMix = clamp(params.interpretationMix, 0.0f, 1.0f);
        params.scanRadius = clamp(params.scanRadius, 0.005f, 0.48f);
        params.scanAspect = clamp(params.scanAspect, 0.05f, 1.0f);
        params.scanRotation = clamp(params.scanRotation, -1.0f, 1.0f);
        params.scanWarp = clamp(params.scanWarp, 0.0f, 1.0f);
        params.selection = static_cast<AmbiWaveTerrainSelection>(std::clamp<uint32_t>(static_cast<uint32_t>(params.selection), 0u, 5u));
        params.transition = static_cast<AmbiWaveTerrainTransition>(std::clamp<uint32_t>(static_cast<uint32_t>(params.transition), 0u, 2u));
        params.fieldDensity = clamp(params.fieldDensity, 0.0f, 1.0f);
        params.fieldDurationSeconds = clamp(params.fieldDurationSeconds, 0.05f, 30.0f);
        params.fieldRestSeconds = clamp(params.fieldRestSeconds, 0.02f, 8.0f);
        params.fieldContrast = clamp(params.fieldContrast, 0.0f, 1.0f);
        params.selectionMemory = clamp(params.selectionMemory, 0.0f, 1.0f);
        params.regionDeviation = clamp(params.regionDeviation, 0.0f, 1.0f);
        params.neighborTransfer = clamp(params.neighborTransfer, 0.0f, 1.0f);
        params.macroDurationSeconds = clamp(params.macroDurationSeconds, 2.0f, 300.0f);
        params.tableXfadeMs = clamp(params.tableXfadeMs, 5.0f, 5000.0f);
        params.attackMs = clamp(params.attackMs, 1.0f, 4000.0f);
        params.decayMs = clamp(params.decayMs, 5.0f, 8000.0f);
        params.sustain = clamp(params.sustain, 0.0f, 1.0f);
        params.releaseMs = clamp(params.releaseMs, 5.0f, 12000.0f);
        params.centerAzimuthDeg = wrapSignedDeg(params.centerAzimuthDeg);
        params.centerElevationDeg = clamp(params.centerElevationDeg, -90.0f, 90.0f);
        params.centerDistance = clamp(params.centerDistance, 0.15f, 3.0f);
        params.spatialSpread = clamp(params.spatialSpread, 0.0f, 1.0f);
        params.spatialFollow = clamp(params.spatialFollow, 0.0f, 0.995f);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
        params.pitchMode = static_cast<AmbiWaveTerrainPitchMode>(
            std::clamp<uint32_t>(static_cast<uint32_t>(params.pitchMode), 0u, 1u));
        params.motionMode = static_cast<AmbiWaveTerrainMotionMode>(
            std::clamp<uint32_t>(static_cast<uint32_t>(params.motionMode), 0u, 1u));
        params.azimuthRateRpm = clamp(params.azimuthRateRpm, -12.0f, 12.0f);
        params.elevationRateRpm = clamp(params.elevationRateRpm, -12.0f, 12.0f);
        params.rotationRateDeviation = clamp(params.rotationRateDeviation, 0.0f, 1.0f);
        params.pitchScale = static_cast<AmbiWaveTerrainPitchScale>(
            std::clamp<uint32_t>(static_cast<uint32_t>(params.pitchScale), 0u, 6u));
        params.terrainForm = static_cast<AmbiWaveTerrainForm>(
            std::clamp<uint32_t>(static_cast<uint32_t>(params.terrainForm), 0u, 5u));
        params.terrainFacet = clamp(params.terrainFacet, 0.0f, 1.0f);
        params.terrainBevel = clamp(params.terrainBevel, 0.0f, 1.0f);
        params.terrainOrientation = clamp(params.terrainOrientation, -1.0f, 1.0f);
        params.terrainTerrace = clamp(params.terrainTerrace, 0.0f, 1.0f);
        params.terrainTerraceSteps = std::clamp<uint32_t>(params.terrainTerraceSteps, 2u, 24u);
        params.terrainRidge = clamp(params.terrainRidge, 0.0f, 1.0f);
        params.terrainErosion = clamp(params.terrainErosion, 0.0f, 1.0f);
        params.terrainDomainWarp = clamp(params.terrainDomainWarp, 0.0f, 1.0f);
        params.terrainTwist = clamp(params.terrainTwist, -1.0f, 1.0f);
        params.polygonSides = std::clamp<uint32_t>(params.polygonSides, 3u, 12u);
        params.polygonRound = clamp(params.polygonRound, 0.0f, 1.0f);
        params.polygonStar = clamp(params.polygonStar, 0.0f, 1.0f);
        params.polygonSkew = clamp(params.polygonSkew, -1.0f, 1.0f);
        params.fieldListenMode = sanitizeAmbiFieldListenMode(params.fieldListenMode);

        const bool motionModeChanged = params.motionMode != params_.motionMode;
        const bool terrainChanged = params.skin != params_.skin
            || params.terrainDepth != params_.terrainDepth
            || params.terrainRoughness != params_.terrainRoughness
            || params.terrainFold != params_.terrainFold
            || params.terrainRelief != params_.terrainRelief
            || params.trace != params_.trace
            || params.interpretation != params_.interpretation
            || params.interpretationMix != params_.interpretationMix
            || params.scanRadius != params_.scanRadius
            || params.scanAspect != params_.scanAspect
            || params.scanRotation != params_.scanRotation
            || params.scanWarp != params_.scanWarp
            || params.terrainForm != params_.terrainForm
            || params.terrainFacet != params_.terrainFacet
            || params.terrainBevel != params_.terrainBevel
            || params.terrainOrientation != params_.terrainOrientation
            || params.terrainTerrace != params_.terrainTerrace
            || params.terrainTerraceSteps != params_.terrainTerraceSteps
            || params.terrainRidge != params_.terrainRidge
            || params.terrainErosion != params_.terrainErosion
            || params.terrainDomainWarp != params_.terrainDomainWarp
            || params.terrainTwist != params_.terrainTwist
            || params.polygonSides != params_.polygonSides
            || params.polygonRound != params_.polygonRound
            || params.polygonStar != params_.polygonStar
            || params.polygonSkew != params_.polygonSkew
            || params.centerAzimuthDeg != params_.centerAzimuthDeg
            || params.centerElevationDeg != params_.centerElevationDeg
            || params.centerDistance != params_.centerDistance
            || params.spatialSpread != params_.spatialSpread;
        params_ = params;
        if (terrainChanged) dirtyVoice_ = 0u;
        if (motionModeChanged) {
            for (uint32_t voice = 0; voice < params_.voices; ++voice) voices_[voice].eventSamples = 1.0f + static_cast<float>(voice) * 64.0f;
        }
    }

    AmbiWaveTerrainParams params() const { return params_; }
    const std::array<AmbiWaveTerrainPoint, kAmbiWaveTerrainMaxVoices>& points() const { return points_; }
    const std::array<uint8_t, kAmbiWaveTerrainMaxVoices>& fieldActive() const { return fieldActive_; }
    const std::array<uint32_t, kAmbiWaveTerrainMaxVoices>& neighbors() const { return neighbor_; }
    float voiceEnergy(uint32_t voice) const { return voices_[std::min<uint32_t>(voice, kAmbiWaveTerrainMaxVoices - 1u)].energy; }
    float voiceTransition(uint32_t voice) const { return voices_[std::min<uint32_t>(voice, kAmbiWaveTerrainMaxVoices - 1u)].transition; }
    float voiceTraversalLength(uint32_t voice, bool next = true) const
    {
        const auto& v = voices_[std::min<uint32_t>(voice, kAmbiWaveTerrainMaxVoices - 1u)];
        return next ? v.next.traversalLength : v.current.traversalLength;
    }
    float voiceFrequencyHz(uint32_t voice) const
    {
        voice = std::min<uint32_t>(voice, kAmbiWaveTerrainMaxVoices - 1u);
        return frequencyForVoice(params_.baseNote, voice, false);
    }
    float voiceScanPhase(uint32_t voice) const
    {
        const auto& v = voices_[std::min<uint32_t>(voice, kAmbiWaveTerrainMaxVoices - 1u)];
        const bool midiVisible = params_.mode == AmbiWaveTerrainMode::Midi
            || (params_.mode == AmbiWaveTerrainMode::Both && v.midiEnvelope.stage != AmbiWaveTerrainEnvelopeStage::Idle);
        return midiVisible ? v.midiPhase : v.freePhase;
    }
    AmbiWaveTerrainRegion voiceRegion(uint32_t voice, bool next = false) const
    {
        const auto& v = voices_[std::min<uint32_t>(voice, kAmbiWaveTerrainMaxVoices - 1u)];
        return next ? v.next.region : v.current.region;
    }
    float tableValue(uint32_t voice, bool next, uint32_t index, uint32_t level = 0u) const
    {
        const auto& v = voices_[std::min<uint32_t>(voice, kAmbiWaveTerrainMaxVoices - 1u)];
        return (next ? v.next : v.current).levels[std::min<uint32_t>(level, kAmbiWaveTerrainMipLevels - 1u)]
            [index % kAmbiWaveTerrainTableSize];
    }
    float terrainProfileValue(uint32_t voice, bool next, uint32_t index) const
    {
        const auto& v = voices_[std::min<uint32_t>(voice, kAmbiWaveTerrainMaxVoices - 1u)];
        return (next ? v.next : v.current).terrainProfile[index % kAmbiWaveTerrainTableSize];
    }

    float terrainHeight(float u, float v) const { return terrain(fract(u), clamp(v, 0.0f, 1.0f)); }
    float fieldListenEnvelope(uint32_t lobe) const { return fieldListener_.envelope(lobe); }
    float fieldListenActivity() const { return fieldListener_.activity(); }
    std::array<float, 2> contourPoint(const AmbiWaveTerrainRegion& region, float phase) const
    {
        return contour(region, phase);
    }

    AmbiWaveTerrainPoint surfacePoint(float u, float v) const
    {
        const float h = terrainHeight(u, v);
        AmbiWaveTerrainPoint p {};
        p.azimuthDeg = wrapSignedDeg(params_.centerAzimuthDeg + (fract(u) - 0.5f) * 360.0f * params_.spatialSpread);
        p.elevationDeg = clamp(params_.centerElevationDeg + (clamp(v, 0.0f, 1.0f) - 0.5f) * 180.0f * params_.spatialSpread, -90.0f, 90.0f);
        p.distance = clamp(params_.centerDistance + h * params_.terrainDepth * params_.terrainRelief * 0.42f, 0.15f, 3.0f);
        p.terrain = h;
        return p;
    }

    void noteOn(int note, float velocity)
    {
        uint32_t best = 0u;
        float oldest = -1.0f;
        for (uint32_t voice = 0; voice < params_.voices; ++voice) {
            const auto& v = voices_[voice];
            if (!v.midiGate && v.midiEnvelope.value < 0.0001f) { best = voice; break; }
            if (v.age > oldest) { oldest = v.age; best = voice; }
        }
        auto& v = voices_[best];
        v.midiNote = note;
        v.velocity = clamp(velocity, 0.0f, 1.0f);
        v.midiGate = true;
        v.age = 0.0f;
        v.midiEnvelope.setGate(true);
    }

    void noteOff(int note)
    {
        for (auto& voice : voices_) {
            if (voice.midiNote == note) {
                voice.midiGate = false;
                voice.midiEnvelope.setGate(false);
            }
        }
    }

    void process(float* const* outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiWaveTerrainMaxChannels);
        for (uint32_t channel = 0; channel < outputChannels; ++channel) {
            if (outputs[channel]) std::fill(outputs[channel], outputs[channel] + frames, 0.0f);
        }
        if (outputChannels == 0u) return;

        refreshDirtyVoice();
        const uint32_t ambiChannels = std::min<uint32_t>(ambiChannelsForOrder(params_.order), outputChannels);
        const bool freeMode = params_.mode == AmbiWaveTerrainMode::Free || params_.mode == AmbiWaveTerrainMode::Both;
        const bool midiMode = params_.mode == AmbiWaveTerrainMode::Midi || params_.mode == AmbiWaveTerrainMode::Both;
        const float layerGain = params_.mode == AmbiWaveTerrainMode::Both ? 0.70710678f : 1.0f;
        const float gain = dbToGain(params_.outputGainDb) * layerGain
            / std::sqrt(static_cast<float>(std::max<uint32_t>(1u, params_.voices)));
        const float attack = envelopeCoefficient(params_.attackMs);
        const float decay = envelopeCoefficient(params_.decayMs);
        const float release = envelopeCoefficient(params_.releaseMs);
        const float spatialCoefficient = clamp(1.0f - params_.spatialFollow, 0.001f, 1.0f);

        constexpr uint32_t kChunkFrames = 16u;
        for (uint32_t chunkStart = 0; chunkStart < frames; chunkStart += kChunkFrames) {
            const uint32_t chunkFrames = std::min<uint32_t>(kChunkFrames, frames - chunkStart);
            updateMacro(chunkFrames);
            for (uint32_t voice = 0; voice < params_.voices; ++voice) {
                updateField(voice, chunkFrames);
                updatePoint(voice);
            }

            for (uint32_t frame = chunkStart; frame < chunkStart + chunkFrames; ++frame) {
                std::array<float, kAmbiWaveTerrainMaxChannels> listenerFrame {};
                for (uint32_t voice = 0; voice < params_.voices; ++voice) {
                    auto& v = voices_[voice];
                    const float activity = v.fieldActive ? 1.0f : 1.0f - params_.fieldContrast;
                    float voiceSample = 0.0f;
                    const auto renderLayer = [&](float& phase, float frequency, float amplitude,
                                                 std::array<float, kAmbiWaveTerrainMaxChannels>& smoothedSpatial) {
                        const auto scan = oscillator(v, phase, frequency);
                        const float transition = v.transition;
                        const float sample = lerp(scan.current, scan.next, transition);
                        voiceSample += sample * amplitude;
                        for (uint32_t channel = 0; channel < ambiChannels; ++channel) {
                            const float targetBasis = v.centerSpatial[channel];
                            smoothedSpatial[channel] += (targetBasis - smoothedSpatial[channel]) * spatialCoefficient;
                            listenerFrame[channel] += sample * amplitude * activity * smoothedSpatial[channel];
                            if (outputs[channel]) {
                                outputs[channel][frame] = flushDenormal(outputs[channel][frame]
                                    + sample * amplitude * gain * activity * smoothedSpatial[channel]);
                            }
                        }
                    };
                    if (freeMode) {
                        v.freeEnvelope.setGate(v.fieldActive || params_.fieldContrast < 0.999f);
                        const float env = v.freeEnvelope.process(attack, decay, params_.sustain, release);
                        renderLayer(v.freePhase, frequencyForVoice(params_.baseNote, voice, false), env * 0.78f, v.freeSpatial);
                    } else {
                        v.freeEnvelope.setGate(false);
                        v.freeEnvelope.process(attack, decay, params_.sustain, release);
                    }
                    if (midiMode) {
                        v.midiEnvelope.setGate(v.midiGate);
                        const float env = v.midiEnvelope.process(attack, decay, params_.sustain, release);
                        renderLayer(v.midiPhase, frequencyForVoice(static_cast<float>(v.midiNote), voice, true), env * v.velocity, v.midiSpatial);
                        if (v.midiEnvelope.stage != AmbiWaveTerrainEnvelopeStage::Idle) v.age += 1.0f / static_cast<float>(sampleRate_);
                    }
                    if (v.transition < 1.0f) v.transition = std::min(1.0f, v.transition + v.transitionStep);
                    v.energy += (std::fabs(voiceSample * gain * activity) - v.energy) * 0.025f;
                }
                fieldListener_.processFrame(listenerFrame.data(), ambiChannels);
            }
        }
        for (uint32_t channel = 0; channel < ambiChannels; ++channel) {
            if (!outputs[channel]) continue;
            for (uint32_t frame = 0; frame < frames; ++frame) outputs[channel][frame] = softSat(outputs[channel][frame]);
        }
    }

private:
    static float fract(float value) { return value - std::floor(value); }
    static float lerpWrappedUnit(float from, float to, float amount)
    {
        float delta = to - from;
        if (delta > 0.5f) delta -= 1.0f;
        else if (delta < -0.5f) delta += 1.0f;
        return fract(from + delta * amount);
    }
    static float wrapSignedDeg(float value)
    {
        while (value > 180.0f) value -= 360.0f;
        while (value <= -180.0f) value += 360.0f;
        return value;
    }
    static uint32_t hash(uint32_t value)
    {
        value ^= value >> 16u; value *= 0x7feb352du;
        value ^= value >> 15u; value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value ? value : 1u;
    }
    static float deterministicSigned(uint32_t value)
    {
        return static_cast<float>(hash(value) & 0x00ffffffu) / 8388607.5f - 1.0f;
    }
    static float cell(float x, float y, float z)
    {
        const float dx = x - std::round(x);
        const float dy = y - std::round(y);
        const float dz = z - std::round(z);
        return 1.0f - clamp(std::sqrt(dx * dx + dy * dy + dz * dz) * 1.35f, 0.0f, 1.0f);
    }

    float randomUnit(AmbiWaveTerrainVoice& voice) const
    {
        voice.randomState = voice.randomState * 1664525u + 1013904223u;
        return static_cast<float>((voice.randomState >> 8u) & 0x00ffffffu) / 16777215.0f;
    }
    float randomSigned(AmbiWaveTerrainVoice& voice) const { return randomUnit(voice) * 2.0f - 1.0f; }

    float envelopeCoefficient(float milliseconds) const
    {
        const float samples = std::max(1.0f, milliseconds * 0.001f * static_cast<float>(sampleRate_));
        return 1.0f - std::exp(-6.90775528f / samples);
    }

    float terrainLayer(float u, float v, uint32_t layer) const
    {
        const Vec3 direction = directionFromAed((u - 0.5f) * 360.0f, (v - 0.5f) * 180.0f);
        const float frequency = 1.15f + static_cast<float>(layer) * (0.68f + params_.terrainRoughness * 0.48f);
        const float x = direction.x * frequency + static_cast<float>(layer) * 0.31f;
        const float y = direction.y * (frequency + 0.37f) - static_cast<float>(layer) * 0.17f;
        const float z = direction.z * (frequency + 0.71f) + static_cast<float>(layer) * 0.23f;
        const float harmonic = std::sin(kPi * (x + 0.31f * z)) * std::cos(kPi * (y - 0.23f * z));
        const float diagonal = std::sin(kPi * (x * 0.73f + y * 0.91f + z * 1.17f));
        const float cellularRaw = cell(x * 1.55f + layer * 0.37f, y * 1.55f, z * 1.55f);
        const float cellular = cellularRaw * 2.0f - 1.0f;
        const float fbm = 0.55f * harmonic + 0.32f * diagonal
            + 0.13f * std::sin(kPi * (x * 2.3f - y * 1.7f + z * 0.41f));
        const float ridges = 1.0f - 2.0f * std::fabs(fbm);
        const float dunes = std::sin(kAmbiWaveTerrainTwoPi * (0.19f * x + 0.08f * std::sin(kPi * (y + z))))
            * (0.72f + 0.28f * std::cos(kPi * z));
        const float craterRim = std::exp(-std::pow((cellularRaw - 0.52f) * 7.0f, 2.0f)) * 2.0f - 1.0f;
        const float craters = clamp(0.70f * craterRim - 0.72f * cellularRaw + 0.18f * harmonic, -1.0f, 1.0f);
        const float tectonic = softSat(1.55f * diagonal + 0.75f * (std::fabs(harmonic) * 2.0f - 1.0f));
        switch (params_.skin) {
        case AmbiWaveTerrainSkin::Harmonic: return 0.70f * harmonic + 0.30f * diagonal;
        case AmbiWaveTerrainSkin::Cellular: return 0.75f * cellular + 0.25f * harmonic;
        case AmbiWaveTerrainSkin::Vot: return softSat(1.30f * fbm + 0.45f * cellular);
        case AmbiWaveTerrainSkin::Ridges: return ridges;
        case AmbiWaveTerrainSkin::Dunes: return dunes;
        case AmbiWaveTerrainSkin::Craters: return craters;
        case AmbiWaveTerrainSkin::Tectonic: return tectonic;
        case AmbiWaveTerrainSkin::Fbm:
        default: return fbm;
        }
    }

    float terrainForm(float u, float v) const
    {
        if (params_.terrainForm == AmbiWaveTerrainForm::Sphere) return 0.0f;
        Vec3 direction = directionFromAed((u - 0.5f) * 360.0f, (v - 0.5f) * 180.0f);
        const float yaw = params_.terrainOrientation * kPi;
        const float pitch = params_.terrainOrientation * kPi * 0.37f;
        const float cy = std::cos(yaw), sy = std::sin(yaw);
        const float cp = std::cos(pitch), sp = std::sin(pitch);
        const float x1 = cy * direction.x - sy * direction.y;
        const float y1 = sy * direction.x + cy * direction.y;
        direction = { cp * x1 + sp * direction.z, y1, -sp * x1 + cp * direction.z };

        std::array<float, 20> dots {};
        uint32_t count = 0u;
        const auto addNormal = [&](float x, float y, float z) {
            const float inverseLength = 1.0f / std::sqrt(std::max(0.000001f, x * x + y * y + z * z));
            dots[count++] = direction.x * x * inverseLength + direction.y * y * inverseLength + direction.z * z * inverseLength;
        };
        if (params_.terrainForm == AmbiWaveTerrainForm::Cube) {
            addNormal(1.0f, 0.0f, 0.0f); addNormal(-1.0f, 0.0f, 0.0f);
            addNormal(0.0f, 1.0f, 0.0f); addNormal(0.0f, -1.0f, 0.0f);
            addNormal(0.0f, 0.0f, 1.0f); addNormal(0.0f, 0.0f, -1.0f);
        } else if (params_.terrainForm == AmbiWaveTerrainForm::Octa) {
            for (int x : { -1, 1 }) for (int y : { -1, 1 }) for (int z : { -1, 1 }) {
                addNormal(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
            }
        } else if (params_.terrainForm == AmbiWaveTerrainForm::Tetra) {
            addNormal(1.0f, 1.0f, 1.0f); addNormal(1.0f, -1.0f, -1.0f);
            addNormal(-1.0f, 1.0f, -1.0f); addNormal(-1.0f, -1.0f, 1.0f);
        } else if (params_.terrainForm == AmbiWaveTerrainForm::Dodeca) {
            constexpr float phi = 1.61803398875f;
            for (int a : { -1, 1 }) for (int b : { -1, 1 }) {
                addNormal(0.0f, static_cast<float>(a), static_cast<float>(b) * phi);
                addNormal(static_cast<float>(a), static_cast<float>(b) * phi, 0.0f);
                addNormal(static_cast<float>(a) * phi, 0.0f, static_cast<float>(b));
            }
        } else {
            constexpr float phi = 1.61803398875f;
            constexpr float inversePhi = 0.61803398875f;
            for (int x : { -1, 1 }) for (int y : { -1, 1 }) for (int z : { -1, 1 }) {
                addNormal(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
            }
            for (int a : { -1, 1 }) for (int b : { -1, 1 }) {
                addNormal(0.0f, static_cast<float>(a) * inversePhi, static_cast<float>(b) * phi);
                addNormal(static_cast<float>(a) * inversePhi, static_cast<float>(b) * phi, 0.0f);
                addNormal(static_cast<float>(a) * phi, 0.0f, static_cast<float>(b) * inversePhi);
            }
        }

        float maximum = dots[0];
        for (uint32_t index = 1u; index < count; ++index) maximum = std::max(maximum, dots[index]);
        float support = maximum;
        if (params_.terrainBevel > 0.0001f) {
            const float sharpness = lerp(80.0f, 6.0f, params_.terrainBevel);
            float sum = 0.0f;
            for (uint32_t index = 0u; index < count; ++index) sum += std::exp((dots[index] - maximum) * sharpness);
            support += std::log(std::max(1.0f, sum)) / sharpness;
        }
        const float radial = 1.0f / std::max(0.18f, support);
        return softSat((radial - 1.0f) * 4.0f - 0.28f);
    }

    float terrain(float u, float v) const
    {
        u = fract(u + params_.terrainTwist * (v - 0.5f) * 0.42f);
        if (params_.terrainDomainWarp > 0.0001f) {
            const float warpU = std::sin(kAmbiWaveTerrainTwoPi * (v * 1.73f + std::sin(u * kAmbiWaveTerrainTwoPi) * 0.19f));
            const float warpV = std::sin(kAmbiWaveTerrainTwoPi * (u * 1.31f - std::cos(v * kAmbiWaveTerrainTwoPi) * 0.23f));
            u = fract(u + warpU * params_.terrainDomainWarp * 0.11f);
            v = clamp(v + warpV * params_.terrainDomainWarp * 0.085f, 0.0f, 1.0f);
        }
        float value = 0.0f;
        float weight = 0.0f;
        for (uint32_t layer = 0; layer < 4u; ++layer) {
            const float w = std::pow(0.62f + params_.terrainRoughness * 0.13f, static_cast<float>(layer));
            value += terrainLayer(u, v, layer) * w;
            weight += w;
        }
        value = weight > 0.0f ? value / weight : 0.0f;
        const float folded = std::sin(value * (1.0f + 6.0f * params_.terrainFold));
        value = softSat(lerp(value, folded, params_.terrainFold) * 1.45f);
        const float ridge = 1.0f - 2.0f * std::fabs(value);
        value = lerp(value, ridge, params_.terrainRidge);
        const float eroded = value / (1.0f + params_.terrainErosion * 2.8f * std::fabs(value));
        value = lerp(value, eroded, params_.terrainErosion);
        if (params_.terrainForm != AmbiWaveTerrainForm::Sphere && params_.terrainFacet > 0.0001f) {
            const float form = terrainForm(u, v);
            value = lerp(value, form * 0.88f + value * 0.22f, params_.terrainFacet);
        }
        if (params_.terrainTerrace > 0.0001f) {
            const float steps = static_cast<float>(params_.terrainTerraceSteps);
            const float terraced = std::round(value * steps) / steps;
            value = lerp(value, terraced, params_.terrainTerrace);
        }
        return softSat(value);
    }

    struct ContourSample {
        float u = 0.5f;
        float v = 0.5f;
        float carrier = 0.0f;
    };

    std::array<float, 2> polygonPoint(float phase) const
    {
        const uint32_t sides = std::clamp<uint32_t>(params_.polygonSides, 3u, 12u);
        const float sectorPosition = fract(phase) * static_cast<float>(sides);
        const uint32_t sector = static_cast<uint32_t>(std::floor(sectorPosition)) % sides;
        const float local = sectorPosition - std::floor(sectorPosition);
        const float angle0 = kAmbiWaveTerrainTwoPi * static_cast<float>(sector) / static_cast<float>(sides);
        const float angle1 = kAmbiWaveTerrainTwoPi * static_cast<float>(sector + 1u) / static_cast<float>(sides);
        const std::array<float, 2> outer0 { std::cos(angle0), std::sin(angle0) };
        const std::array<float, 2> outer1 { std::cos(angle1), std::sin(angle1) };
        const std::array<float, 2> edgeMidpoint {
            (outer0[0] + outer1[0]) * 0.5f,
            (outer0[1] + outer1[1]) * 0.5f,
        };
        const float middleAngle = (angle0 + angle1) * 0.5f;
        const float innerRadius = 1.0f - params_.polygonStar * 0.78f;
        const std::array<float, 2> starPoint {
            std::cos(middleAngle) * innerRadius,
            std::sin(middleAngle) * innerRadius,
        };
        const std::array<float, 2> inner {
            lerp(edgeMidpoint[0], starPoint[0], params_.polygonStar),
            lerp(edgeMidpoint[1], starPoint[1], params_.polygonStar),
        };
        std::array<float, 2> polygon {};
        if (local < 0.5f) {
            polygon = { lerp(outer0[0], inner[0], local * 2.0f), lerp(outer0[1], inner[1], local * 2.0f) };
        } else {
            polygon = { lerp(inner[0], outer1[0], (local - 0.5f) * 2.0f), lerp(inner[1], outer1[1], (local - 0.5f) * 2.0f) };
        }
        const float angle = kAmbiWaveTerrainTwoPi * fract(phase);
        polygon[0] = lerp(polygon[0], std::cos(angle), params_.polygonRound);
        polygon[1] = lerp(polygon[1], std::sin(angle), params_.polygonRound);
        polygon[0] += polygon[1] * params_.polygonSkew * 0.68f;
        return polygon;
    }

    ContourSample contourSample(const AmbiWaveTerrainRegion& region, float phase) const
    {
        const float angle = kAmbiWaveTerrainTwoPi * phase;
        float x = 0.0f;
        float y = 0.0f;
        switch (region.trace) {
        case AmbiWaveTerrainTrace::Orbit:
            x = std::cos(angle); y = std::sin(angle); break;
        case AmbiWaveTerrainTrace::Polygon: {
            const auto point = polygonPoint(phase);
            x = point[0]; y = point[1]; break;
        }
        case AmbiWaveTerrainTrace::Rosette:
            x = std::cos(angle) * (0.58f + 0.42f * std::cos(angle * 5.0f));
            y = std::sin(angle) * (0.58f + 0.42f * std::cos(angle * 5.0f));
            break;
        case AmbiWaveTerrainTrace::Fold:
            x = std::sin(angle);
            y = std::asin(clamp(std::sin(angle * 2.0f + x * 1.4f), -1.0f, 1.0f)) / (kPi * 0.5f);
            break;
        case AmbiWaveTerrainTrace::Lissajous:
        default:
            x = std::sin(angle * 2.0f + kPi * 0.5f);
            y = std::sin(angle * 3.0f);
            break;
        }
        x += std::sin(angle * 7.0f) * params_.scanWarp * 0.16f;
        y += std::cos(angle * 5.0f) * params_.scanWarp * 0.16f;
        const float ca = std::cos(region.rotation * kPi);
        const float sa = std::sin(region.rotation * kPi);
        const float ex = x * region.radius;
        const float ey = y * region.radius * region.aspect;
        const float du = ex * ca - ey * sa;
        const float dv = ex * sa + ey * ca;
        return { fract(region.u + du), clamp(region.v + dv, 0.0f, 1.0f), clamp(dv / std::max(0.005f, region.radius), -1.0f, 1.0f) };
    }

    std::array<float, 2> contour(const AmbiWaveTerrainRegion& region, float phase) const
    {
        const auto sample = contourSample(region, phase);
        return { sample.u, sample.v };
    }

    void buildBandLimitedLevels(AmbiWaveTerrainTable& table) const
    {
        static constexpr std::array<uint32_t, kAmbiWaveTerrainMipLevels - 1u> cutoffs {{ 96u, 48u, 24u, 12u }};

#if defined(__APPLE__)
        if (fftSetup_) {
            std::array<float, kAmbiWaveTerrainTableSize> spectrumReal {};
            std::array<float, kAmbiWaveTerrainTableSize> spectrumImag {};
            std::array<float, kAmbiWaveTerrainTableSize> workReal {};
            std::array<float, kAmbiWaveTerrainTableSize> workImag {};
            std::copy(table.levels[0].begin(), table.levels[0].end(), spectrumReal.begin());
            DSPSplitComplex spectrum { spectrumReal.data(), spectrumImag.data() };
            vDSP_fft_zip(fftSetup_, &spectrum, 1, 9u, FFT_FORWARD);

            for (uint32_t level = 1u; level < kAmbiWaveTerrainMipLevels; ++level) {
                workReal = spectrumReal;
                workImag = spectrumImag;
                const uint32_t cutoff = cutoffs[level - 1u];
                const float taperStart = static_cast<float>(cutoff) * 0.82f;
                for (uint32_t bin = 0u; bin < kAmbiWaveTerrainTableSize; ++bin) {
                    const uint32_t harmonic = std::min(bin, kAmbiWaveTerrainTableSize - bin);
                    float taper = harmonic > 0u && harmonic <= cutoff ? 1.0f : 0.0f;
                    if (taper > 0.0f && static_cast<float>(harmonic) > taperStart) {
                        const float amount = (static_cast<float>(harmonic) - taperStart)
                            / std::max(1.0f, static_cast<float>(cutoff) - taperStart);
                        taper = 0.5f + 0.5f * std::cos(clamp(amount, 0.0f, 1.0f) * kPi);
                    }
                    workReal[bin] *= taper;
                    workImag[bin] *= taper;
                }
                DSPSplitComplex work { workReal.data(), workImag.data() };
                vDSP_fft_zip(fftSetup_, &work, 1, 9u, FFT_INVERSE);
                const float inverseSize = 1.0f / static_cast<float>(kAmbiWaveTerrainTableSize);
                for (uint32_t index = 0u; index < kAmbiWaveTerrainTableSize; ++index)
                    table.levels[level][index] = workReal[index] * inverseSize;
            }
            return;
        }
#endif

        constexpr uint32_t kMaximumBandHarmonic = 96u;
        std::array<float, kMaximumBandHarmonic + 1u> cosineCoefficients {};
        std::array<float, kMaximumBandHarmonic + 1u> sineCoefficients {};
        constexpr float kCoefficientScale = 2.0f / static_cast<float>(kAmbiWaveTerrainTableSize);
        for (uint32_t harmonic = 1u; harmonic <= kMaximumBandHarmonic; ++harmonic) {
            const float step = kAmbiWaveTerrainTwoPi * static_cast<float>(harmonic)
                / static_cast<float>(kAmbiWaveTerrainTableSize);
            const float cosineStep = std::cos(step), sineStep = std::sin(step);
            float cosine = 1.0f, sine = 0.0f;
            float cosineSum = 0.0f, sineSum = 0.0f;
            for (uint32_t index = 0u; index < kAmbiWaveTerrainTableSize; ++index) {
                cosineSum += table.levels[0][index] * cosine;
                sineSum += table.levels[0][index] * sine;
                const float nextCosine = cosine * cosineStep - sine * sineStep;
                sine = sine * cosineStep + cosine * sineStep;
                cosine = nextCosine;
            }
            cosineCoefficients[harmonic] = cosineSum * kCoefficientScale;
            sineCoefficients[harmonic] = sineSum * kCoefficientScale;
        }
        for (uint32_t level = 1u; level < kAmbiWaveTerrainMipLevels; ++level) {
            auto& target = table.levels[level];
            const uint32_t cutoff = cutoffs[level - 1u];
            const float taperStart = static_cast<float>(cutoff) * 0.82f;
            for (uint32_t index = 0u; index < kAmbiWaveTerrainTableSize; ++index) {
                const float angle = kAmbiWaveTerrainTwoPi * static_cast<float>(index)
                    / static_cast<float>(kAmbiWaveTerrainTableSize);
                const float cosineStep = std::cos(angle), sineStep = std::sin(angle);
                float cosine = cosineStep, sine = sineStep;
                float value = 0.0f;
                for (uint32_t harmonic = 1u; harmonic <= cutoff; ++harmonic) {
                    float taper = 1.0f;
                    if (static_cast<float>(harmonic) > taperStart) {
                        const float amount = (static_cast<float>(harmonic) - taperStart)
                            / std::max(1.0f, static_cast<float>(cutoff) - taperStart);
                        taper = 0.5f + 0.5f * std::cos(clamp(amount, 0.0f, 1.0f) * kPi);
                    }
                    value += (cosineCoefficients[harmonic] * cosine + sineCoefficients[harmonic] * sine) * taper;
                    const float nextCosine = cosine * cosineStep - sine * sineStep;
                    sine = sine * cosineStep + cosine * sineStep;
                    cosine = nextCosine;
                }
                target[index] = value;
            }
        }
    }

    void renderTable(AmbiWaveTerrainTable& table, const AmbiWaveTerrainRegion& region) const
    {
        table.region = region;
        const float terrainAmount = params_.terrainDepth * params_.terrainRelief;
        const bool needsGradient = params_.interpretation == AmbiWaveTerrainInterpretation::Gradient
            || params_.interpretation == AmbiWaveTerrainInterpretation::Normal
            || params_.interpretation == AmbiWaveTerrainInterpretation::Vector;
        const bool needsCross = params_.interpretation == AmbiWaveTerrainInterpretation::Cross
            || params_.interpretation == AmbiWaveTerrainInterpretation::Vector;
        std::array<float, kAmbiWaveTerrainTableSize> gradientProfile {};
        std::array<float, kAmbiWaveTerrainTableSize> normalProfile {};
        std::array<float, kAmbiWaveTerrainTableSize> crossProfile {};
        constexpr float kGradientOffset = 0.0025f;
        constexpr float kPhaseStep = 1.0f / static_cast<float>(kAmbiWaveTerrainTableSize);
        for (uint32_t index = 0; index < kAmbiWaveTerrainTableSize; ++index) {
            const float phase = static_cast<float>(index) / static_cast<float>(kAmbiWaveTerrainTableSize);
            const auto sample = contourSample(region, phase);
            const float terrainValue = terrain(sample.u, sample.v);
            table.terrainProfile[index] = lerp(sample.carrier, terrainValue, terrainAmount);
            if (needsGradient) {
                const float dx = (terrain(fract(sample.u + kGradientOffset), sample.v)
                    - terrain(fract(sample.u - kGradientOffset), sample.v)) * 0.5f;
                const float dy = (terrain(sample.u, clamp(sample.v + kGradientOffset, 0.0f, 1.0f))
                    - terrain(sample.u, clamp(sample.v - kGradientOffset, 0.0f, 1.0f))) * 0.5f;
                const float gradient = std::sqrt(dx * dx + dy * dy);
                gradientProfile[index] = softSat(gradient * 3.5f) * terrainAmount;
                normalProfile[index] = (2.0f / std::sqrt(1.0f + gradient * gradient * 96.0f) - 1.0f) * terrainAmount;
            }
            if (needsCross) {
                const auto before = contourSample(region, phase - kPhaseStep);
                const auto after = contourSample(region, phase + kPhaseStep);
                float tangentU = after.u - before.u;
                if (tangentU > 0.5f) tangentU -= 1.0f;
                else if (tangentU < -0.5f) tangentU += 1.0f;
                const float tangentV = after.v - before.v;
                const float inverseTangent = 1.0f / std::sqrt(std::max(0.0000001f, tangentU * tangentU + tangentV * tangentV));
                const float offsetU = -tangentV * inverseTangent * region.radius * 0.11f;
                const float offsetV = tangentU * inverseTangent * region.radius * 0.11f;
                const float acrossA = terrain(fract(sample.u + offsetU), clamp(sample.v + offsetV, 0.0f, 1.0f));
                const float acrossB = terrain(fract(sample.u - offsetU), clamp(sample.v - offsetV, 0.0f, 1.0f));
                crossProfile[index] = softSat((acrossA - acrossB) * 1.8f) * terrainAmount;
            }
        }

        constexpr float kDerivativeScale = static_cast<float>(kAmbiWaveTerrainTableSize) / kAmbiWaveTerrainTwoPi;
        float mean = 0.0f;
        for (uint32_t index = 0; index < kAmbiWaveTerrainTableSize; ++index) {
            const uint32_t previous = (index + kAmbiWaveTerrainTableSize - 1u) % kAmbiWaveTerrainTableSize;
            const uint32_t next = (index + 1u) % kAmbiWaveTerrainTableSize;
            const float height = table.terrainProfile[index];
            const float edge = softSat((table.terrainProfile[next] - table.terrainProfile[previous]) * 0.5f * kDerivativeScale);
            const float curvature = softSat((2.0f * height - table.terrainProfile[next] - table.terrainProfile[previous])
                * kDerivativeScale * kDerivativeScale);
            float value = height;
            if (params_.interpretation == AmbiWaveTerrainInterpretation::Edge) value = edge;
            else if (params_.interpretation == AmbiWaveTerrainInterpretation::Curvature) value = curvature;
            else if (params_.interpretation == AmbiWaveTerrainInterpretation::Blend) {
                value = lerp(height, lerp(edge, curvature, 0.35f), params_.interpretationMix);
            } else if (params_.interpretation == AmbiWaveTerrainInterpretation::Gradient) value = gradientProfile[index];
            else if (params_.interpretation == AmbiWaveTerrainInterpretation::Ridge) value = std::max(0.0f, curvature);
            else if (params_.interpretation == AmbiWaveTerrainInterpretation::Valley) value = std::max(0.0f, -curvature);
            else if (params_.interpretation == AmbiWaveTerrainInterpretation::Normal) value = normalProfile[index];
            else if (params_.interpretation == AmbiWaveTerrainInterpretation::Cross) value = crossProfile[index];
            else if (params_.interpretation == AmbiWaveTerrainInterpretation::Vector) {
                const float derived = edge * 0.38f + curvature * 0.24f
                    + gradientProfile[index] * 0.20f + crossProfile[index] * 0.18f;
                value = lerp(height, softSat(derived * 1.35f), params_.interpretationMix);
            }
            table.levels[0][index] = value;
            mean += value;
        }
        mean /= static_cast<float>(kAmbiWaveTerrainTableSize);
        float peak = 0.0001f;
        for (auto& value : table.levels[0]) { value -= mean; peak = std::max(peak, std::fabs(value)); }
        const float normalize = 0.92f / peak;
        for (auto& value : table.levels[0]) value *= normalize;
        Vec3 firstWorld {};
        Vec3 previousWorld {};
        float traversalLength = 0.0f;
        constexpr uint32_t kTraversalSamples = 64u;
        for (uint32_t index = 0; index < kTraversalSamples; ++index) {
            const auto uv = contour(region, static_cast<float>(index) / static_cast<float>(kTraversalSamples));
            const auto point = surfacePoint(uv[0], uv[1]);
            const auto direction = directionFromAed(point.azimuthDeg, point.elevationDeg);
            const Vec3 world { direction.x * point.distance, direction.y * point.distance, direction.z * point.distance };
            if (index == 0u) firstWorld = world;
            else {
                const float dx = world.x - previousWorld.x, dy = world.y - previousWorld.y, dz = world.z - previousWorld.z;
                traversalLength += std::sqrt(dx * dx + dy * dy + dz * dz);
            }
            previousWorld = world;
        }
        const float closeX = firstWorld.x - previousWorld.x, closeY = firstWorld.y - previousWorld.y, closeZ = firstWorld.z - previousWorld.z;
        table.traversalLength = std::max(0.05f, traversalLength + std::sqrt(closeX * closeX + closeY * closeY + closeZ * closeZ));
        buildBandLimitedLevels(table);
    }

    static float tableSample(const AmbiWaveTerrainTable& table, float phase, float frequency)
    {
        const int level = std::clamp(static_cast<int>(std::floor(std::log2(std::max(1.0f, frequency / 90.0f)))), 0, static_cast<int>(kAmbiWaveTerrainMipLevels - 1u));
        const auto& data = table.levels[static_cast<uint32_t>(level)];
        const float position = fract(phase) * static_cast<float>(kAmbiWaveTerrainTableSize);
        const int i1 = static_cast<int>(std::floor(position));
        const float t = position - std::floor(position);
        const int i0 = (i1 + static_cast<int>(kAmbiWaveTerrainTableSize) - 1) % static_cast<int>(kAmbiWaveTerrainTableSize);
        const int i2 = (i1 + 1) % static_cast<int>(kAmbiWaveTerrainTableSize);
        const int i3 = (i1 + 2) % static_cast<int>(kAmbiWaveTerrainTableSize);
        const float p0 = data[static_cast<uint32_t>(i0)];
        const float p1 = data[static_cast<uint32_t>(i1)];
        const float p2 = data[static_cast<uint32_t>(i2)];
        const float p3 = data[static_cast<uint32_t>(i3)];
        const float t2 = t * t;
        const float t3 = t2 * t;
        return 0.5f * ((2.0f * p1) + (-p0 + p2) * t
            + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
            + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
    }

    struct ScanSample {
        float current = 0.0f;
        float next = 0.0f;
    };

    ScanSample oscillator(AmbiWaveTerrainVoice& voice, float& phase, float frequency) const
    {
        phase = fract(phase + frequency / static_cast<float>(sampleRate_));
        ScanSample sample {};
        sample.current = tableSample(voice.current, phase, frequency);
        sample.next = tableSample(voice.next, phase, frequency);
        return sample;
    }

    float frequencyForVoice(float note, uint32_t voiceIndex, bool midi) const
    {
        const float center = (static_cast<float>(std::max<uint32_t>(1u, params_.voices)) - 1.0f) * 0.5f;
        const float position = center > 0.0f ? (static_cast<float>(voiceIndex) - center) / center : 0.0f;
        const float spread = midi ? 0.0f : position * params_.pitchSpreadSemitones * 0.5f;
        const float detune = deterministicSigned(voiceIndex * 29u + 7u) * params_.detuneCents;
        float terrainNote = note + spread;
        if (!midi && params_.pitchMode == AmbiWaveTerrainPitchMode::Traversal) {
            const auto& voice = voices_[voiceIndex];
            const float length = lerp(voice.current.traversalLength, voice.next.traversalLength, voice.transition);
            constexpr float kReferenceTraversalLength = 4.0f;
            const float traversalRatio = kReferenceTraversalLength / std::max(0.05f, length);
            terrainNote += 12.0f * std::log2(traversalRatio);
        }
        if (!midi) terrainNote = ambiWaveTerrainQuantizeToScale(terrainNote, params_.baseNote, params_.pitchScale);
        const float tuned = terrainNote + (params_.tuneCents + detune) / 100.0f;
        const float frequency = 440.0f * std::pow(2.0f, (tuned - 69.0f) / 12.0f);
        return std::min(frequency, static_cast<float>(sampleRate_) * 0.45f);
    }

    AmbiWaveTerrainRegion initialRegion(uint32_t voice) const
    {
        const float count = static_cast<float>(std::max<uint32_t>(1u, params_.voices));
        AmbiWaveTerrainRegion region {};
        region.u = fract(static_cast<float>(voice) * 0.61803398875f + 0.17f);
        region.v = clamp((static_cast<float>(voice) + 0.5f) / count, 0.05f, 0.95f);
        region.radius = params_.scanRadius;
        region.aspect = params_.scanAspect;
        region.rotation = params_.scanRotation + deterministicSigned(voice * 11u + 3u) * 0.12f;
        region.trace = params_.trace;
        return region;
    }

    float eventDurationSamples(AmbiWaveTerrainVoice& voice, bool active) const
    {
        const float base = active ? params_.fieldDurationSeconds : params_.fieldRestSeconds;
        const float variation = std::exp2(randomSigned(voice) * params_.regionDeviation * 1.5f);
        return std::max(1.0f, base * variation * static_cast<float>(sampleRate_));
    }

    AmbiWaveTerrainRegion chooseRegion(uint32_t voiceIndex)
    {
        auto& voice = voices_[voiceIndex];
        AmbiWaveTerrainRegion region = voice.next.region;
        const float step = 0.015f + params_.regionDeviation * 0.32f;
        float targetU = region.u;
        float targetV = region.v;
        switch (params_.selection) {
        case AmbiWaveTerrainSelection::Random:
            targetU = randomUnit(voice); targetV = 0.04f + randomUnit(voice) * 0.92f;
            break;
        case AmbiWaveTerrainSelection::Series:
            ++voice.seriesCursor;
            targetU = fract(static_cast<float>(voice.seriesCursor) * 0.61803398875f + static_cast<float>(voiceIndex) * 0.037f);
            targetV = 0.08f + fract(static_cast<float>(voice.seriesCursor) * 0.38196601125f + static_cast<float>(voiceIndex) * 0.053f) * 0.84f;
            break;
        case AmbiWaveTerrainSelection::Weight: {
            float best = -1.0f;
            for (uint32_t candidate = 0; candidate < 5u; ++candidate) {
                const float u = fract(region.u + randomSigned(voice) * step * 2.0f);
                const float v = clamp(region.v + randomSigned(voice) * step * 2.0f, 0.03f, 0.97f);
                const float score = std::fabs(terrainHeight(u, v));
                if (score > best) { best = score; targetU = u; targetV = v; }
            }
            break;
        }
        case AmbiWaveTerrainSelection::Tendency:
            targetU = fract(lerp(region.u, macroU_, 0.18f + params_.regionDeviation * 0.42f) + randomSigned(voice) * step * 0.35f);
            targetV = clamp(lerp(region.v, macroV_, 0.18f + params_.regionDeviation * 0.42f) + randomSigned(voice) * step * 0.35f, 0.03f, 0.97f);
            break;
        case AmbiWaveTerrainSelection::Markov: {
            uint32_t direction = std::min<uint32_t>(7u,
                static_cast<uint32_t>(randomUnit(voice) * 8.0f));
            if (params_.fieldListenMode != AmbiFieldListenMode::Off
                && fieldListener_.activity() > 0.0001f) {
                std::array<float, 8> weights {};
                float total = 0.0f;
                for (uint32_t candidate = 0u; candidate < weights.size(); ++candidate) {
                    const float candidateAngle =
                        static_cast<float>(candidate) * kAmbiWaveTerrainTwoPi / 8.0f;
                    const float u = fract(region.u + std::cos(candidateAngle) * step);
                    const float v = clamp(region.v + std::sin(candidateAngle) * step,
                        0.03f, 0.97f);
                    weights[candidate] = 0.08f + listenerPreference(u, v) * 2.4f;
                    total += weights[candidate];
                }
                float target = randomUnit(voice) * std::max(0.0001f, total);
                for (uint32_t candidate = 0u; candidate < weights.size(); ++candidate) {
                    target -= weights[candidate];
                    if (target <= 0.0f) {
                        direction = candidate;
                        break;
                    }
                }
            }
            const float angle = static_cast<float>(direction) * kAmbiWaveTerrainTwoPi / 8.0f;
            targetU = fract(region.u + std::cos(angle) * step);
            targetV = clamp(region.v + std::sin(angle) * step, 0.03f, 0.97f);
            break;
        }
        case AmbiWaveTerrainSelection::Walk:
        default:
            targetU = fract(region.u + randomSigned(voice) * step);
            targetV = clamp(region.v + randomSigned(voice) * step, 0.03f, 0.97f);
            break;
        }

        const float listenAmount = fieldListener_.activity()
            * (params_.fieldListenMode == AmbiFieldListenMode::Off ? 0.0f : 0.46f);
        if (listenAmount > 0.0001f) {
            const auto target = listenerTargetUv();
            targetU = lerpWrappedUnit(targetU, target[0], listenAmount);
            targetV = clamp(lerp(targetV, target[1], listenAmount), 0.03f, 0.97f);
        }

        if (params_.transition == AmbiWaveTerrainTransition::Merge) {
            const uint32_t other = (voiceIndex + 1u) % std::max<uint32_t>(1u, params_.voices);
            const auto neighborRegion = voices_[other].next.region;
            targetU = fract(lerp(targetU, neighborRegion.u, params_.neighborTransfer));
            targetV = clamp(lerp(targetV, neighborRegion.v, params_.neighborTransfer), 0.03f, 0.97f);
            neighbor_[voiceIndex] = other;
        } else if (params_.transition == AmbiWaveTerrainTransition::Vary) {
            targetU = fract(lerp(region.u, targetU, 0.12f + params_.regionDeviation * 0.18f));
            targetV = clamp(lerp(region.v, targetV, 0.12f + params_.regionDeviation * 0.18f), 0.03f, 0.97f);
        } else {
            neighbor_[voiceIndex] = (voiceIndex + 1u) % std::max<uint32_t>(1u, params_.voices);
        }

        const float memory = params_.selectionMemory;
        region.u = fract(lerp(targetU, region.u, memory * 0.82f));
        region.v = clamp(lerp(targetV, region.v, memory * 0.82f), 0.03f, 0.97f);
        region.radius = clamp(params_.scanRadius * std::exp2(randomSigned(voice) * params_.regionDeviation), 0.005f, 0.48f);
        region.aspect = clamp(params_.scanAspect + randomSigned(voice) * params_.regionDeviation * 0.28f, 0.05f, 1.0f);
        region.rotation = clamp(params_.scanRotation + randomSigned(voice) * params_.regionDeviation, -1.0f, 1.0f);
        if (listenAmount > 0.0001f) {
            const auto target = listenerTargetUv();
            float du = target[0] - region.u;
            if (du > 0.5f) du -= 1.0f;
            else if (du < -0.5f) du += 1.0f;
            const float dv = target[1] - region.v;
            const float listenerRotation = std::atan2(dv, du) / kPi;
            region.rotation = clamp(lerp(region.rotation, listenerRotation,
                listenAmount * 0.82f), -1.0f, 1.0f);
        }
        region.trace = params_.trace;
        return region;
    }

    void beginTransition(uint32_t voiceIndex, const AmbiWaveTerrainRegion& region)
    {
        auto& voice = voices_[voiceIndex];
        if (voice.transition >= 1.0f) voice.current = voice.next;
        else {
            const float t = voice.transition;
            for (uint32_t level = 0; level < kAmbiWaveTerrainMipLevels; ++level) {
                for (uint32_t index = 0; index < kAmbiWaveTerrainTableSize; ++index) {
                    voice.current.levels[level][index] = lerp(voice.current.levels[level][index], voice.next.levels[level][index], t);
                }
            }
            for (uint32_t index = 0; index < kAmbiWaveTerrainTableSize; ++index) {
                voice.current.terrainProfile[index] = lerp(
                    voice.current.terrainProfile[index], voice.next.terrainProfile[index], t);
            }
            voice.current.traversalLength = lerp(voice.current.traversalLength, voice.next.traversalLength, t);
            voice.current.region.u = lerpWrappedUnit(voice.current.region.u, voice.next.region.u, t);
            voice.current.region.v = lerp(voice.current.region.v, voice.next.region.v, t);
            voice.current.region.radius = lerp(voice.current.region.radius, voice.next.region.radius, t);
            voice.current.region.aspect = lerp(voice.current.region.aspect, voice.next.region.aspect, t);
            voice.current.region.rotation = lerp(voice.current.region.rotation, voice.next.region.rotation, t);
            if (t >= 0.5f) voice.current.region.trace = voice.next.region.trace;
        }
        renderTable(voice.next, region);
        voice.transition = 0.0f;
        voice.transitionStep = 1.0f / std::max(1.0f, params_.tableXfadeMs * 0.001f * static_cast<float>(sampleRate_));
    }

    void updateField(uint32_t voiceIndex, uint32_t frames)
    {
        auto& voice = voices_[voiceIndex];
        if (params_.motionMode == AmbiWaveTerrainMotionMode::Rotate) {
            const float seconds = static_cast<float>(frames) / static_cast<float>(sampleRate_);
            const float deviation = params_.rotationRateDeviation * 1.5f;
            const float azimuthScale = std::exp2(deterministicSigned(voiceIndex * 43u + 11u) * deviation);
            const float elevationScale = std::exp2(deterministicSigned(voiceIndex * 47u + 19u) * deviation);
            voice.azimuthPhase = fract(voice.azimuthPhase + params_.azimuthRateRpm * azimuthScale * seconds / 60.0f);
            voice.elevationPhase = fract(voice.elevationPhase + params_.elevationRateRpm * elevationScale * seconds / 60.0f);
            voice.eventSamples -= static_cast<float>(frames);
            if (voice.eventSamples > 0.0f) return;

            AmbiWaveTerrainRegion region = voice.next.region;
            const auto uv = ambiWaveTerrainRotationUv(voice.azimuthPhase, voice.elevationPhase);
            region.u = uv[0];
            region.v = uv[1];
            region.radius = params_.scanRadius;
            region.aspect = params_.scanAspect;
            region.rotation = params_.scanRotation;
            region.trace = params_.trace;
            const float listenAmount = fieldListener_.activity()
                * (params_.fieldListenMode == AmbiFieldListenMode::Off ? 0.0f : 0.34f);
            if (listenAmount > 0.0001f) {
                const auto target = listenerTargetUv();
                region.u = lerpWrappedUnit(region.u, target[0], listenAmount);
                region.v = clamp(lerp(region.v, target[1], listenAmount), 0.03f, 0.97f);
            }
            const float updateSeconds = std::max(0.75f, params_.tableXfadeMs * 0.001f);
            beginTransition(voiceIndex, region);
            voice.transitionStep = 1.0f / std::max(1.0f, updateSeconds * static_cast<float>(sampleRate_));
            voice.fieldActive = true;
            fieldActive_[voiceIndex] = 1u;
            neighbor_[voiceIndex] = voiceIndex;
            voice.eventSamples = updateSeconds * static_cast<float>(sampleRate_);
            return;
        }

        voice.eventSamples -= static_cast<float>(frames);
        if (voice.eventSamples > 0.0f) return;
        if (voice.fieldActive) {
            beginTransition(voiceIndex, chooseRegion(voiceIndex));
            float density = params_.fieldDensity;
            if (params_.fieldListenMode != AmbiFieldListenMode::Off) {
                const float preference = listenerPreference(
                    voice.next.region.u, voice.next.region.v);
                density = clamp(density + (preference - 0.5f)
                    * fieldListener_.activity() * 0.72f, 0.01f, 0.995f);
            }
            voice.fieldActive = randomUnit(voice) <= density;
        } else {
            voice.fieldActive = true;
        }
        fieldActive_[voiceIndex] = voice.fieldActive ? 1u : 0u;
        voice.eventSamples = eventDurationSamples(voice, voice.fieldActive);
    }

    AmbiWaveTerrainPoint pointForRegion(const AmbiWaveTerrainRegion& region) const { return surfacePoint(region.u, region.v); }
    void updateSpatialTarget(AmbiWaveTerrainVoice& voice, const AmbiWaveTerrainPoint& point) const
    {
        const auto basis = acnSn3dBasis7(directionFromAed(point.azimuthDeg, point.elevationDeg));
        const float distanceGain = 1.0f / std::max(0.25f, point.distance);
        for (uint32_t channel = 0; channel < kAmbiWaveTerrainMaxChannels; ++channel) {
            voice.centerSpatial[channel] = basis[channel] * distanceGain;
        }
    }

    void updatePoint(uint32_t voiceIndex)
    {
        auto& voice = voices_[voiceIndex];
        const auto from = pointForRegion(voice.current.region);
        const auto to = pointForRegion(voice.next.region);
        AmbiWaveTerrainPoint target {};
        const float t = voice.transition;
        const auto fromDirection = directionFromAed(from.azimuthDeg, from.elevationDeg);
        const auto toDirection = directionFromAed(to.azimuthDeg, to.elevationDeg);
        const auto targetDirection = normalize(Vec3 {
            lerp(fromDirection.x, toDirection.x, t),
            lerp(fromDirection.y, toDirection.y, t),
            lerp(fromDirection.z, toDirection.z, t),
        });
        target.azimuthDeg = wrapSignedDeg(std::atan2(targetDirection.y, targetDirection.x) * 180.0f / kPi);
        target.elevationDeg = std::asin(clamp(targetDirection.z, -1.0f, 1.0f)) * 180.0f / kPi;
        target.distance = lerp(from.distance, to.distance, t);
        target.terrain = lerp(from.terrain, to.terrain, t);
        const float follow = 1.0f - params_.spatialFollow;
        auto& point = points_[voiceIndex];
        const auto pointDirection = directionFromAed(point.azimuthDeg, point.elevationDeg);
        const auto smoothedDirection = normalize(Vec3 {
            lerp(pointDirection.x, targetDirection.x, follow),
            lerp(pointDirection.y, targetDirection.y, follow),
            lerp(pointDirection.z, targetDirection.z, follow),
        });
        point.azimuthDeg = wrapSignedDeg(std::atan2(smoothedDirection.y, smoothedDirection.x) * 180.0f / kPi);
        point.elevationDeg = std::asin(clamp(smoothedDirection.z, -1.0f, 1.0f)) * 180.0f / kPi;
        point.distance += (target.distance - point.distance) * follow;
        point.terrain += (target.terrain - point.terrain) * follow;
        updateSpatialTarget(voice, point);
        previousPoints_[voiceIndex] = point;
    }

    void updateMacro(uint32_t frames)
    {
        const float duration = std::max(1.0f, params_.macroDurationSeconds * static_cast<float>(sampleRate_));
        macroPhase_ += static_cast<float>(frames) / duration;
        if (macroPhase_ >= 1.0f) {
            macroPhase_ = fract(macroPhase_);
            macroStartU_ = macroTargetU_;
            macroStartV_ = macroTargetV_;
            macroSeed_ = hash(macroSeed_ + 0x9e3779b9u);
            macroTargetU_ = static_cast<float>(macroSeed_ & 0xffffu) / 65535.0f;
            macroTargetV_ = 0.08f + static_cast<float>((macroSeed_ >> 16u) & 0xffffu) / 65535.0f * 0.84f;
        }
        const float smooth = macroPhase_ * macroPhase_ * (3.0f - 2.0f * macroPhase_);
        macroU_ = fract(lerp(macroStartU_, macroTargetU_, smooth));
        macroV_ = lerp(macroStartV_, macroTargetV_, smooth);
    }

    void refreshDirtyVoice()
    {
        if (dirtyVoice_ >= params_.voices) return;
        auto& voice = voices_[dirtyVoice_];
        AmbiWaveTerrainRegion region = voice.next.region;
        region.radius = params_.scanRadius;
        region.aspect = params_.scanAspect;
        region.rotation = params_.scanRotation;
        region.trace = params_.trace;
        beginTransition(dirtyVoice_, region);
        ++dirtyVoice_;
    }

    float listenerPreference(float u, float v) const
    {
        const AmbiWaveTerrainPoint point = surfacePoint(u, v);
        return fieldListener_.preference(
            directionFromAed(point.azimuthDeg, point.elevationDeg),
            params_.fieldListenMode);
    }

    std::array<float, 2> listenerTargetUv() const
    {
        const Vec3 direction = fieldListener_.preferredDirection(params_.fieldListenMode);
        const float length = std::sqrt(direction.x * direction.x
            + direction.y * direction.y + direction.z * direction.z);
        if (length < 1.0e-7f) return { macroU_, macroV_ };
        const float azimuth = std::atan2(direction.y, direction.x) * 180.0f / kPi;
        const float elevation = std::asin(clamp(direction.z / length, -1.0f, 1.0f))
            * 180.0f / kPi;
        const float spread = std::max(0.05f, params_.spatialSpread);
        const float azimuthOffset = wrapSignedDeg(azimuth - params_.centerAzimuthDeg);
        const float u = fract(0.5f + azimuthOffset / (360.0f * spread));
        const float v = clamp(0.5f
            + (elevation - params_.centerElevationDeg) / (180.0f * spread),
            0.03f, 0.97f);
        return { u, v };
    }

    double sampleRate_ = 48000.0;
#if defined(__APPLE__)
    FFTSetup fftSetup_ = nullptr;
#endif
    AmbiWaveTerrainParams params_ {};
    std::array<AmbiWaveTerrainVoice, kAmbiWaveTerrainMaxVoices> voices_ {};
    std::array<AmbiWaveTerrainPoint, kAmbiWaveTerrainMaxVoices> points_ {};
    std::array<AmbiWaveTerrainPoint, kAmbiWaveTerrainMaxVoices> previousPoints_ {};
    std::array<uint8_t, kAmbiWaveTerrainMaxVoices> fieldActive_ {};
    std::array<uint32_t, kAmbiWaveTerrainMaxVoices> neighbor_ {};
    uint32_t dirtyVoice_ = kAmbiWaveTerrainMaxVoices;
    uint32_t macroSeed_ = 0x51ed270bu;
    float macroU_ = 0.5f;
    float macroV_ = 0.5f;
    float macroStartU_ = 0.5f;
    float macroStartV_ = 0.5f;
    float macroTargetU_ = 0.67f;
    float macroTargetV_ = 0.38f;
    float macroPhase_ = 0.0f;
    AmbiFieldListener fieldListener_ {};
};

inline const char* ambiWaveTerrainModeName(AmbiWaveTerrainMode mode)
{
    switch (mode) { case AmbiWaveTerrainMode::Midi: return "MIDI"; case AmbiWaveTerrainMode::Both: return "BOTH"; default: return "FREE"; }
}
inline const char* ambiWaveTerrainPitchModeName(AmbiWaveTerrainPitchMode mode)
{
    return mode == AmbiWaveTerrainPitchMode::Traversal ? "TRAVEL" : "NOTE";
}
inline const char* ambiWaveTerrainPitchScaleName(AmbiWaveTerrainPitchScale scale)
{
    static constexpr const char* names[] { "FREE", "CHROM", "MAJOR", "MINOR", "PENTA", "WHOLE", "HARM MIN" };
    return names[std::min<uint32_t>(static_cast<uint32_t>(scale), 6u)];
}
inline const char* ambiWaveTerrainFormName(AmbiWaveTerrainForm form)
{
    static constexpr const char* names[] { "SPHERE", "TETRA", "CUBE", "OCTA", "DODECA", "ICOSA" };
    return names[std::min<uint32_t>(static_cast<uint32_t>(form), 5u)];
}
inline const char* ambiWaveTerrainMotionModeName(AmbiWaveTerrainMotionMode mode)
{
    return mode == AmbiWaveTerrainMotionMode::Rotate ? "ROTATE" : "FIELD";
}
inline const char* ambiWaveTerrainSkinName(AmbiWaveTerrainSkin skin)
{
    static constexpr const char* names[] { "HARMONIC", "FBM", "CELL", "VOT", "RIDGES", "DUNES", "CRATERS", "TECTONIC" };
    return names[std::min<uint32_t>(static_cast<uint32_t>(skin), 7u)];
}
inline const char* ambiWaveTerrainTraceName(AmbiWaveTerrainTrace trace)
{
    static constexpr const char* names[] { "ORBIT", "LISSAJOUS", "ROSETTE", "FOLD", "POLYGON" };
    return names[std::min<uint32_t>(static_cast<uint32_t>(trace), 4u)];
}
inline const char* ambiWaveTerrainSelectionName(AmbiWaveTerrainSelection selection)
{
    static constexpr const char* names[] { "RANDOM", "SERIES", "WEIGHT", "TENDENCY", "MARKOV", "WALK" };
    return names[std::min<uint32_t>(static_cast<uint32_t>(selection), 5u)];
}
inline const char* ambiWaveTerrainTransitionName(AmbiWaveTerrainTransition transition)
{
    static constexpr const char* names[] { "LINK", "MERGE", "VARY" };
    return names[std::min<uint32_t>(static_cast<uint32_t>(transition), 2u)];
}
inline const char* ambiWaveTerrainInterpretationName(AmbiWaveTerrainInterpretation interpretation)
{
    static constexpr const char* names[] {
        "HEIGHT", "EDGE", "CURVE", "BLEND", "GRADIENT",
        "RIDGE", "VALLEY", "NORMAL", "CROSS", "VECTOR",
    };
    return names[std::min<uint32_t>(static_cast<uint32_t>(interpretation), 9u)];
}

} // namespace s3g
