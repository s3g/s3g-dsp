#pragma once

#include "s3g_math.h"
#include "s3g_voice_output_allocator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kIterateDelayMaximumChannels = 16u;
constexpr uint32_t kIterateDelayMaximumVoices = 32u;

struct IterateDelayParams {
    float delayMs = 260.0f;
    float delayRandom = 0.25f;
    float windowMs = 420.0f;
    float pitchRandomSemitones = 2.0f;
    float amplitudeRandom = 0.15f;
    float fade = 0.08f;
    float dry = 0.1f;
    float wet = 0.9f;
    float gainDb = 0.0f;
    uint32_t traversal = static_cast<uint32_t>(
        routing::OutputTraversal::RandomCycle);
    bool stereoEvents = true;
    bool splitStereo = false;
    bool avoidAdjacent = false;
    uint32_t seed = 1979u;
};

class IterateDelay {
public:
    bool prepare(double sampleRate, uint32_t maxBlockFrames = 4096u,
                 double maximumHistorySeconds = 8.0)
    {
        if (!std::isfinite(sampleRate) || sampleRate <= 1.0) return false;
        sampleRate_ = sampleRate;
        maxBlockFrames_ = std::max<uint32_t>(1u, maxBlockFrames);
        bufferSize_ = std::max<uint32_t>(1024u, static_cast<uint32_t>(
            std::ceil(sampleRate_ * maximumHistorySeconds)));
        try {
            historyLeft_.assign(bufferSize_, 0.0f);
            historyRight_.assign(bufferSize_, 0.0f);
        } catch (...) {
            return false;
        }
        ready_ = true;
        reset();
        return true;
    }

    void reset()
    {
        std::fill(historyLeft_.begin(), historyLeft_.end(), 0.0f);
        std::fill(historyRight_.begin(), historyRight_.end(), 0.0f);
        writePosition_ = 0u;
        filledFrames_ = 0u;
        eventCountdown_ = 0u;
        generation_ = 0u;
        randomState_ = params_.seed != 0u ? params_.seed : 1979u;
        allocator_.reset(randomState_);
        for (auto& voice : voices_) voice = {};
    }

    void setParams(const IterateDelayParams& params)
    {
        const uint32_t oldSeed = params_.seed;
        params_ = sanitize(params);
        if (oldSeed != params_.seed) {
            randomState_ = params_.seed;
            allocator_.reset(randomState_);
            generation_ = 0u;
        }
    }

    IterateDelayParams params() const { return params_; }

    void setOutputChannels(uint32_t channels)
    {
        channels = std::clamp<uint32_t>(channels, 2u,
            kIterateDelayMaximumChannels);
        if (channels == outputChannels_) return;
        outputChannels_ = channels;
        allocator_.reset(randomState_);
        for (auto& voice : voices_) voice.active = false;
        generation_ = 0u;
    }

    uint32_t outputChannels() const { return outputChannels_; }
    bool ready() const { return ready_; }

    void process(const float* left, const float* right, float* const* output,
                 uint32_t frames)
    {
        if (!ready_ || !output || frames == 0u) return;
        frames = std::min(frames, maxBlockFrames_);
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const float inputLeft = finite(left ? left[frame] : 0.0f);
            const float inputRight = finite(right ? right[frame] : inputLeft);
            historyLeft_[writePosition_] = inputLeft;
            historyRight_[writePosition_] = inputRight;
            writePosition_ = (writePosition_ + 1u) % bufferSize_;
            filledFrames_ = std::min<uint32_t>(bufferSize_, filledFrames_ + 1u);

            for (uint32_t channel = 0u; channel < outputChannels_; ++channel)
                output[channel][frame] = 0.0f;

            const uint32_t windowFrames = currentWindowFrames();
            if (filledFrames_ > windowFrames + 2u) {
                if (eventCountdown_ == 0u) {
                    launchVoice(windowFrames);
                    eventCountdown_ = nextDelayFrames();
                } else {
                    --eventCountdown_;
                }
            }

            std::array<float, kIterateDelayMaximumChannels> wet {};
            for (auto& voice : voices_) renderVoice(voice, wet.data());
            const float gain = dbToGain(params_.gainDb);
            for (uint32_t channel = 0u; channel < outputChannels_; ++channel)
                output[channel][frame] = std::tanh(wet[channel]
                    * params_.wet * gain);
            output[0u][frame] += inputLeft * params_.dry;
            output[outputChannels_ / 2u][frame] += inputRight * params_.dry;
        }
    }

private:
    struct Voice {
        bool active = false;
        uint32_t start = 0u;
        uint32_t sourceFrames = 0u;
        double phase = 0.0;
        float rate = 1.0f;
        float gain = 1.0f;
        routing::VoiceOutputAssignment assignment {};
    };

    static IterateDelayParams sanitize(IterateDelayParams p)
    {
        p.delayMs = std::clamp(p.delayMs, 10.0f, 4000.0f);
        p.delayRandom = std::clamp(p.delayRandom, 0.0f, 1.0f);
        p.windowMs = std::clamp(p.windowMs, 20.0f, 2500.0f);
        p.pitchRandomSemitones = std::clamp(
            p.pitchRandomSemitones, 0.0f, 24.0f);
        p.amplitudeRandom = std::clamp(p.amplitudeRandom, 0.0f, 1.0f);
        p.fade = std::clamp(p.fade, 0.0f, 0.95f);
        p.dry = std::clamp(p.dry, 0.0f, 1.0f);
        p.wet = std::clamp(p.wet, 0.0f, 1.0f);
        p.gainDb = std::clamp(p.gainDb, -60.0f, 12.0f);
        p.traversal = std::min<uint32_t>(p.traversal,
            static_cast<uint32_t>(routing::OutputTraversal::RandomCycle));
        if (p.seed == 0u) p.seed = 1979u;
        return p;
    }

    static float finite(float value)
    {
        return std::isfinite(value) ? value : 0.0f;
    }

    float randomUnit()
    {
        randomState_ ^= randomState_ << 13u;
        randomState_ ^= randomState_ >> 17u;
        randomState_ ^= randomState_ << 5u;
        return static_cast<float>(randomState_ & 0x00ffffffu)
            / static_cast<float>(0x01000000u);
    }

    uint32_t currentWindowFrames() const
    {
        return std::clamp<uint32_t>(static_cast<uint32_t>(
            std::round(params_.windowMs * sampleRate_ * 0.001)),
            16u, bufferSize_ - 2u);
    }

    uint32_t nextDelayFrames()
    {
        const float bipolar = randomUnit() * 2.0f - 1.0f;
        const float milliseconds = params_.delayMs
            * std::max(0.05f, 1.0f + bipolar * params_.delayRandom);
        return std::max<uint32_t>(1u, static_cast<uint32_t>(
            std::round(milliseconds * sampleRate_ * 0.001)));
    }

    routing::VoiceOutputRouting outputRouting() const
    {
        routing::VoiceOutputRouting result;
        result.traversal = static_cast<routing::OutputTraversal>(
            params_.traversal);
        result.width = params_.stereoEvents
            ? routing::OutputVoiceWidth::Stereo
            : routing::OutputVoiceWidth::Mono;
        result.pairLayout = params_.splitStereo
            ? routing::StereoPairLayout::SplitBanks
            : routing::StereoPairLayout::Adjacent;
        result.avoidAdjacent = params_.avoidAdjacent;
        return result;
    }

    void launchVoice(uint32_t windowFrames)
    {
        Voice* voice = nullptr;
        for (auto& candidate : voices_) {
            if (!candidate.active) {
                voice = &candidate;
                break;
            }
        }
        if (!voice) voice = &voices_[generation_ % voices_.size()];
        const float semitones = (randomUnit() * 2.0f - 1.0f)
            * params_.pitchRandomSemitones;
        const uint32_t destinationCount = params_.stereoEvents
            ? std::max<uint32_t>(1u, outputChannels_ / 2u)
            : outputChannels_;
        const uint32_t generationInCycle = generation_
            % std::max<uint32_t>(1u, destinationCount);
        voice->active = true;
        voice->sourceFrames = windowFrames;
        voice->start = (writePosition_ + bufferSize_ - windowFrames)
            % bufferSize_;
        voice->phase = 0.0;
        voice->rate = std::pow(2.0f, semitones / 12.0f);
        voice->gain = (1.0f - params_.amplitudeRandom * randomUnit())
            * std::pow(1.0f - params_.fade,
                static_cast<float>(generationInCycle));
        voice->assignment = allocator_.next(outputChannels_, outputRouting());
        ++generation_;
    }

    float readHistory(const std::vector<float>& history,
                      const Voice& voice) const
    {
        const double position = voice.phase;
        const uint32_t firstOffset = static_cast<uint32_t>(position)
            % voice.sourceFrames;
        const uint32_t secondOffset = (firstOffset + 1u)
            % voice.sourceFrames;
        const uint32_t first = (voice.start + firstOffset) % bufferSize_;
        const uint32_t second = (voice.start + secondOffset) % bufferSize_;
        const float amount = static_cast<float>(position - std::floor(position));
        return history[first] + (history[second] - history[first]) * amount;
    }

    void renderVoice(Voice& voice, float* wet)
    {
        if (!voice.active || !wet || voice.sourceFrames < 2u) return;
        const float unit = static_cast<float>(voice.phase
            / static_cast<double>(voice.sourceFrames));
        if (unit >= 1.0f) {
            voice.active = false;
            return;
        }
        const float envelope = std::sin(std::clamp(unit, 0.0f, 1.0f)
            * s3g::kPi);
        const float left = readHistory(historyLeft_, voice)
            * envelope * voice.gain;
        const float right = readHistory(historyRight_, voice)
            * envelope * voice.gain;
        wet[voice.assignment.firstChannel] += left;
        if (voice.assignment.channelCount > 1u)
            wet[voice.assignment.secondChannel] += right;
        else
            wet[voice.assignment.firstChannel] += right * 0.5f;
        voice.phase += voice.rate;
    }

    double sampleRate_ = 48000.0;
    uint32_t maxBlockFrames_ = 0u;
    uint32_t bufferSize_ = 0u;
    uint32_t writePosition_ = 0u;
    uint32_t filledFrames_ = 0u;
    uint32_t eventCountdown_ = 0u;
    uint32_t generation_ = 0u;
    uint32_t randomState_ = 1979u;
    uint32_t outputChannels_ = kIterateDelayMaximumChannels;
    bool ready_ = false;
    IterateDelayParams params_ {};
    std::vector<float> historyLeft_;
    std::vector<float> historyRight_;
    std::array<Voice, kIterateDelayMaximumVoices> voices_ {};
    routing::TriggerOutputAllocator<kIterateDelayMaximumChannels> allocator_ {};
};

} // namespace s3g
