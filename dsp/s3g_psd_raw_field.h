#pragma once

#include "s3g_macro_shred.h"
#include "s3g_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kPsdRawFieldChannels = 8;
constexpr uint32_t kPsdRawFieldTapeSize = 65536;
constexpr uint32_t kPsdRawFieldCodebookSize = 32;

enum class PsdRawFieldCodecMode : uint32_t {
    RawPcm = 0,
    DeltaPcm = 1,
    Adpcm = 2,
    MuLaw = 3,
    ALaw = 4,
    CelpScramble = 5,
};

enum class PsdRawFieldChannelScheme : uint32_t {
    Parallel = 0,
    Deinterleave = 1,
    Planes = 2,
    Shuffled = 3,
    Divergent = 4,
};

struct PsdRawFieldParams {
    float scanRate = 0.44f;
    float texture = 0.62f;
    float geometry = 0.64f;
    float chaos = 0.58f;
    float fold = 0.68f;
    float evolve = 0.0f;
    PsdRawFieldChannelScheme channelScheme = PsdRawFieldChannelScheme::Deinterleave;
    float channelSpread = 0.72f;
    PsdRawFieldCodecMode codecMode = PsdRawFieldCodecMode::MuLaw;
    float codecRate = 0.35f;
    float bitDepth = 8.0f;
    float codecDamage = 0.28f;
    float drive = 0.68f;
    float shred = 0.58f;
    float resonance = 0.18f;
    float gainDb = -12.0f;
    uint32_t seed = 0x50434431u;
};

class PsdRawField {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        shaper_.prepare(sampleRate_, kPsdRawFieldChannels);
        setParams(params_);
        reset();
        ready_ = true;
    }

    void reset()
    {
        tapeSeed_ = params_.seed ? params_.seed : 0x50434431u;
        for (uint32_t i = 0; i < kPsdRawFieldTapeSize; ++i) {
            tape_[i] = renderVirtualByte(i);
        }
        evolveTarget_ = tape_;
        evolveSeed_ = hash(tapeSeed_ ^ 0xb5297a4du);
        evolveRegionStart_ = 0u;
        evolveRegionLength_ = 0u;
        evolveRegionCounter_ = 0u;
        evolveBlend_ = 0.0f;
        evolveWaitSamples_ = params_.evolve > 0.0001f ? 0.0f : static_cast<float>(sampleRate_);
        evolveActive_ = false;
        for (uint32_t ch = 0; ch < kPsdRawFieldChannels; ++ch) {
            cursor_[ch] = static_cast<float>((ch * 7919u + 257u) & (kPsdRawFieldTapeSize - 1u));
            prev_[ch] = 0.0f;
            textureState_[ch] = 0.0f;
            dcX_[ch] = 0.0f;
            dcY_[ch] = 0.0f;
            connectStart_[ch] = 0.0f;
            connectTarget_[ch] = 0.0f;
            connectCurrent_[ch] = 0.0f;
            connectPhase_[ch] = 1.0f;
            connectInc_[ch] = 1.0f;
            connectSeed_[ch] = hash(tapeSeed_ ^ (ch * 0x9e3779b9u) ^ 0x31415926u);
            breakpoint_[ch] = BreakpointState {};
            breakpoint_[ch].seed = hash(tapeSeed_ ^ (ch * 0x7f4a7c15u) ^ 0x27182818u);
            breakpoint_[ch].currentAmp = readTapeAudio(distributedPosition(ch * 4099u, ch, 0u));
            breakpoint_[ch].nextAmp = readTapeAudio(distributedPosition(ch * 4099u + 257u, ch, 1u));
            breakpoint_[ch].phase = 1.0f;
            breakpoint_[ch].inc = 1.0f;
            hold_[ch] = 0.0f;
            held_[ch] = 0.0f;
            predictor_[ch] = 0.0f;
            step_[ch] = 0.035f;
            frameDrop_[ch] = false;
            codeGain_[ch] = 0.5f;
            codePitch_[ch] = 48u;
            pitch_[ch].fill(0.0f);
            pitchWrite_[ch] = 0u;
        }
        shaper_.reset();
        sectionPhase_ = 0.0f;
        frameSample_ = 0u;
        loudnessEnergy_ = 0.04f;
        loudnessGain_ = 1.0f;
    }

    void setParams(const PsdRawFieldParams& params)
    {
        const PsdRawFieldParams sanitized = sanitize(params);
        if (params_.evolve <= 0.0001f && sanitized.evolve > 0.0001f && !evolveActive_) {
            evolveWaitSamples_ = 0.0f;
        }
        params_ = sanitized;
    }

    PsdRawFieldParams params() const { return params_; }
    bool ready() const { return ready_; }
    uint8_t byteAt(uint32_t index) const
    {
        return static_cast<uint8_t>(clamp((readTapeAudio(index) * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
    }
    float cursorPosition(uint32_t channel) const { return channel < kPsdRawFieldChannels ? cursor_[channel] : 0.0f; }

    void randomizeByteField(uint32_t salt)
    {
        params_.seed = hash(params_.seed ^ salt ^ 0x7f4a7c15u);
        reset();
    }

    void process(float* const* output, uint32_t outputChannels, uint32_t frames)
    {
        if (!ready_ || !output || frames == 0u) return;
        const uint32_t channels = std::min<uint32_t>(outputChannels, kPsdRawFieldChannels);
        const float gain = dbToGain(params_.gainDb);
        constexpr float dcR = 0.9975f;
        const float baseStep = 0.00002f * std::pow(375000.0f, params_.scanRate);
        const float sectionRate = lerp(0.0000003f, 0.00008f, params_.scanRate * params_.scanRate);
        const uint32_t frameSamples = static_cast<uint32_t>(std::max(1.0, sampleRate_ * 0.010));
        const float shapeMix = clamp(std::max({ params_.drive * 0.9f, params_.shred, params_.resonance }), 0.0f, 1.0f);
        const float calibrationGain = dbToGain(calibrationDb());
        const float energyAttack = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.12));
        const float energyRelease = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.90));
        const float gainSmoothing = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.60));
        configureShaper();

        for (uint32_t i = 0; i < frames; ++i) {
            advanceEvolution();
            sectionPhase_ += sectionRate;
            sectionPhase_ -= std::floor(sectionPhase_);
            const float section = triangle(sectionPhase_);
            const bool newCodecFrame = frameSample_ == 0u;
            std::array<float, kPsdRawFieldChannels> shapedInput {};
            std::array<float, kPsdRawFieldChannels> shapedOutput {};

            for (uint32_t ch = 0; ch < channels; ++ch) {
                const float planeSkew = 1.0f
                    + (static_cast<float>(ch) - 3.5f) * 0.014f * params_.chaos * params_.channelSpread;
                const float rowBump = 1.0f + 0.48f * params_.texture * pulse(rowPhase(ch), 0.035f);
                cursor_[ch] += baseStep * planeSkew * rowBump;
                while (cursor_[ch] >= static_cast<float>(kPsdRawFieldTapeSize)) {
                    cursor_[ch] -= static_cast<float>(kPsdRawFieldTapeSize);
                }

                const uint32_t basePos = static_cast<uint32_t>(cursor_[ch]) & (kPsdRawFieldTapeSize - 1u);
                const uint32_t pos = distributedPosition(basePos, ch, 0u);
                const uint32_t stride = 1u + ((ch * 17u) & 31u);
                const float a = readTapeAudio(pos);
                const float b = readTapeAudio(distributedPosition(basePos + stride, ch, 1u));
                const float c = readTapeAudio(distributedPosition(basePos + 257u + ch * 97u, ch, 2u));

                const float localSmoothing = lerp(0.84f, 0.16f, params_.texture);
                float raw = lerp(a, (a + b) * 0.5f, localSmoothing);
                raw = lerp(raw, c, params_.texture * (0.20f + 0.62f * section));
                raw += metadataTick(pos, ch) * params_.texture * params_.texture * 0.65f;

                raw = breakpointStage(raw, ch, pos);
                const float textureCoeff = lerp(0.001f, 0.85f, params_.texture * params_.texture);
                textureState_[ch] += (raw - textureState_[ch]) * textureCoeff;
                raw = lerp(textureState_[ch], raw, params_.texture);
                const float folded = std::sin(raw * (1.0f + params_.fold * 8.5f));
                raw = lerp(raw, folded, params_.fold);
                raw = connectionStage(raw, ch, pos);
                raw = softClip(raw * 1.55f);

                const float smoothAmount = lerp(0.28f, 0.06f, params_.texture);
                const float smoothed = lerp(raw, prev_[ch], smoothAmount);
                prev_[ch] = smoothed;
                const float dc = smoothed - dcX_[ch] + dcR * dcY_[ch];
                dcX_[ch] = smoothed;
                dcY_[ch] = dc;
                if (newCodecFrame) updateCodecFrame(ch, pos);
                shapedInput[ch] = codecStage(dc, ch);
            }

            if (shapeMix > 0.0001f) shaper_.processFrame(shapedInput.data(), shapedOutput.data());
            else shapedOutput = shapedInput;

            std::array<float, kPsdRawFieldChannels> shapedFrame {};
            float mean = 0.0f;
            float originalEnergy = 0.0f;
            for (uint32_t ch = 0; ch < channels; ++ch) {
                shapedFrame[ch] = lerp(shapedInput[ch], shapedOutput[ch], shapeMix);
                mean += shapedFrame[ch];
                originalEnergy += shapedFrame[ch] * shapedFrame[ch];
            }
            mean /= static_cast<float>(std::max<uint32_t>(1u, channels));
            originalEnergy /= static_cast<float>(std::max<uint32_t>(1u, channels));

            float spatialEnergy = 0.0f;
            for (uint32_t ch = 0; ch < channels; ++ch) {
                const float residual = shapedFrame[ch] - mean;
                shapedFrame[ch] = mean + residual * params_.channelSpread;
                spatialEnergy += shapedFrame[ch] * shapedFrame[ch];
            }
            spatialEnergy /= static_cast<float>(std::max<uint32_t>(1u, channels));
            const float spreadGain = clamp(std::sqrt((originalEnergy + 1.0e-8f) / (spatialEnergy + 1.0e-8f)), 0.70f, 2.25f);

            float calibratedEnergy = 0.0f;
            for (uint32_t ch = 0; ch < channels; ++ch) {
                shapedFrame[ch] *= spreadGain * calibrationGain;
                calibratedEnergy += shapedFrame[ch] * shapedFrame[ch];
            }
            calibratedEnergy /= static_cast<float>(std::max<uint32_t>(1u, channels));
            const float energyCoeff = calibratedEnergy > loudnessEnergy_ ? energyAttack : energyRelease;
            loudnessEnergy_ += (calibratedEnergy - loudnessEnergy_) * energyCoeff;
            float loudnessTarget = 1.0f;
            if (loudnessEnergy_ > 1.0e-7f) {
                loudnessTarget = clamp(0.20f / std::sqrt(loudnessEnergy_), 0.75f, 1.35f);
            }
            loudnessGain_ += (loudnessTarget - loudnessGain_) * gainSmoothing;

            for (uint32_t ch = 0; ch < channels; ++ch) {
                if (output[ch]) output[ch][i] = softClip(shapedFrame[ch] * loudnessGain_ * gain);
            }
            for (uint32_t ch = channels; ch < outputChannels; ++ch) {
                if (output[ch]) output[ch][i] = 0.0f;
            }
            frameSample_ = (frameSample_ + 1u) % frameSamples;
        }
    }

private:
    enum class Distribution : uint32_t {
        Uniform = 0,
        Gaussian = 1,
        Cauchy = 2,
        Logistic = 3,
        Arcsine = 4,
        Exponential = 5,
        Binary = 6,
    };

    struct BreakpointState {
        uint32_t seed = 1u;
        float currentAmp = 0.0f;
        float nextAmp = 0.0f;
        float phase = 1.0f;
        float inc = 1.0f;
        float last = 0.0f;
        float tendencyCenter = 0.0f;
        bool resting = false;
    };

    static PsdRawFieldParams sanitize(PsdRawFieldParams p)
    {
        p.scanRate = clamp(p.scanRate, 0.0f, 1.0f);
        p.texture = clamp(p.texture, 0.0f, 1.0f);
        p.geometry = clamp(p.geometry, 0.0f, 1.0f);
        p.chaos = clamp(p.chaos, 0.0f, 1.0f);
        p.fold = clamp(p.fold, 0.0f, 1.0f);
        p.evolve = clamp(p.evolve, 0.0f, 1.0f);
        p.channelScheme = static_cast<PsdRawFieldChannelScheme>(std::min<uint32_t>(4u, static_cast<uint32_t>(p.channelScheme)));
        p.channelSpread = clamp(p.channelSpread, 0.0f, 1.0f);
        p.codecMode = static_cast<PsdRawFieldCodecMode>(std::min<uint32_t>(5u, static_cast<uint32_t>(p.codecMode)));
        p.codecRate = clamp(p.codecRate, 0.0f, 1.0f);
        p.bitDepth = clamp(p.bitDepth, 2.0f, 16.0f);
        p.codecDamage = clamp(p.codecDamage, 0.0f, 1.0f);
        p.drive = clamp(p.drive, 0.0f, 1.0f);
        p.shred = clamp(p.shred, 0.0f, 1.0f);
        p.resonance = clamp(p.resonance, 0.0f, 1.0f);
        p.gainDb = clamp(p.gainDb, -60.0f, 6.0f);
        return p;
    }

    void configureShaper()
    {
        MacroShredParams p {};
        p.inputGainDb = lerp(-3.0f, 36.0f, params_.drive);
        p.pressure = clamp(params_.drive * 0.82f + params_.shred * 0.18f, 0.0f, 1.0f);
        p.shred = params_.shred;
        p.feedback = params_.resonance * 0.90f;
        p.color = lerp(0.42f, 0.72f, params_.texture);
        p.react = lerp(0.22f, 0.62f, params_.chaos);
        p.tune = 0.94f;
        p.body = lerp(0.70f, 0.42f, params_.shred);
        p.spread = params_.channelSpread * 0.75f;
        p.deviation = params_.channelSpread * 0.82f;
        p.skew = (params_.chaos - 0.5f) * 0.8f;
        p.center = 0.5f;
        p.glideMs = lerp(180.0f, 35.0f, params_.chaos);
        p.mix = 1.0f;
        p.outputGainDb = -3.0f - 7.0f * clamp(params_.drive * 0.72f + params_.shred * 0.28f, 0.0f, 1.0f);
        shaper_.setParams(p);
    }

    float calibrationDb() const
    {
        float db = 9.0f * (params_.codecRate * params_.codecRate - 0.35f * 0.35f);
        db -= 3.0f * (params_.drive - 0.68f);
        db += 3.5f * (params_.shred - 0.58f);
        db -= 2.0f * (params_.resonance - 0.18f);
        db += 2.0f * (params_.geometry - 0.64f);
        if (params_.codecMode == PsdRawFieldCodecMode::CelpScramble) {
            db += 5.0f - 11.0f * (params_.codecDamage - 0.28f);
        }
        return clamp(db, -8.0f, 10.0f);
    }

    uint32_t distributedPosition(uint32_t pos, uint32_t ch, uint32_t lane) const
    {
        pos &= (kPsdRawFieldTapeSize - 1u);
        uint32_t distributed = pos;
        switch (params_.channelScheme) {
        case PsdRawFieldChannelScheme::Deinterleave:
            distributed = pos * kPsdRawFieldChannels + ch + lane * 257u;
            break;
        case PsdRawFieldChannelScheme::Planes:
            distributed = pos + ch * (kPsdRawFieldTapeSize / kPsdRawFieldChannels) + lane * 1021u;
            break;
        case PsdRawFieldChannelScheme::Shuffled:
            distributed = hash(pos ^ tapeSeed_ ^ (ch * 0x9e3779b9u) ^ (lane * 0x85ebca6bu));
            break;
        case PsdRawFieldChannelScheme::Divergent:
            distributed = pos * (3u + ch * 2u + lane * 11u) + ch * 4099u + lane * 8191u;
            break;
        case PsdRawFieldChannelScheme::Parallel:
        default:
            distributed = pos + ch * 17u * static_cast<uint32_t>(lane + 1u);
            break;
        }
        const uint32_t mixed = static_cast<uint32_t>(lerp(
            static_cast<float>(pos),
            static_cast<float>(distributed & (kPsdRawFieldTapeSize - 1u)),
            params_.channelSpread));
        return mixed & (kPsdRawFieldTapeSize - 1u);
    }

    uint8_t renderVirtualByte(uint32_t index) const
    {
        return renderVirtualByte(index, tapeSeed_);
    }

    uint8_t renderVirtualByte(uint32_t index, uint32_t seed) const
    {
        const uint32_t row = index >> 8u;
        const uint32_t x = index & 255u;
        const uint32_t plane = (row >> 2u) & 7u;
        const uint32_t section = (row >> 5u) & 7u;
        const uint32_t h = hash(index ^ seed ^ (plane * 0x9e3779b9u));
        const uint8_t noise = static_cast<uint8_t>(h >> 24u);
        const uint8_t ramp = static_cast<uint8_t>((x * (3u + plane * 2u) + row * 11u) & 255u);
        const uint8_t contour = static_cast<uint8_t>((std::sin(
            static_cast<float>(x) * 0.043f + static_cast<float>(row) * 0.017f + static_cast<float>(plane))
            * 0.5f + 0.5f) * 255.0f);
        const uint8_t mask = (hash((row >> 1u) ^ (x >> 3u) ^ seed) & 1u) ? 0xffu : 0x00u;

        if ((row & 31u) == 0u) return static_cast<uint8_t>((row * 13u + section * 29u) & 255u);
        if ((x & 63u) == 0u) return static_cast<uint8_t>(0x80u | ((row + plane * 19u) & 0x7fu));
        if (section == 0u) return static_cast<uint8_t>(0x38u + ((x + row) & 15u));
        if (section == 1u) return lerpByte(ramp, noise, 0.72f);
        if (section == 2u) return lerpByte(contour, ramp, 0.64f);
        if (section == 3u) return lerpByte(mask, noise, 0.48f);
        return lerpByte(static_cast<uint8_t>(ramp ^ contour), noise, 0.68f);
    }

    float readTapeAudio(uint32_t index) const
    {
        index &= (kPsdRawFieldTapeSize - 1u);
        const float current = byteToAudio(tape_[index]);
        if (!evolveActive_ || evolveRegionLength_ == 0u) return current;
        const uint32_t offset = (index + kPsdRawFieldTapeSize - evolveRegionStart_)
            & (kPsdRawFieldTapeSize - 1u);
        if (offset >= evolveRegionLength_) return current;
        const float blend = evolveBlend_ * evolveBlend_ * (3.0f - 2.0f * evolveBlend_);
        return lerp(current, byteToAudio(evolveTarget_[index]), blend);
    }

    void startEvolutionRegion()
    {
        const uint32_t channel = evolveRegionCounter_ % kPsdRawFieldChannels;
        const uint32_t lane = (evolveRegionCounter_ / kPsdRawFieldChannels) & 3u;
        const uint32_t center = distributedPosition(static_cast<uint32_t>(cursor_[channel]), channel, lane);
        evolveRegionLength_ = 256u + static_cast<uint32_t>(1792.0f * params_.evolve * params_.evolve);
        evolveRegionLength_ = std::min<uint32_t>(evolveRegionLength_, 2048u);
        evolveRegionStart_ = (center + kPsdRawFieldTapeSize - evolveRegionLength_ / 2u)
            & (kPsdRawFieldTapeSize - 1u);
        evolveSeed_ = hash(evolveSeed_ ^ (evolveRegionCounter_ * 0x9e3779b9u) ^ 0x68e31da4u);
        for (uint32_t i = 0; i < evolveRegionLength_; ++i) {
            const uint32_t index = (evolveRegionStart_ + i) & (kPsdRawFieldTapeSize - 1u);
            const uint8_t regenerated = renderVirtualByte(index, evolveSeed_);
            const uint8_t mutation = static_cast<uint8_t>(hash(index ^ evolveSeed_ ^ 0xd1b54a35u) >> 24u);
            evolveTarget_[index] = lerpByte(regenerated, mutation, 0.12f + params_.evolve * 0.44f);
        }
        ++evolveRegionCounter_;
        evolveBlend_ = 0.0f;
        evolveActive_ = true;
    }

    void commitEvolutionRegion()
    {
        for (uint32_t i = 0; i < evolveRegionLength_; ++i) {
            const uint32_t index = (evolveRegionStart_ + i) & (kPsdRawFieldTapeSize - 1u);
            tape_[index] = evolveTarget_[index];
        }
        evolveBlend_ = 0.0f;
        evolveActive_ = false;
        const float intervalSeconds = lerp(12.0f, 0.35f, params_.evolve * params_.evolve);
        evolveWaitSamples_ = intervalSeconds * static_cast<float>(sampleRate_);
    }

    void advanceEvolution()
    {
        if (evolveActive_) {
            const float morphSeconds = lerp(3.0f, 0.15f, params_.evolve);
            evolveBlend_ += 1.0f / std::max(1.0f, morphSeconds * static_cast<float>(sampleRate_));
            if (evolveBlend_ >= 1.0f) commitEvolutionRegion();
            return;
        }
        if (params_.evolve <= 0.0001f) return;
        evolveWaitSamples_ -= 1.0f;
        if (evolveWaitSamples_ <= 0.0f) startEvolutionRegion();
    }

    float breakpointStage(float raw, uint32_t ch, uint32_t pos)
    {
        if (params_.geometry <= 0.0001f) return raw;
        auto& s = breakpoint_[ch];
        if (s.phase >= 1.0f) {
            s.seed = hash(s.seed ^ pos ^ tapeSeed_ ^ (ch * 0x85ebca6bu));
            const uint32_t flavor = (ch + ((tapeSeed_ >> 9u) & 7u)) % 7u;
            const float ampRand = distributionSample(static_cast<Distribution>(flavor), s.seed, ch, pos);
            const float durRand = distributionSample(static_cast<Distribution>((flavor + 3u) % 7u), s.seed ^ 0xa511e9b3u, ch, pos + 197u);
            const float oracle = readTapeAudio(distributedPosition(pos + (s.seed & 4095u), ch, 3u));
            const float memory = lerp(0.90f, 0.18f, params_.chaos);
            const float tendency = lerp(0.18f, 0.70f, params_.texture);
            const float tendencyTarget = lerp(oracle, s.tendencyCenter, memory);
            s.tendencyCenter = lerp(tendencyTarget, raw, tendency * 0.40f);
            const float step = lerp(0.03f, 1.45f, std::pow(params_.geometry, 1.35f) * lerp(0.45f, 1.0f, params_.chaos));
            s.currentAmp = s.last;
            s.nextAmp = clamp(lerp(s.currentAmp, s.tendencyCenter + ampRand * step, 1.0f - memory * 0.72f), -1.0f, 1.0f);

            const float rate = std::pow(params_.scanRate, 1.2f);
            const float minSamples = lerp(96.0f, 2.0f, rate);
            const float maxSamples = lerp(14000.0f, 72.0f, rate) * lerp(1.0f, 0.42f, params_.chaos);
            const float durShape = clamp(std::abs(durRand), 0.0f, 1.0f);
            const float octaveJump = std::pow(2.0f, params_.chaos * ampRand * 5.0f);
            const float samples = lerp(minSamples, maxSamples, durShape) / std::max(0.0625f, octaveJump);
            s.inc = 1.0f / clamp(samples, 2.0f, 24000.0f);
            s.phase = 0.0f;
            const float restProbability = params_.geometry * std::pow(params_.chaos, 3.0f) * 0.18f;
            s.resting = hash01(s.seed ^ 0x632be59bu) < restProbability;
        }

        s.phase = std::min(1.0f, s.phase + s.inc);
        const float t = s.phase;
        const float smoothT = t * t * (3.0f - 2.0f * t);
        const float badT = std::sin((t + hash01(s.seed ^ 0x91e10da5u) * params_.chaos)
            * kPi * (1.0f + 7.0f * params_.chaos));
        const float curve = lerp(smoothT, badT, params_.chaos * params_.chaos * 0.72f);
        float value = lerp(s.currentAmp, s.nextAmp, curve);
        if (s.resting) value *= lerp(0.05f, 0.30f, 1.0f - params_.chaos);
        const float edge = (s.nextAmp - s.currentAmp) * std::sin(t * kPi) * params_.chaos * 0.75f;
        s.last = softClip(value + edge);
        return lerp(raw, s.last, params_.geometry * 0.92f);
    }

    float connectionStage(float raw, uint32_t ch, uint32_t pos)
    {
        const float amount = clamp((params_.geometry - 0.42f) / 0.58f, 0.0f, 1.0f);
        if (amount <= 0.0001f) return raw;
        const float downVector = std::sqrt(params_.codecRate);
        const float disaster = clamp(params_.chaos * 0.78f + downVector * 0.34f, 0.0f, 1.0f);
        if (connectPhase_[ch] >= 1.0f) {
            connectSeed_[ch] = hash(connectSeed_[ch] ^ pos ^ (ch * 0x45d9f3bu));
            const float r0 = hash01(connectSeed_[ch]);
            const float r1 = hash01(connectSeed_[ch] ^ 0xa511e9b3u);
            const float r2 = hash01(connectSeed_[ch] ^ 0x63d83595u);
            const float rate = clamp(params_.scanRate * 0.68f + params_.chaos * 0.32f, 0.0f, 1.0f);
            const float minPeriod = lerp(96.0f, 3.0f, rate * rate) * lerp(1.0f, 3.0f, downVector);
            const float maxPeriod = lerp(10000.0f, 72.0f, rate * rate) * lerp(1.0f, 1.7f, downVector);
            const float chaosPeriod = lerp((minPeriod + maxPeriod) * 0.5f, lerp(minPeriod, maxPeriod, r0), params_.chaos);
            const float octaveJump = std::pow(2.0f, std::floor(lerp(-3.0f, 4.0f, r1) * params_.chaos));
            const float samples = clamp(chaosPeriod / std::max(0.125f, octaveJump), 2.0f, 16000.0f);
            const uint32_t jump = static_cast<uint32_t>(lerp(1.0f, 4096.0f, params_.chaos * r2));
            connectStart_[ch] = connectCurrent_[ch];
            connectTarget_[ch] = lerp(raw, readTapeAudio(pos + jump + ch * 379u), amount);
            connectPhase_[ch] = 0.0f;
            connectInc_[ch] = 1.0f / samples;
        }

        connectPhase_[ch] = std::min(1.0f, connectPhase_[ch] + connectInc_[ch]);
        const float t = connectPhase_[ch];
        const float smoothT = t * t * (3.0f - 2.0f * t);
        const float badT = std::sin((t + hash01(connectSeed_[ch] ^ 0x91e10da5u) * params_.chaos)
            * kPi * (1.0f + 9.0f * disaster));
        const float steps = lerp(10.0f, 2.0f, downVector);
        const float vectorT = std::floor(t * steps) / std::max(1.0f, steps - 1.0f);
        float curve = lerp(smoothT, badT, disaster);
        curve = lerp(curve, vectorT, downVector * amount);
        const float connected = lerp(connectStart_[ch], connectTarget_[ch], curve);
        connectCurrent_[ch] = softClip(connected + (connected - raw) * disaster * 0.8f);
        return lerp(raw, connectCurrent_[ch], amount);
    }

    float distributionSample(Distribution distribution, uint32_t seed, uint32_t ch, uint32_t pos) const
    {
        const float u0 = clamp(hash01(seed ^ pos ^ (ch * 503u)), 0.0001f, 0.9999f);
        const float u1 = clamp(hash01(seed ^ 0x9e3779b9u ^ (pos * 17u)), 0.0001f, 0.9999f);
        switch (distribution) {
        case Distribution::Gaussian:
            return clamp(std::sqrt(-2.0f * std::log(u0)) * std::cos(2.0f * kPi * u1) * 0.34f, -1.0f, 1.0f);
        case Distribution::Cauchy:
            return clamp(std::tan(kPi * (u0 - 0.5f)) * 0.18f, -1.0f, 1.0f);
        case Distribution::Logistic:
            return clamp(std::log(u0 / (1.0f - u0)) * 0.18f, -1.0f, 1.0f);
        case Distribution::Arcsine:
            return clamp(std::sin(kPi * (u0 - 0.5f)), -1.0f, 1.0f);
        case Distribution::Exponential:
            return clamp((1.0f - std::exp(-5.0f * u0)) * 2.0f - 1.0f, -1.0f, 1.0f);
        case Distribution::Binary:
            return u0 < 0.5f ? -1.0f : 1.0f;
        case Distribution::Uniform:
        default:
            return u0 * 2.0f - 1.0f;
        }
    }

    static float quantize(float x, float bits)
    {
        const float levels = std::max(3.0f, std::pow(2.0f, std::floor(bits)) - 1.0f);
        return std::round(clamp(x, -1.0f, 1.0f) * levels) / levels;
    }

    static float signum(float x) { return x < 0.0f ? -1.0f : 1.0f; }

    static float encodeMuLaw(float x)
    {
        constexpr float mu = 255.0f;
        const float ax = std::abs(clamp(x, -1.0f, 1.0f));
        return signum(x) * std::log1p(mu * ax) / std::log1p(mu);
    }

    static float decodeMuLaw(float x)
    {
        constexpr float mu = 255.0f;
        const float ax = std::abs(clamp(x, -1.0f, 1.0f));
        return signum(x) * (std::pow(1.0f + mu, ax) - 1.0f) / mu;
    }

    static float encodeALaw(float x)
    {
        constexpr float a = 87.6f;
        const float ax = std::abs(clamp(x, -1.0f, 1.0f));
        const float y = ax < (1.0f / a)
            ? (a * ax) / (1.0f + std::log(a))
            : (1.0f + std::log(a * ax)) / (1.0f + std::log(a));
        return signum(x) * y;
    }

    static float decodeALaw(float x)
    {
        constexpr float a = 87.6f;
        const float ax = std::abs(clamp(x, -1.0f, 1.0f));
        const float threshold = 1.0f / (1.0f + std::log(a));
        const float y = ax < threshold
            ? ax * (1.0f + std::log(a)) / a
            : std::exp(ax * (1.0f + std::log(a)) - 1.0f) / a;
        return signum(x) * y;
    }

    float compandRoundTrip(float x, bool aLaw) const
    {
        if (aLaw) return decodeALaw(quantize(encodeALaw(x), params_.bitDepth));
        return decodeMuLaw(quantize(encodeMuLaw(x), params_.bitDepth));
    }

    float downsampleStage(float x, uint32_t ch)
    {
        const float holdSamples = std::pow(2.0f, params_.codecRate * 14.0f);
        hold_[ch] += 1.0f;
        if (hold_[ch] >= holdSamples) {
            hold_[ch] -= holdSamples;
            held_[ch] = x;
        }
        return held_[ch];
    }

    void updateCodecFrame(uint32_t ch, uint32_t pos)
    {
        const uint32_t h = hash(pos ^ (ch * 0x45d9f3bu) ^ tapeSeed_);
        const float loss = params_.codecDamage * params_.codecDamage * 0.35f;
        frameDrop_[ch] = static_cast<float>(h & 0xffffu) / 65535.0f < loss;
        codeGain_[ch] = lerp(0.15f, 0.9f, static_cast<float>((h >> 16u) & 0xffu) / 255.0f);
        codePitch_[ch] = 24u + ((h >> 24u) & 95u);
    }

    float codecStage(float input, uint32_t ch)
    {
        float x = downsampleStage(clamp(input, -1.0f, 1.0f), ch);
        if (frameDrop_[ch]) x = predictor_[ch] * lerp(0.68f, 0.985f, params_.codecDamage);

        switch (params_.codecMode) {
        case PsdRawFieldCodecMode::DeltaPcm: {
            const float delta = quantize(x - predictor_[ch], params_.bitDepth);
            x = predictor_[ch] + delta * lerp(0.95f, 1.55f, params_.codecDamage);
            break;
        }
        case PsdRawFieldCodecMode::Adpcm: {
            const float error = x - predictor_[ch];
            const float q = clamp(std::round(error / std::max(step_[ch], 0.0001f)), -7.0f, 7.0f);
            x = predictor_[ch] + q * step_[ch];
            step_[ch] += (std::abs(error) * lerp(0.24f, 0.46f, params_.codecDamage) + 0.006f - step_[ch]) * 0.012f;
            break;
        }
        case PsdRawFieldCodecMode::MuLaw:
            x = compandRoundTrip(x, false);
            break;
        case PsdRawFieldCodecMode::ALaw:
            x = compandRoundTrip(x, true);
            break;
        case PsdRawFieldCodecMode::CelpScramble:
            x = celpScramble(x, ch);
            break;
        case PsdRawFieldCodecMode::RawPcm:
        default:
            x = quantize(x, params_.bitDepth);
            break;
        }

        predictor_[ch] += (x - predictor_[ch]) * lerp(0.18f, 0.82f, params_.codecDamage);
        return x;
    }

    float celpScramble(float x, uint32_t ch)
    {
        const uint32_t delay = std::min<uint32_t>(codePitch_[ch], static_cast<uint32_t>(pitch_[ch].size() - 1u));
        const uint32_t read = (pitchWrite_[ch] + static_cast<uint32_t>(pitch_[ch].size()) - delay)
            % static_cast<uint32_t>(pitch_[ch].size());
        const float adaptive = pitch_[ch][read] * lerp(0.15f, 0.86f, params_.codecDamage);
        const uint32_t codeIndex = static_cast<uint32_t>((std::abs(x) * 32767.0f) + static_cast<float>(ch * 13u))
            & (kPsdRawFieldCodebookSize - 1u);
        const float code = readTapeAudio(codeIndex * 1543u + ch * 257u + tapeSeed_);
        const float excitation = lerp(x, adaptive + code * codeGain_[ch], lerp(0.20f, 1.0f, params_.codecDamage));
        const float coded = compandRoundTrip(excitation, (ch & 1u) != 0u);
        pitch_[ch][pitchWrite_[ch]] = coded;
        pitchWrite_[ch] = (pitchWrite_[ch] + 1u) % static_cast<uint32_t>(pitch_[ch].size());
        return coded;
    }

    static uint32_t hash(uint32_t x)
    {
        x ^= x >> 16u;
        x *= 0x7feb352du;
        x ^= x >> 15u;
        x *= 0x846ca68bu;
        x ^= x >> 16u;
        return x;
    }

    static float hash01(uint32_t x) { return static_cast<float>(hash(x) & 0xffffu) / 65535.0f; }

    static uint8_t lerpByte(uint8_t a, uint8_t b, float t)
    {
        return static_cast<uint8_t>(clamp(lerp(static_cast<float>(a), static_cast<float>(b), t), 0.0f, 255.0f));
    }

    static float byteToAudio(uint8_t b) { return (static_cast<float>(b) - 127.5f) * (1.0f / 127.5f); }
    static float softClip(float x) { return x / (1.0f + std::abs(x)); }
    static float triangle(float phase) { return 1.0f - std::abs(phase * 2.0f - 1.0f); }

    static float pulse(float phase, float width)
    {
        return phase < width ? 1.0f - phase / std::max(width, 0.0001f) : 0.0f;
    }

    float rowPhase(uint32_t ch) const
    {
        const float row = cursor_[ch] * (1.0f / 256.0f);
        return row - std::floor(row);
    }

    static float metadataTick(uint32_t pos, uint32_t ch)
    {
        const uint32_t period = 97u + ch * 23u;
        if (((pos + ch * 11u) % period) > 2u) return 0.0f;
        return ((pos / period) & 1u) ? 0.42f : -0.42f;
    }

    PsdRawFieldParams params_ {};
    double sampleRate_ = 48000.0;
    bool ready_ = false;
    uint32_t tapeSeed_ = 0x50434431u;
    float sectionPhase_ = 0.0f;
    uint32_t frameSample_ = 0u;
    std::array<uint8_t, kPsdRawFieldTapeSize> tape_ {};
    std::array<uint8_t, kPsdRawFieldTapeSize> evolveTarget_ {};
    uint32_t evolveSeed_ = 1u;
    uint32_t evolveRegionStart_ = 0u;
    uint32_t evolveRegionLength_ = 0u;
    uint32_t evolveRegionCounter_ = 0u;
    float evolveBlend_ = 0.0f;
    float evolveWaitSamples_ = 0.0f;
    bool evolveActive_ = false;
    std::array<float, kPsdRawFieldChannels> cursor_ {};
    std::array<float, kPsdRawFieldChannels> prev_ {};
    std::array<float, kPsdRawFieldChannels> textureState_ {};
    std::array<float, kPsdRawFieldChannels> dcX_ {};
    std::array<float, kPsdRawFieldChannels> dcY_ {};
    std::array<float, kPsdRawFieldChannels> connectStart_ {};
    std::array<float, kPsdRawFieldChannels> connectTarget_ {};
    std::array<float, kPsdRawFieldChannels> connectCurrent_ {};
    std::array<float, kPsdRawFieldChannels> connectPhase_ {};
    std::array<float, kPsdRawFieldChannels> connectInc_ {};
    std::array<uint32_t, kPsdRawFieldChannels> connectSeed_ {};
    std::array<BreakpointState, kPsdRawFieldChannels> breakpoint_ {};
    std::array<float, kPsdRawFieldChannels> hold_ {};
    std::array<float, kPsdRawFieldChannels> held_ {};
    std::array<float, kPsdRawFieldChannels> predictor_ {};
    std::array<float, kPsdRawFieldChannels> step_ {};
    std::array<bool, kPsdRawFieldChannels> frameDrop_ {};
    std::array<float, kPsdRawFieldChannels> codeGain_ {};
    std::array<uint32_t, kPsdRawFieldChannels> codePitch_ {};
    std::array<std::array<float, 160>, kPsdRawFieldChannels> pitch_ {};
    std::array<uint32_t, kPsdRawFieldChannels> pitchWrite_ {};
    float loudnessEnergy_ = 0.04f;
    float loudnessGain_ = 1.0f;
    MacroShred shaper_ {};
};

class PsdRawFieldMorph {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        for (auto& engine : engines_) engine.prepare(sampleRate_);
        ready_ = true;
        reset();
    }

    void reset()
    {
        for (auto& engine : engines_) {
            engine.setParams(params_);
            engine.reset();
        }
        activeEngine_ = 0u;
        targetEngine_ = 1u;
        transitionPosition_ = 0u;
        transitionSamples_ = 1u;
        transitioning_ = false;
    }

    void setParams(const PsdRawFieldParams& params)
    {
        params_ = params;
        if (transitioning_) engines_[targetEngine_].setParams(params_);
        else engines_[activeEngine_].setParams(params_);
    }

    void transitionTo(const PsdRawFieldParams& params, float seconds = 0.75f)
    {
        params_ = params;
        if (!ready_) {
            for (auto& engine : engines_) engine.setParams(params_);
            return;
        }
        if (transitioning_ && transitionProgress() >= 0.5f) activeEngine_ = targetEngine_;
        transitioning_ = false;
        targetEngine_ = 1u - activeEngine_;
        engines_[targetEngine_].setParams(params_);
        engines_[targetEngine_].reset();
        transitionPosition_ = 0u;
        transitionSamples_ = std::max<uint64_t>(64u,
            static_cast<uint64_t>(sampleRate_ * clamp(seconds, 0.02f, 4.0f)));
        transitioning_ = true;
    }

    PsdRawFieldParams params() const { return params_; }
    bool ready() const { return ready_; }
    bool transitioning() const { return transitioning_; }
    float transitionProgress() const
    {
        if (!transitioning_) return 1.0f;
        return clamp(static_cast<float>(transitionPosition_) / static_cast<float>(transitionSamples_), 0.0f, 1.0f);
    }

    void process(float* const* output, uint32_t outputChannels, uint32_t frames)
    {
        if (!ready_ || !output || frames == 0u) return;
        if (!transitioning_) {
            engines_[activeEngine_].process(output, outputChannels, frames);
            return;
        }

        uint32_t processed = 0u;
        while (processed < frames) {
            const uint32_t chunk = std::min<uint32_t>(kMorphChunk, frames - processed);
            for (uint32_t ch = 0; ch < kPsdRawFieldChannels; ++ch) {
                sourcePtrs_[ch] = sourceScratch_[ch].data();
                targetPtrs_[ch] = targetScratch_[ch].data();
            }
            engines_[activeEngine_].process(sourcePtrs_.data(), kPsdRawFieldChannels, chunk);
            engines_[targetEngine_].process(targetPtrs_.data(), kPsdRawFieldChannels, chunk);

            const uint32_t channels = std::min<uint32_t>(outputChannels, kPsdRawFieldChannels);
            for (uint32_t i = 0; i < chunk; ++i) {
                const float phase = clamp(
                    static_cast<float>(transitionPosition_ + i) / static_cast<float>(transitionSamples_),
                    0.0f,
                    1.0f);
                const float mix = phase * phase * (3.0f - 2.0f * phase);
                for (uint32_t ch = 0; ch < channels; ++ch) {
                    if (output[ch]) {
                        output[ch][processed + i] = lerp(sourceScratch_[ch][i], targetScratch_[ch][i], mix);
                    }
                }
                for (uint32_t ch = channels; ch < outputChannels; ++ch) {
                    if (output[ch]) output[ch][processed + i] = 0.0f;
                }
            }
            transitionPosition_ += chunk;
            processed += chunk;
            if (transitionPosition_ >= transitionSamples_) {
                activeEngine_ = targetEngine_;
                transitioning_ = false;
                if (processed < frames) {
                    std::array<float*, kPsdRawFieldChannels> remainderPtrs {};
                    const uint32_t renderChannels = std::min<uint32_t>(outputChannels, kPsdRawFieldChannels);
                    for (uint32_t ch = 0; ch < renderChannels; ++ch) {
                        remainderPtrs[ch] = output[ch] ? output[ch] + processed : nullptr;
                    }
                    engines_[activeEngine_].process(remainderPtrs.data(), renderChannels, frames - processed);
                    for (uint32_t ch = renderChannels; ch < outputChannels; ++ch) {
                        if (output[ch]) std::fill(output[ch] + processed, output[ch] + frames, 0.0f);
                    }
                }
                return;
            }
        }
    }

private:
    static constexpr uint32_t kMorphChunk = 256u;
    PsdRawFieldParams params_ {};
    std::array<PsdRawField, 2> engines_ {};
    uint32_t activeEngine_ = 0u;
    uint32_t targetEngine_ = 1u;
    uint64_t transitionPosition_ = 0u;
    uint64_t transitionSamples_ = 1u;
    double sampleRate_ = 48000.0;
    bool ready_ = false;
    bool transitioning_ = false;
    std::array<std::array<float, kMorphChunk>, kPsdRawFieldChannels> sourceScratch_ {};
    std::array<std::array<float, kMorphChunk>, kPsdRawFieldChannels> targetScratch_ {};
    std::array<float*, kPsdRawFieldChannels> sourcePtrs_ {};
    std::array<float*, kPsdRawFieldChannels> targetPtrs_ {};
};

} // namespace s3g
