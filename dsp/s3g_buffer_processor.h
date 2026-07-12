#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kBufferProcessorChannels = 8;

struct BufferProcessorParams {
    float sizeMs = 160.0f;
    float rate = 1.0f;
    float crossfade = 0.12f;
    float repeat = 0.65f;
    float skip = 0.0f;
    float skipGrid = 0.0f;
    float skipJam = 0.0f;
    float skipChase = 0.0f;
    float skipSync = 0.0f;
    float reverse = 0.0f;
    float crush = 0.0f;
    float error = 0.0f;
    float errorMode = 0.0f;
    float memory = 0.0f;
    float spread = 0.0f;
    float deviation = 0.0f;
    float skew = 0.0f;
    float center = 0.5f;
    float glideMs = 140.0f;
    float mix = 0.45f;
    float outputGainDb = -1.5f;
};

class BufferProcessor {
public:
    void prepare(double sampleRate, uint32_t channels, double maxBufferSeconds = 4.0)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        channels_ = std::clamp<uint32_t>(channels, 0u, kBufferProcessorChannels);
        bufferSize_ = std::max<uint32_t>(1024u, static_cast<uint32_t>(std::ceil(sampleRate_ * maxBufferSeconds)));
        writePos_ = 0u;
        cycleCounter_ = 0u;
        buffer_.assign(channels_, std::vector<float>(bufferSize_, 0.0f));
        memory_.assign(channels_, std::vector<float>(bufferSize_, 0.0f));
        memoryCaptured_ = false;
        lanes_.assign(channels_, LaneState {});
        reset();
    }

    void reset()
    {
        for (auto& channel : buffer_) {
            std::fill(channel.begin(), channel.end(), 0.0f);
        }
        for (uint32_t ch = 0; ch < lanes_.size(); ++ch) {
            lanes_[ch] = LaneState {};
            lanes_[ch].direction = 1.0f;
            lanes_[ch].heldSample = 0.0f;
            lanes_[ch].holdCounter = 0u;
            lanes_[ch].targetSamples = std::max(8.0f, params_.sizeMs * static_cast<float>(sampleRate_ * 0.001));
            lanes_[ch].smoothedSamples = lanes_[ch].targetSamples;
            lanes_[ch].activeSamples = lanes_[ch].targetSamples;
            lanes_[ch].skipSamples = lanes_[ch].targetSamples;
            lanes_[ch].errorSample = 0.0f;
            lanes_[ch].errorCounter = 0u;
            lanes_[ch].skipCycles = 0u;
        }
        writePos_ = 0u;
        cycleCounter_ = 0u;
        smoothedSpread_ = params_.spread;
        smoothedDeviation_ = params_.deviation;
        smoothedSkew_ = params_.skew;
        smoothedCenter_ = params_.center;
        mixSmoothed_ = params_.mix;
        gainSmoothed_ = dbToGain(params_.outputGainDb);
    }

    void captureMemory()
    {
        if (memory_.size() != buffer_.size()) {
            memory_.assign(channels_, std::vector<float>(bufferSize_, 0.0f));
        }
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            if (ch < buffer_.size() && ch < memory_.size()) {
                memory_[ch] = buffer_[ch];
            }
        }
        memoryCaptured_ = true;
    }

    void clearMemory()
    {
        for (auto& channel : memory_) {
            std::fill(channel.begin(), channel.end(), 0.0f);
        }
        memoryCaptured_ = false;
    }

    void setTransport(double bpm, bool hasTempo)
    {
        transportBpm_ = std::clamp(bpm, 20.0, 300.0);
        transportTempoAvailable_ = hasTempo;
    }

    void setParams(const BufferProcessorParams& params)
    {
        params_ = sanitize(params);
    }

    BufferProcessorParams params() const { return params_; }
    uint32_t channels() const { return channels_; }

    void processFrame(const float* input, float* output)
    {
        if (!input || !output || channels_ == 0u || bufferSize_ == 0u) {
            return;
        }

        updateSmoothing();
        std::array<float, kBufferProcessorChannels> wet {};
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            buffer_[ch][writePos_] = flushDenormal(input[ch]);
        }

        for (uint32_t ch = 0; ch < channels_; ++ch) {
            wet[ch] = processLane(ch);
        }

        const float mixTarget = params_.mix;
        const float gainTarget = dbToGain(params_.outputGainDb);
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            mixSmoothed_ += (mixTarget - mixSmoothed_) * 0.0015f;
            gainSmoothed_ += (gainTarget - gainSmoothed_) * 0.0015f;
            const float value = input[ch] + (wet[ch] - input[ch]) * mixSmoothed_;
            output[ch] = softLimit(flushDenormal(value * gainSmoothed_));
        }

        ++writePos_;
        if (writePos_ >= bufferSize_) {
            writePos_ = 0u;
        }
    }

private:
    struct LaneState {
        float targetSamples = 7680.0f;
        float smoothedSamples = 7680.0f;
        float segmentStart = 0.0f;
        float playPos = 0.0f;
        float activeSamples = 7680.0f;
        float skipSamples = 7680.0f;
        float direction = 1.0f;
        float heldSample = 0.0f;
        float errorSample = 0.0f;
        float errorPcmCurrent = 0.0f;
        float errorPcmTarget = 0.0f;
        float signalEnv = 0.0f;
        float errorPath = 0.0f;
        float errorPathTarget = 0.0f;
        float lastOutput = 0.0f;
        float deClickStart = 0.0f;
        uint32_t holdCounter = 0u;
        uint32_t errorCounter = 0u;
        uint32_t errorFrame = 0u;
        uint32_t errorFrameLength = 1u;
        uint32_t errorPathCounter = 0u;
        uint32_t errorPathLength = 1u;
        uint32_t skipCycles = 0u;
        uint32_t deClickRemaining = 0u;
        uint32_t deClickTotal = 0u;
    };

    static BufferProcessorParams sanitize(BufferProcessorParams params)
    {
        params.sizeMs = clamp(params.sizeMs, 8.0f, 1200.0f);
        params.rate = clamp(params.rate, -2.0f, 2.0f);
        if (std::abs(params.rate) < 0.02f) params.rate = params.rate < 0.0f ? -0.02f : 0.02f;
        params.crossfade = clamp(params.crossfade, 0.0f, 0.45f);
        params.repeat = clamp(params.repeat, 0.0f, 1.0f);
        params.skip = clamp(params.skip, 0.0f, 1.0f);
        params.skipGrid = clamp(std::floor(params.skipGrid + 0.5f), 0.0f, 4.0f);
        params.skipJam = clamp(params.skipJam, 0.0f, 1.0f);
        params.skipChase = clamp(params.skipChase, 0.0f, 1.0f);
        params.skipSync = clamp(std::floor(params.skipSync + 0.5f), 0.0f, 1.0f);
        params.reverse = clamp(params.reverse, 0.0f, 1.0f);
        params.crush = clamp(params.crush, 0.0f, 1.0f);
        params.error = clamp(params.error, 0.0f, 1.0f);
        params.errorMode = clamp(std::floor(params.errorMode + 0.5f), 0.0f, 4.0f);
        params.memory = clamp(params.memory, 0.0f, 1.0f);
        params.spread = clamp(params.spread, 0.0f, 1.0f);
        params.deviation = clamp(params.deviation, 0.0f, 1.0f);
        params.skew = clamp(params.skew, -1.0f, 1.0f);
        params.center = clamp(params.center, 0.0f, 1.0f);
        params.glideMs = clamp(params.glideMs, 10.0f, 2000.0f);
        params.mix = clamp(params.mix, 0.0f, 1.0f);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
        return params;
    }

    static float hash01(uint32_t x)
    {
        x ^= x >> 16u;
        x *= 0x7feb352du;
        x ^= x >> 15u;
        x *= 0x846ca68bu;
        x ^= x >> 16u;
        return static_cast<float>(x & 0xffffu) / 65535.0f;
    }

    static float hashSigned(uint32_t x)
    {
        return hash01(x) * 2.0f - 1.0f;
    }

    static float softLimit(float value)
    {
        return std::tanh(clamp(value, -8.0f, 8.0f));
    }

    static float pseudoByteAudio(uint32_t counter, uint32_t lane)
    {
        uint32_t x = counter * (37u + lane * 11u) + lane * 919u + 0x6d2b79f5u;
        x ^= x >> 7u;
        x *= 0x45d9f3bu;
        x ^= x >> 11u;
        const float byte = static_cast<float>(x & 0xffu) / 127.5f - 1.0f;
        const float nibble = static_cast<float>((x >> 8u) & 0x0fu) / 7.5f - 1.0f;
        return clamp(byte * 0.36f + nibble * 0.18f, -0.62f, 0.62f);
    }

    static float inventedPcmTarget(uint32_t counter, uint32_t lane, float previous, float source, float err)
    {
        const float raw = pseudoByteAudio(counter, lane);
        const float drift = hashSigned(counter * 17u + lane * 503u + 71u) * lerp(0.018f, 0.16f, err);
        const float sourceHint = clamp(source, -0.9f, 0.9f) * lerp(0.70f, 0.22f, err);
        const float imagined = raw * lerp(0.18f, 0.58f, err) + sourceHint + drift;
        const float maxDelta = lerp(0.035f, 0.34f, err);
        const float bounded = previous + clamp(imagined - previous, -maxDelta, maxDelta);
        const float pcmLevels = std::floor(lerp(4096.0f, 96.0f, err));
        return clamp(std::round(bounded * pcmLevels) / std::max(1.0f, pcmLevels), -0.82f, 0.82f);
    }

    static float errorModeIntensity(uint32_t mode)
    {
        switch (mode) {
        case 1u: return 0.78f;
        case 2u: return 0.86f;
        case 3u: return 0.72f;
        case 4u: return 1.0f;
        default: return 0.62f;
        }
    }

    static uint32_t modeIndex(float mode, uint32_t maxValue)
    {
        return static_cast<uint32_t>(std::clamp(std::floor(mode + 0.5f), 0.0f, static_cast<float>(maxValue)));
    }

    static float smoothStep(float t)
    {
        t = clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    float signalGate(LaneState& lane, float source)
    {
        const float a = std::abs(source);
        const float coeff = a > lane.signalEnv ? 0.008f : 0.0009f;
        lane.signalEnv += (a - lane.signalEnv) * coeff;
        return smoothStep((lane.signalEnv - 0.0015f) / 0.035f);
    }

    float errorPathGate(LaneState& lane, uint32_t ch, uint32_t mode, float err, float sourceGate)
    {
        if (sourceGate <= 0.0001f) {
            lane.errorPathTarget = 0.0f;
            lane.errorPath += (lane.errorPathTarget - lane.errorPath) * 0.018f;
            return 0.0f;
        }

        if (lane.errorPathCounter == 0u || lane.errorPathCounter >= lane.errorPathLength) {
            const float minMs = lerp(220.0f, 28.0f, err);
            const float maxMs = lerp(820.0f, 150.0f, err);
            const uint32_t salt = cycleCounter_ * 313u + writePos_ * 7u + ch * 809u + mode * 127u;
            const float ms = minMs + hash01(salt + 41u) * (maxMs - minMs);
            lane.errorPathLength = std::max<uint32_t>(8u, static_cast<uint32_t>(sampleRate_ * ms * 0.001));

            const float openChance = std::min(0.92f, 0.18f + err * errorModeIntensity(mode) * 0.74f + params_.skip * 0.12f);
            if (hash01(salt + 89u) < openChance) {
                lane.errorPathTarget = lerp(0.18f, 1.0f, err) * lerp(0.55f, 1.0f, hash01(salt + 113u));
            } else {
                lane.errorPathTarget = 0.0f;
            }
            lane.errorPathCounter = 0u;
        }

        ++lane.errorPathCounter;
        const float slew = lerp(0.0045f, 0.026f, err);
        lane.errorPath += (lane.errorPathTarget - lane.errorPath) * slew;
        return sourceGate * lane.errorPath;
    }

    float readWrapped(uint32_t ch, float index) const
    {
        if (ch >= buffer_.size() || bufferSize_ == 0u) return 0.0f;
        while (index < 0.0f) index += static_cast<float>(bufferSize_);
        while (index >= static_cast<float>(bufferSize_)) index -= static_cast<float>(bufferSize_);
        const uint32_t i0 = static_cast<uint32_t>(index);
        const uint32_t i1 = (i0 + 1u) % bufferSize_;
        const float frac = index - static_cast<float>(i0);
        return lerp(buffer_[ch][i0], buffer_[ch][i1], frac);
    }

    float readMemoryWrapped(uint32_t ch, float index) const
    {
        if (!memoryCaptured_ || ch >= memory_.size() || bufferSize_ == 0u) return readWrapped(ch, index);
        while (index < 0.0f) index += static_cast<float>(bufferSize_);
        while (index >= static_cast<float>(bufferSize_)) index -= static_cast<float>(bufferSize_);
        const uint32_t i0 = static_cast<uint32_t>(index);
        const uint32_t i1 = (i0 + 1u) % bufferSize_;
        const float frac = index - static_cast<float>(i0);
        return lerp(memory_[ch][i0], memory_[ch][i1], frac);
    }

    float memoryGhost(uint32_t ch, float index, float live, float len) const
    {
        if (!memoryCaptured_ || params_.memory <= 0.0001f) return live;
        const float depth = params_.memory * params_.memory;
        const float drift = hashSigned(cycleCounter_ * 31u + ch * 991u + 5u) * depth * len * 0.018f;
        const float old = readMemoryWrapped(ch, index + drift);
        const float delta = clamp(old - live, -0.42f, 0.42f);
        const float ghost = live + delta * lerp(0.06f, 0.24f, depth);
        const float aged = std::round(ghost * lerp(512.0f, 64.0f, depth)) / lerp(512.0f, 64.0f, depth);
        return lerp(ghost, aged, depth * 0.35f);
    }

    float wrapIndex(float index) const
    {
        if (bufferSize_ == 0u) return 0.0f;
        while (index < 0.0f) index += static_cast<float>(bufferSize_);
        while (index >= static_cast<float>(bufferSize_)) index -= static_cast<float>(bufferSize_);
        return index;
    }

    bool crossesZero(uint32_t ch, float index) const
    {
        const float a = readWrapped(ch, index - 1.0f);
        const float b = readWrapped(ch, index);
        return (a <= 0.0f && b >= 0.0f) || (a >= 0.0f && b <= 0.0f);
    }

    float boundaryScore(uint32_t ch, float start, float len) const
    {
        const float a0 = readWrapped(ch, start);
        const float a1 = readWrapped(ch, start + 1.0f);
        const float z0 = readWrapped(ch, start - 1.0f);
        const float e0 = readWrapped(ch, start + len - 1.0f);
        const float e1 = readWrapped(ch, start + len);
        const float energy = std::abs(a0) + 0.50f * std::abs(a1) + 0.70f * std::abs(e0) + 0.35f * std::abs(e1);
        const float slope = std::abs(a0 - z0) + 0.55f * std::abs(e1 - e0);
        const float loopGap = std::abs(e0 - a0) * 1.65f;
        const float zeroBonus = (crossesZero(ch, start) ? -0.05f : 0.0f) + (crossesZero(ch, start + len) ? -0.04f : 0.0f);
        return energy + slope + loopGap + zeroBonus;
    }

    float findBoundarySafeStart(uint32_t ch, float desiredStart, float len, uint32_t salt) const
    {
        if (bufferSize_ < 128u) return wrapIndex(desiredStart);
        const float safeLen = std::clamp(len, 8.0f, static_cast<float>(std::max<uint32_t>(8u, bufferSize_ / 2u)));
        const float search = std::min(safeLen * 0.18f, static_cast<float>(std::clamp<uint32_t>(
            static_cast<uint32_t>(sampleRate_ * 0.0035), 48u, 256u)));
        const int radius = std::max(8, static_cast<int>(search));
        const int stride = 2;
        float best = wrapIndex(desiredStart);
        float bestScore = boundaryScore(ch, best, safeLen) - hash01(salt + 901u) * 0.0001f;
        for (int offset = -radius; offset <= radius; offset += stride) {
            const float candidate = wrapIndex(desiredStart + static_cast<float>(offset));
            const float score = boundaryScore(ch, candidate, safeLen) + std::abs(static_cast<float>(offset)) * 0.00012f;
            if (score < bestScore) {
                bestScore = score;
                best = candidate;
            }
        }
        return best;
    }

    void armDeClick(LaneState& lane)
    {
        const uint32_t base = static_cast<uint32_t>(std::clamp(sampleRate_ * 0.0015, 32.0, 144.0));
        const uint32_t extra = static_cast<uint32_t>(params_.skip * 96.0f + params_.error * 48.0f);
        lane.deClickStart = lane.lastOutput;
        lane.deClickTotal = std::max<uint32_t>(1u, base + extra);
        lane.deClickRemaining = lane.deClickTotal;
    }

    float quantizedSkipLength(float len, uint32_t salt) const
    {
        const uint32_t grid = modeIndex(params_.skipGrid, 4u);
        if (grid == 0u) {
            const float minSkip = std::max(20.0f, static_cast<float>(sampleRate_) * 0.004f);
            const float maxSkip = std::max(minSkip + 1.0f, len * lerp(0.10f, 0.48f, 1.0f - params_.skip * 0.35f));
            return std::clamp(minSkip + hash01(salt + 251u) * (maxSkip - minSkip), minSkip, len);
        }

        static constexpr float freeMs[] { 125.0f, 83.333f, 62.5f, 31.25f };
        static constexpr float beatDivs[] { 8.0f, 12.0f, 16.0f, 32.0f };
        const uint32_t idx = std::min<uint32_t>(grid - 1u, 3u);
        float samples = freeMs[idx] * static_cast<float>(sampleRate_ * 0.001);
        if (params_.skipSync > 0.5f && transportTempoAvailable_) {
            const float beatSamples = static_cast<float>(sampleRate_ * 60.0 / transportBpm_);
            samples = beatSamples * (4.0f / beatDivs[idx]);
        }
        const float jitter = 1.0f + hashSigned(salt + 457u) * params_.deviation * 0.18f;
        return std::clamp(samples * jitter, 20.0f, len);
    }

    uint32_t skipLockCycles(uint32_t ch, uint32_t salt) const
    {
        const float laneChase = channels_ > 1u ? static_cast<float>(ch) / static_cast<float>(channels_ - 1u) : 0.0f;
        const float chaseBias = params_.skipChase * (0.5f + laneChase * 2.25f);
        const float jamBias = params_.skipJam * (2.0f + hash01(salt + 601u) * 10.0f);
        return static_cast<uint32_t>(1u + std::floor(params_.skip * params_.skip * (3.0f + params_.repeat * 8.0f) + jamBias + chaseBias));
    }

    void updateSmoothing()
    {
        const float coeff = 1.0f - std::exp(-1.0f / static_cast<float>(std::max(1.0, sampleRate_ * params_.glideMs * 0.001)));
        smoothedSpread_ += (params_.spread - smoothedSpread_) * coeff;
        smoothedDeviation_ += (params_.deviation - smoothedDeviation_) * coeff;
        smoothedSkew_ += (params_.skew - smoothedSkew_) * coeff;
        smoothedCenter_ += (params_.center - smoothedCenter_) * coeff;

        const float baseSamples = std::clamp(params_.sizeMs * static_cast<float>(sampleRate_ * 0.001),
            8.0f,
            static_cast<float>(std::max<uint32_t>(8u, bufferSize_ / 2u)));
        const float denom = static_cast<float>(std::max<uint32_t>(1u, channels_ - 1u));
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            const float u = channels_ > 1u ? static_cast<float>(ch) / denom : 0.5f;
            const float centered = clamp((u - smoothedCenter_) * 2.0f, -1.0f, 1.0f);
            const float spreadRatio = std::pow(2.0f, centered * smoothedSpread_);
            const float devRatio = std::pow(2.0f, hashSigned(ch + 17u) * smoothedDeviation_ * 0.65f);
            const float skewRatio = std::pow(2.0f, smoothedSkew_ * u);
            lanes_[ch].targetSamples = std::clamp(baseSamples * spreadRatio * devRatio * skewRatio,
                8.0f,
                static_cast<float>(std::max<uint32_t>(8u, bufferSize_ / 2u)));
            lanes_[ch].smoothedSamples += (lanes_[ch].targetSamples - lanes_[ch].smoothedSamples) * coeff;
        }
    }

    void startCycle(uint32_t ch)
    {
        auto& lane = lanes_[ch];
        const float len = std::clamp(lane.smoothedSamples, 8.0f, static_cast<float>(std::max<uint32_t>(8u, bufferSize_ / 2u)));
        const uint32_t salt = cycleCounter_ * 131u + ch * 977u + 19u;
        if (lane.skipCycles > 0u) {
            --lane.skipCycles;
            lane.activeSamples = std::clamp(lane.skipSamples, 32.0f, len);
            const float jamHold = params_.skipJam * params_.skip;
            const float chaseOffset = params_.skipChase * static_cast<float>(ch) * lane.activeSamples * 0.11f;
            const float nudge = hashSigned(salt + 311u) * lane.activeSamples * params_.skip * lerp(0.08f, 0.015f, jamHold) + chaseOffset;
            lane.segmentStart = findBoundarySafeStart(ch, lane.segmentStart + nudge, lane.activeSamples, salt + 349u);
            if (hash01(salt + 337u) < params_.skip * params_.reverse * 0.45f) {
                lane.direction *= -1.0f;
            }
            armDeClick(lane);
            return;
        }
        const float scatter = params_.deviation * params_.repeat;
        const float offset = (0.15f + hash01(salt) * 3.85f) * len * scatter;
        lane.segmentStart = findBoundarySafeStart(ch, static_cast<float>(writePos_) - len - offset - 2.0f, len, salt + 73u);
        lane.activeSamples = len;
        if (params_.skip > 0.0001f && hash01(salt + 223u) < params_.skip * (0.26f + params_.repeat * 0.64f)) {
            lane.skipSamples = quantizedSkipLength(len, salt);
            lane.activeSamples = lane.skipSamples;
            lane.skipCycles = skipLockCycles(ch, salt);
            lane.segmentStart = findBoundarySafeStart(ch,
                lane.segmentStart + hashSigned(salt + 271u) * len * params_.skip * 0.18f
                    + params_.skipChase * static_cast<float>(ch) * lane.activeSamples * 0.25f,
                lane.activeSamples,
                salt + 293u);
        }
        const float reverseChance = params_.reverse;
        const bool rateReverse = params_.rate < 0.0f;
        const bool chanceReverse = hash01(salt + 41u) < reverseChance;
        lane.direction = (rateReverse ^ chanceReverse) ? -1.0f : 1.0f;
        armDeClick(lane);
    }

    float processLane(uint32_t ch)
    {
        auto& lane = lanes_[ch];
        const float baseLen = std::clamp(lane.smoothedSamples, 8.0f, static_cast<float>(std::max<uint32_t>(8u, bufferSize_ / 2u)));
        const float len = std::clamp(lane.activeSamples > 0.0f ? lane.activeSamples : baseLen, 8.0f, baseLen);
        if (lane.playPos <= 0.0f || lane.playPos >= len) {
            lane.playPos = std::fmod(std::max(0.0f, lane.playPos), len);
            startCycle(ch);
            if (ch == channels_ - 1u) {
                ++cycleCounter_;
            }
        }

        const float play = lane.direction > 0.0f ? lane.playPos : len - 1.0f - lane.playPos;
        float readIndex = lane.segmentStart + play;
        float sample = readWrapped(ch, readIndex);
        sample = memoryGhost(ch, readIndex, sample, len);

        const float xf = std::min(len * 0.48f, std::max(1.0f, len * params_.crossfade));
        if (xf > 1.0f) {
            if (lane.playPos < xf) {
                const float fade = lane.playPos / xf;
                const float wrapPlay = lane.direction > 0.0f ? len - xf + lane.playPos : xf - lane.playPos;
                const float wrapIndex = lane.segmentStart + wrapPlay;
                sample = lerp(readWrapped(ch, wrapIndex), sample, fade);
            } else if (lane.playPos > len - xf) {
                const float fade = (len - lane.playPos) / xf;
                const float wrapPlay = lane.direction > 0.0f ? lane.playPos - len : len - (lane.playPos - (len - xf)) - 1.0f;
                const float wrapIndex = lane.segmentStart + wrapPlay;
                sample = lerp(readWrapped(ch, wrapIndex), sample, fade);
            }
        }

        const uint32_t hold = static_cast<uint32_t>(1u + std::floor(params_.crush * params_.crush * 63.0f));
        if (hold > 1u) {
            if (lane.holdCounter == 0u) lane.heldSample = sample;
            sample = lane.heldSample;
            lane.holdCounter = (lane.holdCounter + 1u) % hold;
        } else {
            lane.holdCounter = 0u;
            lane.heldSample = sample;
        }

        if (params_.error > 0.0001f) {
            const float err = params_.error;
            const uint32_t errorMode = modeIndex(params_.errorMode, 4u);
            const float sourceGate = signalGate(lane, sample);
            const float pathGate = errorPathGate(lane, ch, errorMode, err, sourceGate);
            if (pathGate <= 0.0001f) {
                lane.errorSample += (sample - lane.errorSample) * 0.02f;
                lane.errorPcmCurrent += (sample - lane.errorPcmCurrent) * 0.02f;
                lane.errorPcmTarget += (sample - lane.errorPcmTarget) * 0.02f;
                lane.errorFrame = 0u;
            } else {
            const uint32_t frameLength = static_cast<uint32_t>(std::clamp(sampleRate_ * lerp(0.00035f, 0.0045f, err), 8.0, 256.0));
            if (lane.errorFrame == 0u || lane.errorFrameLength != frameLength) {
                lane.errorPcmCurrent = lane.errorPcmTarget;
                lane.errorPcmTarget = inventedPcmTarget(
                    writePos_ / std::max<uint32_t>(1u, frameLength) + cycleCounter_ * 17u,
                    ch,
                    lane.errorPcmCurrent,
                    sample,
                    err);
                lane.errorFrameLength = std::max<uint32_t>(1u, frameLength);
            }
            const float framePhase = static_cast<float>(lane.errorFrame) / static_cast<float>(std::max<uint32_t>(1u, lane.errorFrameLength));
            const float fakePcm = lerp(lane.errorPcmCurrent, lane.errorPcmTarget, smoothStep(framePhase));
            const float bits = std::floor(lerp(96.0f, 9.0f, err));
            const float quant = std::round(sample * bits) / std::max(1.0f, bits);
            float conceal = lerp(quant, fakePcm, err * 0.76f);
            if (errorMode == 1u) {
                const uint32_t block = static_cast<uint32_t>(std::max(16.0, sampleRate_ * lerp(0.002, 0.022, err)));
                const bool missing = hash01((writePos_ / block) * 199u + ch * 43u) < err * 0.38f;
                conceal = missing ? lane.errorSample : lerp(conceal, lane.errorSample, err * 0.28f);
            } else if (errorMode == 2u) {
                const float parity = (hash01(writePos_ / 128u + ch * 53u) < err * 0.13f) ? -conceal : conceal;
                conceal = lerp(conceal, parity, err * 0.48f);
            } else if (errorMode == 3u) {
                const float wrongDepth = std::round(fakePcm * lerp(24.0f, 7.0f, err)) / lerp(24.0f, 7.0f, err);
                const float folded = std::sin((wrongDepth + hashSigned(writePos_ / 512u + ch * 79u) * err * 0.18f) * 3.14159265359f);
                conceal = lerp(quant, folded * 0.58f, err * 0.72f);
            } else if (errorMode == 4u) {
                const uint32_t counter = writePos_ / std::max<uint32_t>(1u, frameLength / 2u) + ch * 101u + cycleCounter_ * 23u;
                const float raw = pseudoByteAudio(counter, ch);
                conceal = lerp(quant, raw, err * 0.88f);
            } else {
                const float parity = (hash01(writePos_ / 256u + ch * 53u) < err * 0.035f) ? -conceal : conceal;
                conceal = lerp(conceal, parity, err * 0.18f);
            }
            const float depth = std::min(1.0f, err * pathGate * lerp(0.48f, 1.0f, errorModeIntensity(errorMode)));
            sample = lerp(sample, conceal, depth);
            lane.errorSample += (sample - lane.errorSample) * lerp(0.10f, 0.38f, err);
            sample = lerp(sample, lane.errorSample, 0.10f * pathGate);
            lane.errorFrame = (lane.errorFrame + 1u) % std::max<uint32_t>(1u, lane.errorFrameLength);
            }
        } else {
            lane.errorCounter = 0u;
            lane.errorFrame = 0u;
            lane.errorFrameLength = 1u;
            lane.errorPathCounter = 0u;
            lane.errorPathLength = 1u;
            lane.errorPath = 0.0f;
            lane.errorPathTarget = 0.0f;
            lane.errorPcmCurrent = sample;
            lane.errorPcmTarget = sample;
            lane.errorSample = sample;
        }

        if (lane.deClickRemaining > 0u && lane.deClickTotal > 0u) {
            const float t = 1.0f - static_cast<float>(lane.deClickRemaining) / static_cast<float>(lane.deClickTotal);
            const float shaped = smoothStep(t);
            sample = lerp(lane.deClickStart, sample, shaped);
            --lane.deClickRemaining;
        }

        const float rate = std::max(0.02f, std::abs(params_.rate));
        const float repeatBias = lerp(0.35f, 1.85f, params_.repeat);
        lane.playPos += rate * repeatBias;
        lane.lastOutput = flushDenormal(sample);
        return lane.lastOutput;
    }

    double sampleRate_ = 48000.0;
    double transportBpm_ = 120.0;
    uint32_t channels_ = 0;
    uint32_t bufferSize_ = 0;
    uint32_t writePos_ = 0;
    uint32_t cycleCounter_ = 0;
    bool transportTempoAvailable_ = false;
    bool memoryCaptured_ = false;
    BufferProcessorParams params_ {};
    std::vector<std::vector<float>> buffer_;
    std::vector<std::vector<float>> memory_;
    std::vector<LaneState> lanes_;
    float smoothedSpread_ = 0.0f;
    float smoothedDeviation_ = 0.0f;
    float smoothedSkew_ = 0.0f;
    float smoothedCenter_ = 0.5f;
    float mixSmoothed_ = 0.45f;
    float gainSmoothed_ = 1.0f;
};

} // namespace s3g
