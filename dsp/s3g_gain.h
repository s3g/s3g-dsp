#pragma once

#include "s3g_math.h"
#include "s3g_smoothing.h"

namespace s3g {

class MultichannelGain {
public:
    void prepare(float sampleRate, int channels, float smoothingMs = 10.0f)
    {
        channels_ = std::max(0, channels);
        gain_.reset(sampleRate, smoothingMs, 1.0f);
    }

    void setGainDb(float db)
    {
        gain_.setTarget(dbToGain(db));
    }

    template <typename Sample>
    void process(Sample** outputs, int frameCount)
    {
        if (!outputs || channels_ <= 0 || frameCount <= 0) {
            return;
        }

        for (int i = 0; i < frameCount; ++i) {
            const float g = gain_.next();
            for (int ch = 0; ch < channels_; ++ch) {
                outputs[ch][i] = static_cast<Sample>(outputs[ch][i] * g);
            }
        }
    }

private:
    int channels_ = 0;
    SmoothedValue gain_;
};

} // namespace s3g
