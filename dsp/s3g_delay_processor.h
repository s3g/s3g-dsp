#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace s3g {

constexpr int kDelayProcessorMaxChannels = 128;

class DelayProcessor {
public:
    void prepare(double sampleRate, int channels, double maxDelaySeconds = 2.0)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        channels_ = std::clamp(channels, 0, kDelayProcessorMaxChannels);
        maxDelaySamples_ = std::max(2, static_cast<int>(std::ceil(sampleRate_ * maxDelaySeconds)) + 2);
        buffer_.assign(static_cast<size_t>(channels_) * static_cast<size_t>(maxDelaySamples_), 0.0f);
        writeIndex_ = 0;
        feedbackFilter_.assign(static_cast<size_t>(channels_), 0.0f);
        delayMs_.assign(static_cast<size_t>(channels_), 250.0f);
        delayTargetMs_.assign(static_cast<size_t>(channels_), 250.0f);
        delayFromMs_.assign(static_cast<size_t>(channels_), 250.0f);
        delayToMs_.assign(static_cast<size_t>(channels_), 250.0f);
        delayFade_.assign(static_cast<size_t>(channels_), 1.0f);
        delaySettleSamples_.assign(static_cast<size_t>(channels_), delaySettleSampleCount());
        feedback_.assign(static_cast<size_t>(channels_), 0.30f);
        feedbackTarget_.assign(static_cast<size_t>(channels_), 0.30f);
        tone_.assign(static_cast<size_t>(channels_), 0.65f);
        toneTarget_.assign(static_cast<size_t>(channels_), 0.65f);
        pitchSemitones_.assign(static_cast<size_t>(channels_), 0.0f);
        pitchTargetSemitones_.assign(static_cast<size_t>(channels_), 0.0f);
        pitchPhase_.assign(static_cast<size_t>(channels_), 0.0f);
        network_.assign(static_cast<size_t>(channels_), 0.0f);
        networkTarget_.assign(static_cast<size_t>(channels_), 0.0f);
        networkNeighborA_.assign(static_cast<size_t>(channels_), 0);
        networkNeighborB_.assign(static_cast<size_t>(channels_), 0);
        networkNeighborC_.assign(static_cast<size_t>(channels_), 0);
        networkNeighborCount_.assign(static_cast<size_t>(channels_), 2);
        networkCentroid_.assign(static_cast<size_t>(channels_), 0.22f);
        networkCentroidTarget_.assign(static_cast<size_t>(channels_), 0.22f);
        character_.assign(static_cast<size_t>(channels_), 0.0f);
        characterTarget_.assign(static_cast<size_t>(channels_), 0.0f);
        smearAmount_.assign(static_cast<size_t>(channels_), 0.0f);
        smearTargetAmount_.assign(static_cast<size_t>(channels_), 0.0f);
        smearFast_.assign(static_cast<size_t>(channels_), 0.0f);
        smearSlow_.assign(static_cast<size_t>(channels_), 0.0f);
        delayedFrame_.assign(static_cast<size_t>(channels_), 0.0f);
        filteredFrame_.assign(static_cast<size_t>(channels_), 0.0f);
        hasProcessed_ = false;
        for (int ch = 0; ch < channels_; ++ch) {
            const size_t index = static_cast<size_t>(ch);
            networkNeighborA_[index] = (ch - 1 + channels_) % std::max(1, channels_);
            networkNeighborB_[index] = (ch + 1) % std::max(1, channels_);
            networkNeighborC_[index] = (ch + channels_ / 2) % std::max(1, channels_);
        }
    }

    void reset()
    {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        std::fill(feedbackFilter_.begin(), feedbackFilter_.end(), 0.0f);
        writeIndex_ = 0;
        delayTargetMs_ = delayMs_;
        delayFromMs_ = delayMs_;
        delayToMs_ = delayMs_;
        std::fill(delayFade_.begin(), delayFade_.end(), 1.0f);
        std::fill(delaySettleSamples_.begin(), delaySettleSamples_.end(), delaySettleSampleCount());
        std::fill(pitchPhase_.begin(), pitchPhase_.end(), 0.0f);
        std::fill(smearFast_.begin(), smearFast_.end(), 0.0f);
        std::fill(smearSlow_.begin(), smearSlow_.end(), 0.0f);
        hasProcessed_ = false;
    }

    void setChannelDelayMs(int channel, float delayMs)
    {
        if (validChannel(channel)) {
            const size_t index = static_cast<size_t>(channel);
            const float target = clamp(delayMs, 1.0f, static_cast<float>(maxDelayMs()));
            if (!hasProcessed_) {
                delayMs_[index] = target;
                delayTargetMs_[index] = target;
                delayFromMs_[index] = target;
                delayToMs_[index] = target;
                delayFade_[index] = 1.0f;
                delaySettleSamples_[index] = delaySettleSampleCount();
                return;
            }

            if (std::fabs(target - delayTargetMs_[index]) < 0.25f) {
                return;
            }
            delayTargetMs_[index] = target;
            delaySettleSamples_[index] = 0;
        }
    }

    void setChannelFeedback(int channel, float feedback)
    {
        if (validChannel(channel)) {
            const size_t index = static_cast<size_t>(channel);
            feedbackTarget_[index] = clamp(feedback, 0.0f, 0.82f);
            if (!hasProcessed_) {
                feedback_[index] = feedbackTarget_[index];
            }
        }
    }

    void setChannelTone(int channel, float tone)
    {
        if (validChannel(channel)) {
            const size_t index = static_cast<size_t>(channel);
            toneTarget_[index] = clamp(tone, 0.0f, 1.0f);
            if (!hasProcessed_) {
                tone_[index] = toneTarget_[index];
            }
        }
    }

    void setChannelPitchSemitones(int channel, float semitones)
    {
        if (validChannel(channel)) {
            const size_t index = static_cast<size_t>(channel);
            pitchTargetSemitones_[index] = clamp(semitones, -24.0f, 24.0f);
            if (!hasProcessed_) {
                pitchSemitones_[index] = pitchTargetSemitones_[index];
            }
        }
    }

    void setChannelNetwork(int channel, float amount)
    {
        if (validChannel(channel)) {
            const size_t index = static_cast<size_t>(channel);
            networkTarget_[index] = clamp(amount, 0.0f, 0.75f);
            if (!hasProcessed_) {
                network_[index] = networkTarget_[index];
            }
        }
    }

    void setChannelNetworkNeighbors(int channel, int neighborA, int neighborB)
    {
        if (validChannel(channel)) {
            const size_t index = static_cast<size_t>(channel);
            networkNeighborA_[index] = validChannel(neighborA) ? neighborA : channel;
            networkNeighborB_[index] = validChannel(neighborB) ? neighborB : networkNeighborA_[index];
            networkNeighborC_[index] = networkNeighborB_[index];
            networkNeighborCount_[index] = 2;
        }
    }

    void setChannelNetworkTopology(int channel, int neighborA, int neighborB, int neighborC, int neighborCount, float centroidAmount)
    {
        if (validChannel(channel)) {
            const size_t index = static_cast<size_t>(channel);
            networkNeighborA_[index] = validChannel(neighborA) ? neighborA : channel;
            networkNeighborB_[index] = validChannel(neighborB) ? neighborB : networkNeighborA_[index];
            networkNeighborC_[index] = validChannel(neighborC) ? neighborC : networkNeighborB_[index];
            networkNeighborCount_[index] = std::clamp(neighborCount, 1, 3);
            networkCentroidTarget_[index] = clamp(centroidAmount, 0.0f, 1.0f);
            if (!hasProcessed_) {
                networkCentroid_[index] = networkCentroidTarget_[index];
            }
        }
    }

    void setChannelCharacter(int channel, float amount)
    {
        if (validChannel(channel)) {
            const size_t index = static_cast<size_t>(channel);
            characterTarget_[index] = clamp(amount, 0.0f, 1.0f);
            if (!hasProcessed_) {
                character_[index] = characterTarget_[index];
            }
        }
    }

    void setChannelSmearAmount(int channel, float amount)
    {
        if (validChannel(channel)) {
            const size_t index = static_cast<size_t>(channel);
            smearTargetAmount_[index] = clamp(amount, 0.0f, 1.0f);
            if (!hasProcessed_) {
                smearAmount_[index] = smearTargetAmount_[index];
            }
        }
    }

    void processFrame(const float* input, float* wetOutput)
    {
        if (!input || !wetOutput || channels_ <= 0 || maxDelaySamples_ <= 0) {
            return;
        }

        for (int ch = 0; ch < channels_; ++ch) {
            const size_t index = static_cast<size_t>(ch);
            updateDelayCommit(index);
            feedback_[index] += (feedbackTarget_[index] - feedback_[index]) * 0.0008f;
            tone_[index] += (toneTarget_[index] - tone_[index]) * 0.0030f;
            pitchSemitones_[index] += (pitchTargetSemitones_[index] - pitchSemitones_[index]) * 0.00025f;
            network_[index] += (networkTarget_[index] - network_[index]) * 0.0012f;
            networkCentroid_[index] += (networkCentroidTarget_[index] - networkCentroid_[index]) * 0.0012f;
            character_[index] += (characterTarget_[index] - character_[index]) * 0.0010f;
            smearAmount_[index] += (smearTargetAmount_[index] - smearAmount_[index]) * 0.0010f;

            const float delayed = readCommittedDelay(ch, index);
            const float age = character_[index];
            const float toneCoeff = 0.006f + tone_[index] * tone_[index] * (0.28f - age * 0.16f);
            float& filter = feedbackFilter_[index];
            filter = flushDenormal(filter + toneCoeff * (delayed - filter));

            delayedFrame_[index] = delayed;
            filteredFrame_[index] = filter;
            const float colored = (delayed + (filter - delayed) * (age * 0.42f)) * (1.0f - age * 0.18f);
            wetOutput[ch] = safetyLimit(applySmearTexture(index, colored));
        }

        float centroid = 0.0f;
        for (int ch = 0; ch < channels_; ++ch) {
            centroid += filteredFrame_[static_cast<size_t>(ch)];
        }
        centroid /= static_cast<float>(std::max(1, channels_));

        for (int ch = 0; ch < channels_; ++ch) {
            const size_t index = static_cast<size_t>(ch);
            const int neighborA = validChannel(networkNeighborA_[index]) ? networkNeighborA_[index] : ch;
            const int neighborB = validChannel(networkNeighborB_[index]) ? networkNeighborB_[index] : neighborA;
            const int neighborC = validChannel(networkNeighborC_[index]) ? networkNeighborC_[index] : neighborB;
            const int neighborCount = std::clamp(networkNeighborCount_[index], 1, 3);
            float neighbor = filteredFrame_[static_cast<size_t>(neighborA)];
            if (neighborCount >= 2) {
                neighbor += filteredFrame_[static_cast<size_t>(neighborB)];
            }
            if (neighborCount >= 3) {
                neighbor += filteredFrame_[static_cast<size_t>(neighborC)];
            }
            neighbor /= static_cast<float>(neighborCount);
            const float centroidMix = networkCentroid_[index];
            const float networked = neighbor * (1.0f - centroidMix) + centroid * centroidMix;
            const float feedbackSource = filteredFrame_[index] + (networked - filteredFrame_[index]) * network_[index];
            const float character = character_[index];
            const float feedbackTrim = 1.0f - character * 0.28f;
            const float feedbackSend = flushDenormal(tapeSaturate(feedbackSource * feedback_[index] * feedbackTrim, character));
            const float writeValue = input[ch] + feedbackSend;
            writeSample(ch, safetyLimit(tapeSaturate(writeValue, character * 0.35f)));
        }

        writeIndex_ = (writeIndex_ + 1) % maxDelaySamples_;
        hasProcessed_ = true;
    }

    int channels() const { return channels_; }
    double maxDelayMs() const { return (static_cast<double>(maxDelaySamples_ - 2) / sampleRate_) * 1000.0; }

private:
    bool validChannel(int channel) const
    {
        return channel >= 0 && channel < channels_;
    }

    size_t indexFor(int channel, int sample) const
    {
        return static_cast<size_t>(channel) * static_cast<size_t>(maxDelaySamples_) + static_cast<size_t>(sample);
    }

    float readDelay(int channel, float delaySamples) const
    {
        const float read = static_cast<float>(writeIndex_) - delaySamples;
        const float wrapped = read < 0.0f ? read + static_cast<float>(maxDelaySamples_) : read;
        const int i0 = static_cast<int>(std::floor(wrapped)) % maxDelaySamples_;
        const int i1 = (i0 + 1) % maxDelaySamples_;
        const int im1 = (i0 - 1 + maxDelaySamples_) % maxDelaySamples_;
        const int i2 = (i0 + 2) % maxDelaySamples_;
        const float frac = wrapped - std::floor(wrapped);
        return cubicInterpolate(
            buffer_[indexFor(channel, im1)],
            buffer_[indexFor(channel, i0)],
            buffer_[indexFor(channel, i1)],
            buffer_[indexFor(channel, i2)],
            frac);
    }

    float readPitchShiftedDelay(int channel, size_t index, float baseDelaySamples, bool advancePhase)
    {
        const float semitones = pitchSemitones_[index];
        if (std::fabs(semitones) < 0.01f) {
            return readDelay(channel, baseDelaySamples);
        }

        const float normal = readDelay(channel, baseDelaySamples);
        const float shiftBlend = std::clamp((std::fabs(semitones) - 0.02f) / 0.24f, 0.0f, 1.0f);
        if (shiftBlend <= 0.0f) {
            return normal;
        }

        const float ratio = std::pow(2.0f, semitones / 12.0f);
        const float windowSamples = std::clamp(
            static_cast<float>(sampleRate_ * 0.110),
            static_cast<float>(sampleRate_ * 0.050),
            std::max(16.0f, baseDelaySamples * 0.55f));
        const float increment = std::clamp(std::fabs(1.0f - ratio) / windowSamples, 0.0f, 0.025f);
        if (advancePhase) {
            pitchPhase_[index] += increment;
            if (pitchPhase_[index] >= 1.0f) {
                pitchPhase_[index] -= std::floor(pitchPhase_[index]);
            }
        }

        auto readHead = [&](float phase) {
            phase -= std::floor(phase);
            const float offset = ratio >= 1.0f ? (1.0f - phase) * windowSamples : phase * windowSamples;
            return readDelay(channel, baseDelaySamples + offset);
        };

        const float phaseA = pitchPhase_[index];
        const float phaseB = phaseA + 0.5f;
        const float wrappedB = phaseB - std::floor(phaseB);
        const float envA = std::sin(static_cast<float>(M_PI) * phaseA);
        const float envB = std::sin(static_cast<float>(M_PI) * wrappedB);
        const float weightA = envA * envA;
        const float weightB = envB * envB;
        const float weightSum = std::max(0.0001f, weightA + weightB);
        const float shifted = (readHead(phaseA) * weightA + readHead(phaseB) * weightB) / weightSum;
        return normal + (shifted - normal) * shiftBlend;
    }

    float readCommittedDelay(int channel, size_t index)
    {
        if (delayFade_[index] >= 1.0f) {
            const float delaySamples = delayMs_[index] * static_cast<float>(sampleRate_ * 0.001);
            return readPitchShiftedDelay(channel, index, delaySamples, true);
        }

        const float fade = smoothstep(delayFade_[index]);
        const float fromSamples = delayFromMs_[index] * static_cast<float>(sampleRate_ * 0.001);
        const float toSamples = delayToMs_[index] * static_cast<float>(sampleRate_ * 0.001);
        const float from = readPitchShiftedDelay(channel, index, fromSamples, true);
        const float to = readPitchShiftedDelay(channel, index, toSamples, false);
        return from + (to - from) * fade;
    }

    float applySmearTexture(size_t index, float primary)
    {
        const float amount = smearAmount_[index];
        if (amount <= 0.0001f) {
            return primary;
        }

        const float fastCoeff = 0.22f - amount * 0.12f;
        const float slowCoeff = 0.035f + amount * 0.020f;
        smearFast_[index] += (primary - smearFast_[index]) * fastCoeff;
        smearSlow_[index] += (smearFast_[index] - smearSlow_[index]) * slowCoeff;
        const float texture = smearFast_[index] * 0.56f + smearSlow_[index] * 0.44f;
        const float mix = std::min(0.52f, amount * 0.46f);
        return primary + (texture - primary) * mix;
    }

    void updateDelayCommit(size_t index)
    {
        if (delaySettleSamples_[index] < delaySettleSampleCount()) {
            ++delaySettleSamples_[index];
        }

        if (delayFade_[index] < 1.0f) {
            delayFade_[index] = std::min(1.0f, delayFade_[index] + 1.0f / (static_cast<float>(sampleRate_) * delayFadeSeconds()));
            if (delayFade_[index] >= 1.0f) {
                delayMs_[index] = delayToMs_[index];
                delayFromMs_[index] = delayMs_[index];
            }
            return;
        }

        if (delaySettleSamples_[index] < delaySettleSampleCount()) {
            return;
        }

        if (std::fabs(delayTargetMs_[index] - delayMs_[index]) < 0.25f) {
            return;
        }

        delayFromMs_[index] = delayMs_[index];
        delayToMs_[index] = delayTargetMs_[index];
        delayFade_[index] = 0.0f;
    }

    int delaySettleSampleCount() const
    {
        return std::max(1, static_cast<int>(sampleRate_ * 0.120));
    }

    float delayFadeSeconds() const
    {
        return 0.250f;
    }

    float smoothstep(float value) const
    {
        value = clamp(value, 0.0f, 1.0f);
        return value * value * (3.0f - 2.0f * value);
    }

    float cubicInterpolate(float y0, float y1, float y2, float y3, float t) const
    {
        const float a0 = y3 - y2 - y0 + y1;
        const float a1 = y0 - y1 - a0;
        const float a2 = y2 - y0;
        const float a3 = y1;
        return ((a0 * t + a1) * t + a2) * t + a3;
    }

    float softClip(float value) const
    {
        return std::tanh(value * 0.95f) * 1.0526316f;
    }

    float tapeSaturate(float value, float amount) const
    {
        const float drive = 0.95f + amount * 1.35f;
        const float shaped = std::tanh(value * drive) / std::max(1.0f, drive);
        return value + (shaped - value) * (amount * 0.72f);
    }

    float safetyLimit(float value) const
    {
        const float magnitude = std::fabs(value);
        if (magnitude <= 1.0f) {
            return value;
        }
        const float limited = 1.0f + std::tanh((magnitude - 1.0f) * 0.65f) * 0.85f;
        return std::copysign(limited, value);
    }

    void writeSample(int channel, float value)
    {
        buffer_[indexFor(channel, writeIndex_)] = value;
    }

    double sampleRate_ = 48000.0;
    int channels_ = 0;
    int maxDelaySamples_ = 0;
    int writeIndex_ = 0;
    bool hasProcessed_ = false;
    std::vector<float> buffer_;
    std::vector<float> feedbackFilter_;
    std::vector<float> delayMs_;
    std::vector<float> delayTargetMs_;
    std::vector<float> delayFromMs_;
    std::vector<float> delayToMs_;
    std::vector<float> delayFade_;
    std::vector<int> delaySettleSamples_;
    std::vector<float> feedback_;
    std::vector<float> feedbackTarget_;
    std::vector<float> tone_;
    std::vector<float> toneTarget_;
    std::vector<float> pitchSemitones_;
    std::vector<float> pitchTargetSemitones_;
    std::vector<float> pitchPhase_;
    std::vector<float> network_;
    std::vector<float> networkTarget_;
    std::vector<int> networkNeighborA_;
    std::vector<int> networkNeighborB_;
    std::vector<int> networkNeighborC_;
    std::vector<int> networkNeighborCount_;
    std::vector<float> networkCentroid_;
    std::vector<float> networkCentroidTarget_;
    std::vector<float> character_;
    std::vector<float> characterTarget_;
    std::vector<float> smearAmount_;
    std::vector<float> smearTargetAmount_;
    std::vector<float> smearFast_;
    std::vector<float> smearSlow_;
    std::vector<float> delayedFrame_;
    std::vector<float> filteredFrame_;
};

} // namespace s3g
