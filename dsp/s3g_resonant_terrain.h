#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"
#include "s3g_topology.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kResonantTerrainChannels = 8;
constexpr uint32_t kResonantTerrainModes = 96;
constexpr uint32_t kResonantTerrainVoices = 192;

struct ResonantTerrainParams {
    float baseHz = 72.0f;
    float density = 0.62f;
    float decay = 0.72f;
    float brightness = 0.58f;
    float harmonicity = 0.48f;
    float exciterTone = 0.42f;
    float midiInfluence = 0.65f;
    float outputGainDb = -12.0f;
};

struct ResonantTerrainLaneParams {
    float pitchSemitones = 0.0f;
    float brightness = 0.5f;
    float decay = 0.5f;
    float strike = 0.5f;
    float density = 0.5f;
    float material = 0.5f;
    float articulation = 0.5f;
    float size = 0.5f;
    float energy = 0.5f;
    float coupling = 0.0f;
    float pressure = 0.0f;
};

class ResonantTerrain {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        phaseNoise_ = 0x4d3a571bu;
        midiHz_ = 110.0f;
        midiGate_ = 0.0f;
        midiGateTarget_ = 0.0f;
        outputGain_ = 0.25f;
        outputGainTarget_ = 0.25f;
        eventCountdown_ = 0.0f;
        eventPhase_ = 0.0f;
        for (uint32_t lane = 0; lane < kResonantTerrainChannels; ++lane) {
            laneParams_[lane] = {};
            laneDc_[lane] = 0.0f;
        }
        for (auto& voice : voices_) {
            voice.active = false;
        }
        rebuildModes();
    }

    void reset()
    {
        for (auto& voice : voices_) {
            voice.active = false;
        }
        laneDc_.fill(0.0f);
        eventCountdown_ = 0.0f;
        eventPhase_ = 0.0f;
        midiGate_ = 0.0f;
        midiGateTarget_ = 0.0f;
    }

    void setParams(const ResonantTerrainParams& params)
    {
        const float oldBase = params_.baseHz;
        const float oldHarm = params_.harmonicity;
        params_ = params;
        params_.baseHz = clamp(params_.baseHz, 24.0f, 880.0f);
        params_.density = clamp(params_.density, 0.0f, 1.0f);
        params_.decay = clamp(params_.decay, 0.0f, 1.0f);
        params_.brightness = clamp(params_.brightness, 0.0f, 1.0f);
        params_.harmonicity = clamp(params_.harmonicity, 0.0f, 1.0f);
        params_.exciterTone = clamp(params_.exciterTone, 0.0f, 1.0f);
        params_.midiInfluence = clamp(params_.midiInfluence, 0.0f, 1.0f);
        params_.outputGainDb = clamp(params_.outputGainDb, -48.0f, -3.0f);
        outputGainTarget_ = static_cast<float>(std::pow(10.0, params_.outputGainDb / 20.0f));
        if (std::fabs(oldBase - params_.baseHz) > 0.01f || std::fabs(oldHarm - params_.harmonicity) > 0.0001f) {
            rebuildModes();
        }
    }

    void setLaneParams(uint32_t lane, const ResonantTerrainLaneParams& params)
    {
        if (lane >= kResonantTerrainChannels) {
            return;
        }
        laneParams_[lane].pitchSemitones = clamp(params.pitchSemitones, -24.0f, 24.0f);
        laneParams_[lane].brightness = clamp(params.brightness, 0.0f, 1.0f);
        laneParams_[lane].decay = clamp(params.decay, 0.0f, 1.0f);
        laneParams_[lane].strike = clamp(params.strike, 0.0f, 1.0f);
        laneParams_[lane].density = clamp(params.density, 0.0f, 1.0f);
        laneParams_[lane].material = clamp(params.material, 0.0f, 1.0f);
        laneParams_[lane].articulation = clamp(params.articulation, 0.0f, 1.0f);
        laneParams_[lane].size = clamp(params.size, 0.0f, 1.0f);
        laneParams_[lane].energy = clamp(params.energy, 0.0f, 1.0f);
        laneParams_[lane].coupling = clamp(params.coupling, 0.0f, 1.0f);
        laneParams_[lane].pressure = clamp(params.pressure, 0.0f, 1.0f);
    }

    void noteOn(int key, float velocity)
    {
        if (key < 0 || key > 127) {
            return;
        }
        midiHz_ = 440.0f * std::pow(2.0f, (static_cast<float>(key) - 69.0f) / 12.0f);
        midiGateTarget_ = clamp(velocity, 0.0f, 1.0f);
        launchTerrainEvent(true, midiGateTarget_);
    }

    void noteOff(int key)
    {
        (void)key;
        midiGateTarget_ = 0.0f;
    }

    void processFrame(float* output)
    {
        if (!output) {
            return;
        }
        for (uint32_t lane = 0; lane < kResonantTerrainChannels; ++lane) {
            output[lane] = 0.0f;
        }

        outputGain_ += (outputGainTarget_ - outputGain_) * 0.0008f;
        midiGate_ += (midiGateTarget_ - midiGate_) * 0.0012f;
        eventPhase_ += 1.0f / static_cast<float>(sampleRate_);

        if (params_.density > 0.0001f) {
            eventCountdown_ -= 1.0f;
            if (eventCountdown_ <= 0.0f) {
                const float topologyDensity = averageLaneDensity();
                const float eventForce = clamp(0.38f + topologyDensity * 0.92f, 0.0f, 1.35f);
                launchTerrainEvent(false, eventForce);
                const float eventsPerSecond = 0.35f + params_.density * params_.density * (3.2f + topologyDensity * 10.8f);
                const float jitter = 0.64f + rand01() * (0.78f - topologyDensity * 0.24f);
                eventCountdown_ = static_cast<float>(sampleRate_) * jitter / std::max(0.05f, eventsPerSecond);
            }
        }

        const float sr = static_cast<float>(sampleRate_);
        float peak = 0.0f;
        for (auto& voice : voices_) {
            if (!voice.active) {
                continue;
            }
            voice.age += 1.0f;
            if (voice.age >= voice.length) {
                voice.active = false;
                continue;
            }
            const float u = voice.age / std::max(1.0f, voice.length);
            const float attack = std::max(2.0f, voice.attack);
            float env = std::exp(-u * voice.decayShape);
            if (voice.age < attack) {
                const float a = voice.age / attack;
                env *= a * a * (3.0f - 2.0f * a);
            }
            const float release = std::min(0.18f, 1.0f / std::max(2.0f, voice.length * 0.02f));
            if (u > 1.0f - release) {
                const float r = clamp((1.0f - u) / release, 0.0f, 1.0f);
                env *= r * r * (3.0f - 2.0f * r);
            }
            const float drop = voice.pitchDrop * std::exp(-u * 9.0f);
            const float bend = 1.0f + voice.bendDepth * std::sin(6.28318530717958647692f * (voice.bendPhase + voice.bendRate * voice.age / sr)) - drop;
            voice.phase += 6.28318530717958647692f * voice.freq * std::max(0.08f, bend) / sr;
            if (voice.phase >= 6.28318530717958647692f) {
                voice.phase -= 6.28318530717958647692f * std::floor(voice.phase / 6.28318530717958647692f);
            }
            const float clickEnv = std::exp(-u * 80.0f);
            const float rawNoise = rand01() * 2.0f - 1.0f;
            const float noiseCoeff = 0.04f + voice.noiseTone * 0.45f;
            voice.noiseState += (rawNoise - voice.noiseState) * noiseCoeff;
            const float resonant = std::sin(voice.phase) + voice.secondAmp * std::sin(voice.phase * voice.secondRatio + voice.bendPhase * 6.28318530717958647692f);
            const float value = (resonant * env + voice.clickAmp * clickEnv + voice.noiseAmp * voice.noiseState * env) * voice.amp;
            output[voice.lane] += value;
            if (voice.spread > 0.0001f) {
                const uint32_t left = (voice.lane + kResonantTerrainChannels - 1u) % kResonantTerrainChannels;
                const uint32_t right = (voice.lane + 1u) % kResonantTerrainChannels;
                output[left] += value * voice.spread * 0.38f;
                output[right] += value * voice.spread * 0.38f;
            }
        }

        for (uint32_t lane = 0; lane < kResonantTerrainChannels; ++lane) {
            laneDc_[lane] += (output[lane] - laneDc_[lane]) * 0.0007f;
            const float dcProtected = output[lane] - laneDc_[lane];
            const float shaped = std::tanh(dcProtected * 1.35f) * outputGain_;
            output[lane] = flushDenormal(shaped);
            peak = std::max(peak, std::fabs(output[lane]));
        }
        if (peak > 0.98f) {
            const float trim = 0.98f / peak;
            for (uint32_t lane = 0; lane < kResonantTerrainChannels; ++lane) {
                output[lane] *= trim;
            }
        }
    }

private:
    struct Voice {
        bool active = false;
        uint32_t lane = 0;
        float freq = 110.0f;
        float phase = 0.0f;
        float amp = 0.0f;
        float age = 0.0f;
        float length = 1.0f;
        float attack = 32.0f;
        float decayShape = 5.0f;
        float bendPhase = 0.0f;
        float bendRate = 0.05f;
        float bendDepth = 0.0f;
        float pitchDrop = 0.0f;
        float clickAmp = 0.0f;
        float noiseAmp = 0.0f;
        float noiseState = 0.0f;
        float noiseTone = 0.5f;
        float secondAmp = 0.0f;
        float secondRatio = 2.0f;
        float spread = 0.0f;
    };

    float rand01()
    {
        phaseNoise_ ^= phaseNoise_ << 13;
        phaseNoise_ ^= phaseNoise_ >> 17;
        phaseNoise_ ^= phaseNoise_ << 5;
        return static_cast<float>(phaseNoise_ & 0x00ffffffu) / static_cast<float>(0x01000000u);
    }

    void rebuildModes()
    {
        static constexpr float metals[] = { 1.0f, 1.4142f, 1.6180f, 2.2361f, 2.7183f, 3.1416f, 4.2361f, 5.3852f };
        float maxAmp = 0.0001f;
        for (uint32_t mode = 0; mode < kResonantTerrainModes; ++mode) {
            const float cluster = laneNoise(mode * 13u + 5u) * 5.8f;
            const float jitter = laneNoise(mode * 31u + 11u) * (0.12f + params_.harmonicity * 0.32f);
            const float cloud = params_.baseHz * std::pow(2.0f, cluster + jitter);
            const float degree = metals[mode % 8u];
            const float lattice = params_.baseHz * degree * std::pow(2.0f, static_cast<float>(mode / 8u) * 0.34f + laneNoise(mode * 7u + 19u) * 0.16f);
            const float harmonic = params_.baseHz * std::pow(static_cast<float>(mode + 1u), 1.0f + params_.harmonicity * 0.24f);
            float freq = lerp(lerp(cloud, lattice, 0.42f), harmonic, params_.harmonicity * 0.44f);
            while (freq > sampleRate_ * 0.43) {
                freq *= 0.5f;
            }
            modeFreq_[mode] = clamp(freq, 18.0f, static_cast<float>(sampleRate_ * 0.43));
            const float freqRatio = modeFreq_[mode] / std::max(1.0f, params_.baseHz);
            modeAmp_[mode] = std::pow(freqRatio, -0.20f - params_.brightness * 0.88f) * (0.42f + 0.58f * randHash(mode * 37u + 9u));
            maxAmp = std::max(maxAmp, modeAmp_[mode]);
        }
        for (float& amp : modeAmp_) {
            amp /= maxAmp;
        }
    }

    float randHash(uint32_t seed) const
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return static_cast<float>(seed & 0x00ffffffu) / static_cast<float>(0x01000000u);
    }

    float averageLaneDensity() const
    {
        float total = 0.0f;
        for (const auto& lane : laneParams_) {
            total += clamp(lane.density, 0.0f, 1.0f);
        }
        return total / static_cast<float>(kResonantTerrainChannels);
    }

    uint32_t pickLane(bool midi)
    {
        float total = 0.0f;
        for (uint32_t lane = 0; lane < kResonantTerrainChannels; ++lane) {
            total += std::max(0.0f, laneParams_[lane].density) + (midi ? 0.10f : 0.0f);
        }
        if (total <= 0.0001f) {
            return static_cast<uint32_t>(rand01() * kResonantTerrainChannels) % kResonantTerrainChannels;
        }
        float r = rand01() * total;
        for (uint32_t lane = 0; lane < kResonantTerrainChannels; ++lane) {
            r -= std::max(0.0f, laneParams_[lane].density) + (midi ? 0.10f : 0.0f);
            if (r <= 0.0f) {
                return lane;
            }
        }
        return kResonantTerrainChannels - 1u;
    }

    Voice* allocateVoice()
    {
        for (auto& voice : voices_) {
            if (!voice.active) {
                return &voice;
            }
        }
        Voice* quietest = &voices_[0];
        float oldest = voices_[0].age / std::max(1.0f, voices_[0].length);
        for (auto& voice : voices_) {
            const float u = voice.age / std::max(1.0f, voice.length);
            if (u > oldest) {
                oldest = u;
                quietest = &voice;
            }
        }
        return quietest;
    }

    void launchTerrainEvent(bool midi, float velocity)
    {
        const float topologyDensity = averageLaneDensity();
        const uint32_t lanesForEvent = midi ? 2u : 1u + static_cast<uint32_t>(rand01() * (1.0f + topologyDensity * 2.0f));
        const uint32_t basePartials = midi ? 10u : static_cast<uint32_t>(2.0f + params_.exciterTone * 4.0f + topologyDensity * 8.0f + rand01() * 4.0f);
        for (uint32_t lanePick = 0; lanePick < lanesForEvent; ++lanePick) {
            const uint32_t lane = pickLane(midi);
            const auto lp = laneParams_[lane];
            const float laneDensity = clamp(lp.density, 0.0f, 1.0f);
            const float material = clamp(lp.material, 0.0f, 1.0f);
            const float articulation = clamp(lp.articulation, 0.0f, 1.0f);
            const float size = clamp(lp.size, 0.0f, 1.0f);
            const float energy = clamp(lp.energy, 0.0f, 1.0f);
            const float coupling = clamp(lp.coupling, 0.0f, 1.0f);
            const float pressure = clamp(lp.pressure, 0.0f, 1.0f);

            const float body = clamp((0.38f - material) / 0.38f, 0.0f, 1.0f) * clamp((0.62f - articulation) / 0.62f, 0.0f, 1.0f);
            const float skin = clamp(1.0f - std::fabs(material - 0.23f) / 0.30f, 0.0f, 1.0f) * clamp(1.0f - std::fabs(articulation - 0.52f) / 0.55f, 0.0f, 1.0f);
            const float wood = clamp(1.0f - std::fabs(material - 0.12f) / 0.24f, 0.0f, 1.0f) * clamp((articulation - 0.36f) / 0.55f, 0.0f, 1.0f);
            const float modal = clamp(1.0f - std::fabs(material - 0.52f) / 0.34f, 0.0f, 1.0f) * clamp(1.0f - std::fabs(articulation - 0.48f) / 0.62f, 0.0f, 1.0f);
            const float metal = clamp((material - 0.46f) / 0.54f, 0.0f, 1.0f) * clamp((1.05f - articulation) / 0.78f, 0.0f, 1.0f);
            const float rim = clamp((articulation - 0.72f) / 0.28f, 0.0f, 1.0f) * clamp(0.45f + material * 0.75f, 0.0f, 1.0f);
            const float noise = clamp((articulation - 0.58f) / 0.42f, 0.0f, 1.0f) * clamp(0.25f + (1.0f - std::fabs(material - 0.78f) / 0.45f), 0.0f, 1.0f);

            uint32_t partials = std::max<uint32_t>(1u, static_cast<uint32_t>(std::round(static_cast<float>(basePartials) * (0.38f + laneDensity * 0.92f + metal * 0.50f + noise * 0.34f - body * 0.20f))));
            partials = std::min<uint32_t>(24u, partials);
            const float eventGain = (midi ? 1.15f : 0.72f) * (0.42f + velocity * 0.66f) * (0.62f + energy * 0.78f + pressure * 0.28f) / std::sqrt(static_cast<float>(partials));
            const float basePitchRatio = std::pow(2.0f, lp.pitchSemitones / 12.0f);
            const float midiRatio = midi ? midiHz_ / std::max(24.0f, params_.baseHz) : lerp(1.0f, midiHz_ / std::max(24.0f, params_.baseHz), midiGate_ * params_.midiInfluence * 0.35f);
            const float sizePitch = std::pow(2.0f, lerp(-1.25f, 1.35f, size));
            const float laneBright = clamp(params_.brightness * 0.54f + lp.brightness * 0.44f + metal * 0.26f + rim * 0.35f + noise * 0.18f - body * 0.24f, 0.0f, 1.35f);
            const float laneDecay = clamp(params_.decay * 0.58f + lp.decay * 0.35f + body * 0.34f + metal * 0.22f - rim * 0.36f - wood * 0.28f, 0.0f, 1.35f);
            const float laneStrike = clamp(0.20f + lp.strike * 0.70f + articulation * 0.62f + energy * 0.42f + (midi ? velocity * 0.65f : 0.0f), 0.0f, 2.1f);

            for (uint32_t i = 0; i < partials; ++i) {
                const float modalPick = clamp((lp.pitchSemitones + 24.0f) / 48.0f * 0.48f + size * 0.40f + rand01() * 0.32f - 0.12f, 0.0f, 1.0f);
                float shapedPick = modalPick;
                if (body > 0.25f) {
                    shapedPick = clamp(rand01() * 0.22f + size * 0.20f, 0.0f, 1.0f);
                } else if (wood > 0.35f || rim > 0.45f) {
                    shapedPick = clamp(0.28f + rand01() * 0.45f + size * 0.20f, 0.0f, 1.0f);
                } else if (metal > 0.45f || noise > 0.45f) {
                    shapedPick = clamp(0.38f + rand01() * 0.58f + size * 0.12f, 0.0f, 1.0f);
                }
                const uint32_t mode = std::min<uint32_t>(kResonantTerrainModes - 1u, static_cast<uint32_t>(shapedPick * static_cast<float>(kResonantTerrainModes - 1u)));
                const float modeNorm = static_cast<float>(mode) / static_cast<float>(kResonantTerrainModes - 1u);
                const float inharmonic = clamp(material * 0.85f + metal * 0.55f + noise * 0.28f - body * 0.35f, 0.0f, 1.4f);
                const float detune = 1.0f + (rand01() * 2.0f - 1.0f) * (0.0008f + inharmonic * 0.012f + laneDensity * 0.002f);
                const float familyRatio = body > 0.45f ? (0.50f + 0.40f * rand01()) : wood > 0.42f ? (0.82f + 0.55f * rand01()) : 1.0f;
                const float freq = clamp(modeFreq_[mode] * basePitchRatio * midiRatio * sizePitch * detune * familyRatio, 18.0f, static_cast<float>(sampleRate_ * 0.43));
                const float decayMs = (55.0f + laneDecay * laneDecay * 3600.0f) * (0.54f + rand01() * 1.05f) * (1.16f - modeNorm * laneBright * 0.62f) * (1.55f - articulation * 0.72f) * (0.80f + pressure * 0.35f);
                const float length = std::max(32.0f, decayMs * static_cast<float>(sampleRate_) / 1000.0f * (1.55f + body * 1.65f + metal * 0.60f - rim * 0.82f - wood * 0.48f));
                const float attackMs = (0.55f + (1.0f - articulation) * 34.0f + body * 12.0f + noise * 3.0f - rim * 0.45f);
                Voice* voice = allocateVoice();
                voice->active = true;
                voice->lane = lane;
                voice->freq = freq;
                voice->phase = rand01() * 6.28318530717958647692f;
                voice->age = 0.0f;
                voice->length = length;
                voice->attack = std::max(1.0f, attackMs * static_cast<float>(sampleRate_) / 1000.0f);
                voice->decayShape = clamp(2.4f + (1.0f - laneDecay) * 3.2f + modeNorm * (0.65f + laneBright * 1.65f) + rim * 3.0f + wood * 1.5f - body * 0.9f, 0.75f, 9.5f);
                voice->bendPhase = rand01();
                voice->bendRate = 0.025f + rand01() * 0.12f;
                voice->bendDepth = (0.0004f + params_.harmonicity * 0.0026f + laneDensity * 0.0012f + metal * 0.0038f + noise * 0.0045f) * (midi ? 0.55f : 1.0f);
                voice->pitchDrop = clamp(skin * 0.075f + body * 0.050f + wood * 0.018f - metal * 0.012f, 0.0f, 0.12f);
                voice->clickAmp = clamp(rim * 1.30f + wood * 0.42f + articulation * 0.18f, 0.0f, 1.55f);
                voice->noiseAmp = clamp(noise * 1.10f + rim * 0.22f + skin * 0.15f, 0.0f, 1.35f);
                voice->noiseState = 0.0f;
                voice->noiseTone = clamp(0.18f + noise * 0.68f + rim * 0.38f + material * 0.26f, 0.0f, 1.0f);
                voice->secondAmp = clamp(modal * 0.22f + metal * 0.38f + body * 0.10f + skin * 0.18f, 0.0f, 0.62f);
                voice->secondRatio = metal > 0.42f ? lerp(1.4142f, 2.7183f, rand01()) : body > 0.35f ? lerp(1.50f, 2.00f, rand01()) : lerp(1.96f, 2.04f, rand01());
                voice->spread = clamp(0.035f + params_.density * 0.08f + laneDensity * 0.16f + coupling * 0.30f + pressure * 0.08f, 0.0f, 0.55f);
                const float brightTilt = std::pow(1.0f - modeNorm * 0.78f, 0.30f + (1.0f - laneBright) * 1.25f + body * 0.55f);
                voice->amp = eventGain * laneStrike * (0.54f + laneDensity * 0.55f + energy * 0.42f) * modeAmp_[mode] * brightTilt;
            }

            if (!midi && coupling > 0.18f && rand01() < coupling * 0.78f) {
                const uint32_t echoLane = (lane + 1u + static_cast<uint32_t>(rand01() * 2.0f)) % kResonantTerrainChannels;
                ResonantTerrainLaneParams original = laneParams_[echoLane];
                laneParams_[echoLane].density = std::max(laneParams_[echoLane].density, laneDensity * 0.72f);
                laneParams_[echoLane].material = lerp(laneParams_[echoLane].material, material, 0.45f);
                laneParams_[echoLane].articulation = std::min(1.0f, articulation + 0.08f);
                laneParams_[echoLane].energy = std::max(laneParams_[echoLane].energy, energy * 0.66f);
                if (rand01() < 0.35f + pressure * 0.35f) {
                    const uint32_t savedLane = echoLane;
                    (void)savedLane;
                    const float delayedVelocity = velocity * (0.35f + coupling * 0.38f);
                    // Immediate micro-flam; the envelope attack makes this read as a coupled neighbor hit.
                    launchNeighborFlam(echoLane, delayedVelocity, material, articulation, size, energy, coupling, pressure);
                }
                laneParams_[echoLane] = original;
            }
        }
    }

    void launchNeighborFlam(uint32_t lane, float velocity, float material, float articulation, float size, float energy, float coupling, float pressure)
    {
        const auto lp = laneParams_[lane];
        const uint32_t partials = 1u + static_cast<uint32_t>(coupling * 4.0f + rand01() * 2.0f);
        const float basePitchRatio = std::pow(2.0f, (lp.pitchSemitones + (rand01() * 2.0f - 1.0f) * 3.5f) / 12.0f);
        const float eventGain = 0.34f * velocity * (0.55f + energy * 0.65f) / std::sqrt(static_cast<float>(partials));
        for (uint32_t i = 0; i < partials; ++i) {
            const float pick = clamp(size * 0.42f + material * 0.34f + rand01() * 0.34f, 0.0f, 1.0f);
            const uint32_t mode = std::min<uint32_t>(kResonantTerrainModes - 1u, static_cast<uint32_t>(pick * static_cast<float>(kResonantTerrainModes - 1u)));
            Voice* voice = allocateVoice();
            voice->active = true;
            voice->lane = lane;
            voice->freq = clamp(modeFreq_[mode] * basePitchRatio * std::pow(2.0f, (size - 0.5f) * 0.65f), 18.0f, static_cast<float>(sampleRate_ * 0.43));
            voice->phase = rand01() * 6.28318530717958647692f;
            voice->age = 0.0f;
            voice->length = static_cast<float>(sampleRate_) * (0.045f + pressure * 0.18f + (1.0f - articulation) * 0.16f);
            voice->attack = static_cast<float>(sampleRate_) * (0.0007f + (1.0f - articulation) * 0.004f);
            voice->decayShape = 4.8f + articulation * 3.0f;
            voice->bendPhase = rand01();
            voice->bendRate = 0.06f + rand01() * 0.12f;
            voice->bendDepth = 0.001f + material * 0.004f;
            voice->pitchDrop = clamp((1.0f - material) * 0.035f, 0.0f, 0.08f);
            voice->clickAmp = clamp(articulation * 0.95f, 0.0f, 1.1f);
            voice->noiseAmp = clamp((articulation - 0.45f) * 0.65f, 0.0f, 0.8f);
            voice->noiseState = 0.0f;
            voice->noiseTone = clamp(0.2f + articulation * 0.6f, 0.0f, 1.0f);
            voice->secondAmp = clamp(material * 0.35f, 0.0f, 0.5f);
            voice->secondRatio = lerp(1.5f, 2.6f, material);
            voice->spread = clamp(0.08f + coupling * 0.34f, 0.0f, 0.48f);
            voice->amp = eventGain * (0.8f + pressure * 0.45f) * modeAmp_[mode];
        }
    }

    double sampleRate_ = 48000.0;
    float eventCountdown_ = 0.0f;
    float eventPhase_ = 0.0f;
    uint32_t phaseNoise_ = 0x4d3a571bu;
    ResonantTerrainParams params_ {};
    std::array<ResonantTerrainLaneParams, kResonantTerrainChannels> laneParams_ {};
    std::array<float, kResonantTerrainModes> modeFreq_ {};
    std::array<float, kResonantTerrainModes> modeAmp_ {};
    std::array<Voice, kResonantTerrainVoices> voices_ {};
    std::array<float, kResonantTerrainChannels> laneDc_ {};
    float midiHz_ = 110.0f;
    float midiGate_ = 0.0f;
    float midiGateTarget_ = 0.0f;
    float outputGain_ = 0.25f;
    float outputGainTarget_ = 0.25f;
};

} // namespace s3g
