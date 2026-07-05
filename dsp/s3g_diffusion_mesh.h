#pragma once

#include "s3g_24ch_layout.h"
#include "s3g_math.h"

#include <array>

namespace s3g {

constexpr int kMaxRealtimeChannels = 128;

class DiffusionMesh {
public:
    void prepare(int channels)
    {
        channels_ = std::min(std::max(channels, 0), kMaxRealtimeChannels);
        reset();
    }

    void reset()
    {
        state_.fill(0.0f);
    }

    void setAmount(float amount)
    {
        amount_ = clamp(amount, 0.0f, 1.0f);
        amountByChannel_.fill(amount_);
    }

    void setFeedback(float feedback)
    {
        feedback_ = clamp(feedback, 0.0f, 0.95f);
        feedbackByChannel_.fill(feedback_);
    }

    void setChannelAmount(int channel, float amount)
    {
        if (channel >= 0 && channel < kMaxRealtimeChannels) {
            amountByChannel_[channel] = clamp(amount, 0.0f, 1.0f);
        }
    }

    void setChannelFeedback(int channel, float feedback)
    {
        if (channel >= 0 && channel < kMaxRealtimeChannels) {
            feedbackByChannel_[channel] = clamp(feedback, 0.0f, 0.95f);
        }
    }

    void processFrame(const float* input, float* output)
    {
        if (!input || !output || channels_ <= 0) {
            return;
        }

        for (int ch = 0; ch < channels_; ++ch) {
            const int left = (ch + channels_ - 1) % channels_;
            const int right = (ch + 1) % channels_;
            const float neighbor = 0.5f * (state_[left] + state_[right]);
            const float diffused = lerp(input[ch], neighbor, amountByChannel_[ch]);
            output[ch] = diffused;
        }

        for (int ch = 0; ch < channels_; ++ch) {
            state_[ch] = input[ch] + output[ch] * feedbackByChannel_[ch];
        }
    }

    int channels() const { return channels_; }

private:
    std::array<float, kMaxRealtimeChannels> state_ {};
    std::array<float, kMaxRealtimeChannels> amountByChannel_ {};
    std::array<float, kMaxRealtimeChannels> feedbackByChannel_ {};
    int channels_ = 0;
    float amount_ = 0.0f;
    float feedback_ = 0.0f;
};

class DiffusionMesh24 {
public:
    DiffusionMesh24()
    {
        mesh_.prepare(kVirtualSpeakerCount);
    }

    void reset() { mesh_.reset(); }
    void setAmount(float amount) { mesh_.setAmount(amount); }
    void setFeedback(float feedback) { mesh_.setFeedback(feedback); }
    void processFrame(const float* input, float* output) { mesh_.processFrame(input, output); }

private:
    DiffusionMesh mesh_;
};

} // namespace s3g
