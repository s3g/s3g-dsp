#pragma once

#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAmbiEnvironmentMaxOrder = 7;
constexpr uint32_t kAmbiEnvironmentMaxChannels = 64;
constexpr uint32_t kAmbiEnvironmentCellCount = 8;

inline float ambiEnvironmentWrapDeg(float degrees)
{
    while (degrees > 180.0f) degrees -= 360.0f;
    while (degrees <= -180.0f) degrees += 360.0f;
    return degrees;
}

enum class AmbiEnvironmentScene : uint32_t {
    Woodland = 0,
    Wetland,
    Shore,
    Rain,
    Urban,
    Industrial,
    Interior,
};

struct AmbiEnvironmentParams {
    uint32_t order = 3;
    AmbiEnvironmentScene scene = AmbiEnvironmentScene::Woodland;
    uint32_t seed = 1977;
    float activity = 0.55f;
    float evolve = 0.35f;
    float wind = 0.70f;
    float rain = 0.45f;
    float water = 0.45f;
    float fire = 0.30f;
    float insects = 0.50f;
    float machine = 0.35f;
    float nearFar = 0.55f;
    float space = 0.65f;
    float fieldAzimuthDeg = 0.0f;
    float fieldElevationDeg = 0.0f;
    float headRollDeg = 0.0f;
    float width = 1.0f;
    float walkRate = 0.16f;
    float walkDepth = 0.35f;
    float sourceMotion = 0.30f;
    float outputGainDb = -18.0f;
};

struct AmbiEnvironmentCell {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
    float energy = 0.0f;
};

inline const char* ambiEnvironmentSceneName(AmbiEnvironmentScene scene)
{
    switch (scene) {
    case AmbiEnvironmentScene::Wetland: return "WETLAND";
    case AmbiEnvironmentScene::Shore: return "SHORE";
    case AmbiEnvironmentScene::Rain: return "RAIN";
    case AmbiEnvironmentScene::Urban: return "URBAN";
    case AmbiEnvironmentScene::Industrial: return "INDUSTRIAL";
    case AmbiEnvironmentScene::Interior: return "INTERIOR";
    case AmbiEnvironmentScene::Woodland:
    default: return "WOODLAND";
    }
}

class AmbiEnvironmentGenerator {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        reset();
    }

    void reset()
    {
        rng_ = params_.seed == 0u ? 1u : params_.seed;
        sampleCounter_ = 0u;
        scenePhase_ = 0.0f;
        walkPhase_ = random01();
        gust_ = 0.0f;
        machinePhase_ = 0.0f;
        machineTravel_ = 0.18f;
        machineNoise_ = 0.0f;
        outputGainSmoothed_ = dbToGain(params_.outputGainDb);
        smooth_ = params_;
        for (auto& cell : cells_) cell = {};
        for (auto& state : bed_) state = {};
        for (auto& state : eventAir_) state = 0.0f;
        for (auto& voice : rainVoices_) voice = {};
        for (auto& voice : bubbleVoices_) voice = {};
        for (auto& voice : crackleVoices_) voice = {};
        initializeWorldLayout();
        for (uint32_t i = 0; i < insectVoices_.size(); ++i) {
            auto& voice = insectVoices_[i];
            voice.phase = random01();
            voice.modPhase = random01();
            voice.frequency = 3600.0f + random01() * 3100.0f;
            voice.modRate = 0.65f + random01() * 1.15f;
            voice.cell = insectCells_[i % insectCells_.size()];
        }
        updateSpatialState();
    }

    void setParams(AmbiEnvironmentParams params)
    {
        params = sanitize(params);
        const bool seedChanged = params.seed != params_.seed;
        params_ = params;
        if (seedChanged) reset();
    }

    AmbiEnvironmentParams params() const { return params_; }
    const std::array<AmbiEnvironmentCell, kAmbiEnvironmentCellCount>& cells() const { return cells_; }
    const std::array<float, 6>& layerLevels() const { return layerLevels_; }

    void processBlock(float* const* outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiEnvironmentMaxChannels);
        for (uint32_t ch = 0; ch < outputChannels; ++ch) {
            if (outputs[ch]) std::fill(outputs[ch], outputs[ch] + frames, 0.0f);
        }
        if (outputChannels == 0u) return;

        smoothParams();
        const uint32_t ambiChannels = std::min<uint32_t>(outputChannels, (params_.order + 1u) * (params_.order + 1u));
        const uint32_t diffuseOrder = std::min<uint32_t>(params_.order, 1u + static_cast<uint32_t>(std::lround(smooth_.space * 2.0f)));
        const uint32_t diffuseChannels = std::min<uint32_t>(ambiChannels, (diffuseOrder + 1u) * (diffuseOrder + 1u));
        const SceneProfile profile = sceneProfile(params_.scene);
        const float activity = 0.12f + smooth_.activity * 0.88f;
        const float windLevel = profile.wind * smooth_.wind;
        const float rainLevel = profile.rain * smooth_.rain;
        const float waterLevel = profile.water * smooth_.water;
        const float fireLevel = profile.fire * smooth_.fire;
        const float insectLevel = profile.insects * smooth_.insects;
        const float machineLevel = profile.machine * smooth_.machine;
        const float outputTarget = dbToGain(smooth_.outputGainDb);
        const float distanceGain = 1.0f / (0.45f + smooth_.nearFar * 1.35f);
        const float airCutoff = 13500.0f - smooth_.nearFar * 10500.0f;
        const float airCoef = onePoleCoef(airCutoff);
        std::array<float, kAmbiEnvironmentMaxChannels> orderWeights {};
        for (uint32_t ch = 0u; ch < ambiChannels; ++ch) {
            const uint32_t order = static_cast<uint32_t>(std::floor(std::sqrt(static_cast<float>(ch))));
            orderWeights[ch] = order == 0u ? 1.0f : std::pow(smooth_.width, static_cast<float>(order) * 0.72f);
        }
        constexpr uint32_t kSpatialUpdateFrames = 32u;

        for (uint32_t chunkStart = 0; chunkStart < frames; chunkStart += kSpatialUpdateFrames) {
            const uint32_t chunkFrames = std::min<uint32_t>(kSpatialUpdateFrames, frames - chunkStart);
            updateSpatialState();
            const auto machineBasis = machineSpatialBasis();

            for (uint32_t local = 0; local < chunkFrames; ++local) {
                const uint32_t frame = chunkStart + local;
                outputGainSmoothed_ += (outputTarget - outputGainSmoothed_) * 0.0009f;
                scenePhase_ += (0.004f + smooth_.evolve * 0.075f) / static_cast<float>(sampleRate_);
                if (scenePhase_ >= 1.0f) scenePhase_ -= 1.0f;
                const float walkHz = 0.0015f + smooth_.walkRate * smooth_.walkRate * 0.045f;
                walkPhase_ += walkHz / static_cast<float>(sampleRate_);
                if (walkPhase_ >= 1.0f) walkPhase_ -= 1.0f;

                const float gustTarget = randomSigned();
                gust_ += (gustTarget - gust_) * (0.000015f + smooth_.evolve * 0.00012f);
                const float gust = std::clamp(0.72f + gust_ * 0.42f + std::sin(scenePhase_ * 2.0f * kPi) * 0.16f, 0.08f, 1.35f);

                maybeSpawnRain(rainLevel * activity);
                maybeSpawnBubble(waterLevel * activity);
                maybeSpawnCrackle(fireLevel * activity);

                std::array<float, kAmbiEnvironmentCellCount> diffuse {};
                std::array<float, kAmbiEnvironmentCellCount> events {};
                for (uint32_t cell = 0; cell < kAmbiEnvironmentCellCount; ++cell) {
                    const float noise = randomSigned();
                    auto& state = bed_[cell];
                    state.fast += (noise - state.fast) * 0.075f;
                    state.mid += (noise - state.mid) * 0.011f;
                    state.slow += (noise - state.slow) * 0.0012f;
                    state.fireMod += (noise - state.fireMod) * 0.00065f;
                    const float windTarget = std::clamp(gust + std::sin(scenePhase_ * 2.0f * kPi + cell * 0.73f) * smooth_.evolve * 0.12f, 0.0f, 1.5f);
                    state.windControl += (windTarget - state.windControl) * (0.00042f / (1.0f + static_cast<float>(cell) * 0.24f));
                    const float localWind = state.windControl;
                    const float wind = ((state.fast - state.mid) * 0.70f + (state.mid - state.slow) * 1.25f + state.slow * 0.20f)
                        * windLevel * localWind;
                    const float whistleGate = std::max(0.0f, localWind - (0.48f + static_cast<float>(cell % 3u) * 0.08f));
                    const float whistleHz = (520.0f + static_cast<float>(cell) * 185.0f) * (0.72f + localWind * 0.72f);
                    state.whistlePhase += whistleHz / static_cast<float>(sampleRate_);
                    state.whistlePhase -= std::floor(state.whistlePhase);
                    const float whistle = std::sin(state.whistlePhase * 2.0f * kPi) * whistleGate * whistleGate * windLevel * 0.028f;
                    const float rainHiss = (noise - state.fast) * rainLevel * (0.12f + 0.22f * activity);
                    const float waterWash = (state.mid - state.slow) * waterLevel * (0.18f + 0.10f * std::sin(scenePhase_ * 2.0f * kPi + cell));
                    const float hissGate = std::max(0.0f, std::fabs(state.fireMod) * 4.0f - 0.22f);
                    const float fireHiss = (noise - state.fast) * hissGate * hissGate * fireLevel * 0.18f;
                    const float flameLap = (state.mid - state.slow) * (0.55f + std::fabs(state.fireMod) * 2.5f) * fireLevel * 0.16f;
                    const float roomAir = state.slow * profile.air * (0.12f + smooth_.space * 0.12f);
                    const float windZone = windZone_[cell];
                    const float rainZone = rainZone_[cell];
                    const float waterZone = waterZone_[cell];
                    const float fireZone = fireZone_[cell];
                    diffuse[cell] = (wind * 0.22f + whistle) * windZone
                        + rainHiss * rainZone
                        + waterWash * waterZone
                        + (fireHiss + flameLap) * fireZone
                        + roomAir;

                    // Leaf and branch detail is localized at full order. The
                    // slower wind bed above remains low-order and enveloping.
                    const float rustleGate = std::max(0.0f, localWind - 0.36f);
                    const float leafNoise = noise - state.fast;
                    events[cell] += leafNoise * rustleGate * rustleGate * windLevel * windZone * 0.055f;
                }

                processParticles(rainVoices_, events, 0.48f);
                processParticles(bubbleVoices_, events, 0.28f);
                processParticles(crackleVoices_, events, 0.72f);
                processInsects(events, insectLevel * activity);

                float blockEnergy = 0.0f;
                for (uint32_t cell = 0; cell < kAmbiEnvironmentCellCount; ++cell) {
                    eventAir_[cell] += (events[cell] - eventAir_[cell]) * airCoef;
                    const float localDistanceGain = 1.0f / (0.48f + cells_[cell].distance * 0.52f);
                    const float direct = eventAir_[cell] * distanceGain * localDistanceGain;
                    const float bedSample = diffuse[cell] * (0.72f + smooth_.space * 0.28f);
                    blockEnergy += std::fabs(bedSample) + std::fabs(direct);
                    const auto& basis = cellBasis_[cell];
                    for (uint32_t ch = 0; ch < diffuseChannels; ++ch) {
                        if (outputs[ch]) outputs[ch][frame] += bedSample * basis[ch];
                    }
                    for (uint32_t ch = 0; ch < ambiChannels; ++ch) {
                        if (outputs[ch]) outputs[ch][frame] += direct * basis[ch];
                    }
                    cells_[cell].energy += ((std::fabs(bedSample) + std::fabs(direct)) - cells_[cell].energy) * 0.0025f;
                }

                const float machine = processMachine(machineLevel * activity);
                blockEnergy += std::fabs(machine);
                for (uint32_t ch = 0; ch < ambiChannels; ++ch) {
                    if (outputs[ch]) outputs[ch][frame] += machine * machineBasis[ch] * distanceGain;
                }

                const float norm = outputGainSmoothed_ * 0.72f;
                for (uint32_t ch = 0; ch < ambiChannels; ++ch) {
                    if (!outputs[ch]) continue;
                    outputs[ch][frame] = flushDenormal(std::tanh(outputs[ch][frame] * norm * orderWeights[ch]));
                }
                globalEnergy_ += (blockEnergy - globalEnergy_) * 0.0015f;
                ++sampleCounter_;
            }
        }

        layerLevels_[0] += (windLevel - layerLevels_[0]) * 0.08f;
        layerLevels_[1] += (rainLevel - layerLevels_[1]) * 0.08f;
        layerLevels_[2] += (waterLevel - layerLevels_[2]) * 0.08f;
        layerLevels_[3] += (fireLevel - layerLevels_[3]) * 0.08f;
        layerLevels_[4] += (insectLevel - layerLevels_[4]) * 0.08f;
        layerLevels_[5] += (machineLevel - layerLevels_[5]) * 0.08f;
    }

private:
    struct SceneProfile {
        float wind;
        float rain;
        float water;
        float fire;
        float insects;
        float machine;
        float air;
    };

    struct BedState {
        float fast = 0.0f;
        float mid = 0.0f;
        float slow = 0.0f;
        float windControl = 0.0f;
        float fireMod = 0.0f;
        float whistlePhase = 0.0f;
    };

    struct ParticleVoice {
        float envelope = 0.0f;
        float decay = 0.999f;
        float phase = 0.0f;
        float phaseIncrement = 0.0f;
        float sweep = 1.0f;
        float noiseMix = 0.0f;
        uint32_t cell = 0u;
        bool active = false;
    };

    struct InsectVoice {
        float phase = 0.0f;
        float modPhase = 0.0f;
        float frequency = 4200.0f;
        float modRate = 11.0f;
        uint32_t cell = 0u;
    };

    static AmbiEnvironmentParams sanitize(AmbiEnvironmentParams p)
    {
        p.order = std::clamp<uint32_t>(p.order, 1u, kAmbiEnvironmentMaxOrder);
        p.scene = static_cast<AmbiEnvironmentScene>(std::clamp<uint32_t>(static_cast<uint32_t>(p.scene), 0u, 6u));
        p.seed = std::clamp<uint32_t>(p.seed, 1u, 999999u);
        p.activity = clamp(p.activity, 0.0f, 1.0f);
        p.evolve = clamp(p.evolve, 0.0f, 1.0f);
        p.wind = clamp(p.wind, 0.0f, 1.0f);
        p.rain = clamp(p.rain, 0.0f, 1.0f);
        p.water = clamp(p.water, 0.0f, 1.0f);
        p.fire = clamp(p.fire, 0.0f, 1.0f);
        p.insects = clamp(p.insects, 0.0f, 1.0f);
        p.machine = clamp(p.machine, 0.0f, 1.0f);
        p.nearFar = clamp(p.nearFar, 0.0f, 1.0f);
        p.space = clamp(p.space, 0.0f, 1.0f);
        p.fieldAzimuthDeg = ambiEnvironmentWrapDeg(p.fieldAzimuthDeg);
        p.fieldElevationDeg = clamp(p.fieldElevationDeg, -90.0f, 90.0f);
        p.headRollDeg = ambiEnvironmentWrapDeg(p.headRollDeg);
        p.width = clamp(p.width, 0.0f, 1.0f);
        p.walkRate = clamp(p.walkRate, 0.0f, 1.0f);
        p.walkDepth = clamp(p.walkDepth, 0.0f, 1.0f);
        p.sourceMotion = clamp(p.sourceMotion, 0.0f, 1.0f);
        p.outputGainDb = clamp(p.outputGainDb, -60.0f, 6.0f);
        return p;
    }

    static SceneProfile sceneProfile(AmbiEnvironmentScene scene)
    {
        switch (scene) {
        case AmbiEnvironmentScene::Wetland: return { 0.42f, 0.48f, 0.82f, 0.02f, 1.00f, 0.04f, 0.22f };
        case AmbiEnvironmentScene::Shore: return { 0.88f, 0.18f, 1.00f, 0.00f, 0.08f, 0.08f, 0.30f };
        case AmbiEnvironmentScene::Rain: return { 0.68f, 1.00f, 0.42f, 0.00f, 0.08f, 0.08f, 0.20f };
        case AmbiEnvironmentScene::Urban: return { 0.32f, 0.30f, 0.08f, 0.02f, 0.04f, 1.00f, 0.38f };
        case AmbiEnvironmentScene::Industrial: return { 0.22f, 0.10f, 0.05f, 0.18f, 0.00f, 1.00f, 0.42f };
        case AmbiEnvironmentScene::Interior: return { 0.08f, 0.02f, 0.00f, 0.02f, 0.00f, 0.52f, 1.00f };
        case AmbiEnvironmentScene::Woodland:
        default: return { 0.82f, 0.28f, 0.10f, 0.06f, 0.78f, 0.04f, 0.20f };
        }
    }

    void smoothParams()
    {
        constexpr float c = 0.08f;
        smooth_.activity += (params_.activity - smooth_.activity) * c;
        smooth_.evolve += (params_.evolve - smooth_.evolve) * c;
        smooth_.wind += (params_.wind - smooth_.wind) * c;
        smooth_.rain += (params_.rain - smooth_.rain) * c;
        smooth_.water += (params_.water - smooth_.water) * c;
        smooth_.fire += (params_.fire - smooth_.fire) * c;
        smooth_.insects += (params_.insects - smooth_.insects) * c;
        smooth_.machine += (params_.machine - smooth_.machine) * c;
        smooth_.nearFar += (params_.nearFar - smooth_.nearFar) * c;
        smooth_.space += (params_.space - smooth_.space) * c;
        smooth_.fieldAzimuthDeg += ambiEnvironmentWrapDeg(params_.fieldAzimuthDeg - smooth_.fieldAzimuthDeg) * c;
        smooth_.fieldElevationDeg += (params_.fieldElevationDeg - smooth_.fieldElevationDeg) * c;
        smooth_.headRollDeg += ambiEnvironmentWrapDeg(params_.headRollDeg - smooth_.headRollDeg) * c;
        smooth_.width += (params_.width - smooth_.width) * c;
        smooth_.walkRate += (params_.walkRate - smooth_.walkRate) * c;
        smooth_.walkDepth += (params_.walkDepth - smooth_.walkDepth) * c;
        smooth_.sourceMotion += (params_.sourceMotion - smooth_.sourceMotion) * c;
        smooth_.outputGainDb += (params_.outputGainDb - smooth_.outputGainDb) * c;
    }

    uint32_t nextRandom()
    {
        uint32_t x = rng_;
        x ^= x << 13u;
        x ^= x >> 17u;
        x ^= x << 5u;
        rng_ = x == 0u ? 1u : x;
        return rng_;
    }

    float random01() { return static_cast<float>(nextRandom() & 0x00ffffffu) / 16777215.0f; }
    float randomSigned() { return random01() * 2.0f - 1.0f; }

    float randomGaussian()
    {
        float sum = 0.0f;
        for (uint32_t i = 0; i < 6u; ++i) sum += random01();
        return (sum - 3.0f) * 0.72f;
    }

    float onePoleCoef(float cutoff) const
    {
        return 1.0f - std::exp(-2.0f * kPi * std::clamp(cutoff, 20.0f, static_cast<float>(sampleRate_ * 0.45)) / static_cast<float>(sampleRate_));
    }

    template <size_t N>
    ParticleVoice* freeVoice(std::array<ParticleVoice, N>& voices)
    {
        for (auto& voice : voices) {
            if (!voice.active) return &voice;
        }
        auto* quietest = &voices[0];
        for (auto& voice : voices) {
            if (voice.envelope < quietest->envelope) quietest = &voice;
        }
        return quietest;
    }

    void maybeSpawnRain(float level)
    {
        const float rate = level * level * (30.0f + smooth_.activity * 520.0f);
        if (random01() >= rate / static_cast<float>(sampleRate_)) return;
        auto* voice = freeVoice(rainVoices_);
        voice->active = true;
        voice->envelope = std::clamp(0.12f + std::fabs(randomGaussian()) * 0.62f, 0.10f, 1.0f);
        const bool hardSurface = params_.scene == AmbiEnvironmentScene::Urban || params_.scene == AmbiEnvironmentScene::Industrial;
        const bool softSurface = params_.scene == AmbiEnvironmentScene::Woodland || params_.scene == AmbiEnvironmentScene::Wetland;
        const float duration = hardSurface ? (0.018f + random01() * 0.11f)
            : (softSurface ? (0.035f + random01() * 0.16f) : (0.012f + random01() * 0.085f));
        voice->decay = std::exp(-1.0f / (static_cast<float>(sampleRate_) * duration));
        voice->phase = random01();
        const float baseHz = hardSurface ? 1700.0f : (softSurface ? 480.0f : 900.0f);
        voice->phaseIncrement = (baseHz + random01() * (hardSurface ? 5200.0f : 2800.0f)) / static_cast<float>(sampleRate_);
        voice->sweep = 0.9999988f - random01() * 0.0000008f;
        voice->noiseMix = hardSurface ? 0.78f : (softSurface ? 0.94f : 0.88f);
        voice->cell = nextRandom() % kAmbiEnvironmentCellCount;
    }

    void maybeSpawnBubble(float level)
    {
        const float rate = level * level * (3.0f + smooth_.activity * 68.0f);
        if (random01() >= rate / static_cast<float>(sampleRate_)) return;
        auto* voice = freeVoice(bubbleVoices_);
        voice->active = true;
        voice->envelope = 0.12f + random01() * 0.38f;
        voice->decay = std::exp(-1.0f / (static_cast<float>(sampleRate_) * (0.035f + random01() * 0.16f)));
        voice->phase = random01();
        voice->phaseIncrement = (140.0f + std::pow(random01(), 1.8f) * 1150.0f) / static_cast<float>(sampleRate_);
        voice->sweep = 1.0000003f + random01() * 0.0000007f;
        voice->noiseMix = 0.30f;
        voice->cell = waterCells_[nextRandom() % waterCells_.size()];
    }

    void maybeSpawnCrackle(float level)
    {
        const float rate = level * level * (5.0f + smooth_.activity * 95.0f);
        if (random01() >= rate / static_cast<float>(sampleRate_)) return;
        auto* voice = freeVoice(crackleVoices_);
        voice->active = true;
        voice->envelope = 0.16f + random01() * 0.84f;
        voice->decay = std::exp(-1.0f / (static_cast<float>(sampleRate_) * (0.004f + random01() * 0.030f)));
        voice->phase = random01();
        voice->phaseIncrement = (700.0f + random01() * 4200.0f) / static_cast<float>(sampleRate_);
        voice->sweep = 0.9997f;
        voice->noiseMix = 0.88f;
        voice->cell = fireCells_[nextRandom() % fireCells_.size()];
    }

    template <size_t N>
    void processParticles(std::array<ParticleVoice, N>& voices,
                          std::array<float, kAmbiEnvironmentCellCount>& events,
                          float gain)
    {
        for (auto& voice : voices) {
            if (!voice.active) continue;
            const float tone = std::sin(voice.phase * 2.0f * kPi);
            const float noise = randomSigned();
            const float sample = (tone * (1.0f - voice.noiseMix) + noise * voice.noiseMix) * voice.envelope * gain;
            events[voice.cell] += sample;
            voice.phase += voice.phaseIncrement;
            voice.phase -= std::floor(voice.phase);
            voice.phaseIncrement *= voice.sweep;
            voice.envelope *= voice.decay;
            if (voice.envelope < 0.00008f) voice.active = false;
        }
    }

    void processInsects(std::array<float, kAmbiEnvironmentCellCount>& events, float level)
    {
        for (auto& voice : insectVoices_) {
            voice.phase += voice.frequency / static_cast<float>(sampleRate_);
            voice.phase -= std::floor(voice.phase);
            voice.modPhase += voice.modRate / static_cast<float>(sampleRate_);
            voice.modPhase -= std::floor(voice.modPhase);
            const float phrasePosition = voice.modPhase;
            const float phraseGate = phrasePosition < 0.19f ? 1.0f : 0.0f;
            const float subPhase = std::fmod(phrasePosition / 0.19f * 7.0f, 1.0f);
            const float pulseBase = phraseGate * std::max(0.0f, std::sin(subPhase * kPi));
            const float pulse = pulseBase * pulseBase * pulseBase * pulseBase;
            const float carrier = std::sin(voice.phase * 2.0f * kPi)
                + 0.22f * std::sin(voice.phase * 4.0f * kPi);
            events[voice.cell] += carrier * pulse * level * 0.055f;
        }
    }

    float processMachine(float level)
    {
        const bool fixedMachine = params_.scene == AmbiEnvironmentScene::Industrial
            || params_.scene == AmbiEnvironmentScene::Interior;
        const float passShape = std::max(0.0f, std::sin(machineTravel_ * kPi));
        const float passGain = fixedMachine ? 1.0f : std::pow(passShape, 0.38f);
        const float doppler = fixedMachine ? 1.0f : 1.14f - machineTravel_ * 0.28f;
        const float baseHz = params_.scene == AmbiEnvironmentScene::Industrial ? 47.0f
            : (params_.scene == AmbiEnvironmentScene::Interior ? 59.5f : 74.0f);
        const float wander = 1.0f + 0.035f * std::sin(scenePhase_ * 2.0f * kPi * 0.37f);
        machinePhase_ += baseHz * wander * doppler / static_cast<float>(sampleRate_);
        machinePhase_ -= std::floor(machinePhase_);
        machineTravel_ += (0.003f + smooth_.evolve * 0.018f) / static_cast<float>(sampleRate_);
        machineTravel_ -= std::floor(machineTravel_);
        const float exhaust = randomSigned();
        machineNoise_ += (exhaust - machineNoise_) * 0.035f;
        const float hum = std::sin(machinePhase_ * 2.0f * kPi)
            + 0.34f * std::sin(machinePhase_ * 4.0f * kPi)
            + 0.13f * std::sin(machinePhase_ * 6.0f * kPi)
            + machineNoise_ * (0.32f + smooth_.activity * 0.28f);
        return hum * level * passGain * 0.15f;
    }

    void initializeWorldLayout()
    {
        std::array<uint32_t, kAmbiEnvironmentCellCount> permutation {};
        for (uint32_t i = 0; i < kAmbiEnvironmentCellCount; ++i) {
            permutation[i] = i;
            worldAzimuth_[i] = ambiEnvironmentWrapDeg(static_cast<float>(i) * -45.0f + randomSigned() * 21.0f);
            worldElevation_[i] = -12.0f + random01() * 58.0f;
            worldRadius_[i] = 0.85f + random01() * 2.25f;
            worldDriftPhase_[i] = random01();
            windZone_[i] = 0.07f;
            rainZone_[i] = 0.66f + random01() * 0.34f;
            waterZone_[i] = 0.04f;
            fireZone_[i] = 0.025f;
        }
        for (uint32_t i = kAmbiEnvironmentCellCount - 1u; i > 0u; --i) {
            const uint32_t swapIndex = nextRandom() % (i + 1u);
            std::swap(permutation[i], permutation[swapIndex]);
        }
        for (uint32_t i = 0u; i < 3u; ++i) windZone_[permutation[i]] = i == 1u ? 1.0f : 0.72f;
        insectCells_ = { permutation[3u], permutation[4u], permutation[3u] };
        waterCells_ = { permutation[5u], permutation[6u] };
        fireCells_ = { permutation[7u], permutation[6u] };
        waterZone_[waterCells_[0u]] = 1.0f;
        waterZone_[waterCells_[1u]] = 0.58f;
        fireZone_[fireCells_[0u]] = 1.0f;
        fireZone_[fireCells_[1u]] = 0.22f;
        worldHeadingDeg_ = randomSigned() * 180.0f;
        walkShapePhase_ = random01();
    }

    Vec3 listenerPosition() const
    {
        const float phase = walkPhase_ * 2.0f * kPi;
        const float depth = smooth_.walkDepth * 1.35f;
        const float localX = std::sin(phase) * depth;
        const float localY = std::sin(phase * 2.0f + walkShapePhase_ * 2.0f * kPi) * depth * 0.62f;
        const float heading = worldHeadingDeg_ * kPi / 180.0f;
        return {
            std::cos(heading) * localX - std::sin(heading) * localY,
            std::sin(heading) * localX + std::cos(heading) * localY,
            0.0f,
        };
    }

    void updateSpatialState()
    {
        const Vec3 listener = listenerPosition();
        const float sourceMotion = smooth_.sourceMotion;
        for (uint32_t i = 0; i < kAmbiEnvironmentCellCount; ++i) {
            const float driftPhase = scenePhase_ + worldDriftPhase_[i];
            const float az = worldAzimuth_[i] + std::sin(driftPhase * 2.0f * kPi) * sourceMotion * 22.0f;
            const float el = std::clamp(worldElevation_[i]
                    + std::sin(driftPhase * 2.0f * kPi * 0.61f + static_cast<float>(i)) * sourceMotion * 11.0f,
                -60.0f, 82.0f);
            const float radius = worldRadius_[i] * (1.0f + std::sin(driftPhase * 2.0f * kPi * 0.43f) * sourceMotion * 0.16f);
            const Vec3 worldDirection = directionFromAed(az, el);
            const Vec3 relative {
                worldDirection.x * radius - listener.x,
                worldDirection.y * radius - listener.y,
                worldDirection.z * radius - listener.z,
            };
            const float relativeDistance = std::sqrt(relative.x * relative.x + relative.y * relative.y + relative.z * relative.z);
            const Vec3 direction = transformedVector(normalize(relative));
            const float transformedAz = ambiEnvironmentWrapDeg(std::atan2(direction.y, direction.x) * 180.0f / kPi);
            const float transformedEl = std::asin(clamp(direction.z, -1.0f, 1.0f)) * 180.0f / kPi;
            cells_[i].azimuthDeg = transformedAz;
            cells_[i].elevationDeg = transformedEl;
            cells_[i].distance = std::clamp(relativeDistance + smooth_.nearFar * 1.35f, 0.12f, 4.5f);
            cellBasis_[i] = acnSn3dBasis7(direction);
        }
    }

    Vec3 transformedVector(Vec3 direction) const
    {
        const float yaw = smooth_.fieldAzimuthDeg * kPi / 180.0f;
        const float pitch = smooth_.fieldElevationDeg * kPi / 180.0f;
        const float roll = smooth_.headRollDeg * kPi / 180.0f;
        const float cy = std::cos(yaw);
        const float sy = std::sin(yaw);
        const float cp = std::cos(pitch);
        const float sp = std::sin(pitch);
        const float cr = std::cos(roll);
        const float sr = std::sin(roll);
        const float x1 = cy * direction.x - sy * direction.y;
        const float y1 = sy * direction.x + cy * direction.y;
        const float x2 = cp * x1 - sp * direction.z;
        const float z2 = sp * x1 + cp * direction.z;
        return normalize({ x2, cr * y1 - sr * z2, sr * y1 + cr * z2 });
    }

    Vec3 transformedDirection(float azimuthDeg, float elevationDeg) const
    {
        return transformedVector(directionFromAed(azimuthDeg, elevationDeg));
    }

    std::array<float, kAmbiEnvironmentMaxChannels> machineSpatialBasis() const
    {
        const bool fixedMachine = params_.scene == AmbiEnvironmentScene::Industrial
            || params_.scene == AmbiEnvironmentScene::Interior;
        const float az = fixedMachine
            ? (params_.scene == AmbiEnvironmentScene::Interior ? 155.0f : 105.0f)
            : -165.0f + machineTravel_ * 330.0f;
        const float el = fixedMachine
            ? (params_.scene == AmbiEnvironmentScene::Interior ? -8.0f : 6.0f)
            : 16.0f + std::sin(machineTravel_ * kPi) * 67.0f;
        return acnSn3dBasis7(transformedDirection(az, el));
    }

    double sampleRate_ = 48000.0;
    AmbiEnvironmentParams params_ {};
    AmbiEnvironmentParams smooth_ {};
    uint32_t rng_ = 1977u;
    uint64_t sampleCounter_ = 0u;
    float scenePhase_ = 0.0f;
    float walkPhase_ = 0.0f;
    float walkShapePhase_ = 0.0f;
    float worldHeadingDeg_ = 0.0f;
    float gust_ = 0.0f;
    float machinePhase_ = 0.0f;
    float machineTravel_ = 0.0f;
    float machineNoise_ = 0.0f;
    float outputGainSmoothed_ = 0.0f;
    float globalEnergy_ = 0.0f;
    std::array<AmbiEnvironmentCell, kAmbiEnvironmentCellCount> cells_ {};
    std::array<std::array<float, kAmbiEnvironmentMaxChannels>, kAmbiEnvironmentCellCount> cellBasis_ {};
    std::array<float, kAmbiEnvironmentCellCount> worldAzimuth_ {};
    std::array<float, kAmbiEnvironmentCellCount> worldElevation_ {};
    std::array<float, kAmbiEnvironmentCellCount> worldRadius_ {};
    std::array<float, kAmbiEnvironmentCellCount> worldDriftPhase_ {};
    std::array<float, kAmbiEnvironmentCellCount> windZone_ {};
    std::array<float, kAmbiEnvironmentCellCount> rainZone_ {};
    std::array<float, kAmbiEnvironmentCellCount> waterZone_ {};
    std::array<float, kAmbiEnvironmentCellCount> fireZone_ {};
    std::array<uint32_t, 3> insectCells_ {};
    std::array<uint32_t, 2> waterCells_ {};
    std::array<uint32_t, 2> fireCells_ {};
    std::array<BedState, kAmbiEnvironmentCellCount> bed_ {};
    std::array<float, kAmbiEnvironmentCellCount> eventAir_ {};
    std::array<ParticleVoice, 24> rainVoices_ {};
    std::array<ParticleVoice, 16> bubbleVoices_ {};
    std::array<ParticleVoice, 12> crackleVoices_ {};
    std::array<InsectVoice, 8> insectVoices_ {};
    std::array<float, 6> layerLevels_ {};
};

} // namespace s3g
