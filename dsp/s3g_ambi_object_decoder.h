#pragma once

#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_layout_panner.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

enum class AmbiObjectMethod : uint32_t {
    Vbap = 0,
    Lbap = 1,
    Dbap = 2,
};

struct AmbiObjectDecoderParams {
    AmbiSpeakerDecoderParams decoder {};
    AmbiObjectMethod objectMethod = AmbiObjectMethod::Vbap;
    float objectBlend = 0.35f;
    float objectGainDb = 0.0f;
    float fieldGainDb = 0.0f;
    float directionSmoothingMs = 25.0f;
    float objectConfidence = 0.18f;
    float objectHighpassHz = 250.0f;
};

class AmbiObjectDecoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        field_.prepare(sampleRate_);
        object_.prepare(sampleRate_);
        syncObjectLayout();
        updateObjectParams();
        reset();
    }

    void reset()
    {
        smoothedDir_ = { 1.0f, 0.0f, 0.0f };
        hpW_ = hpX_ = hpY_ = hpZ_ = {};
    }

    void setParams(AmbiObjectDecoderParams params)
    {
        params.decoder.outputGainDb = clamp(params.decoder.outputGainDb, -60.0f, 12.0f);
        params.objectMethod = static_cast<AmbiObjectMethod>(
            std::clamp<uint32_t>(static_cast<uint32_t>(params.objectMethod), 0u, 2u));
        params.objectBlend = clamp(params.objectBlend, 0.0f, 1.0f);
        params.objectGainDb = clamp(params.objectGainDb, -60.0f, 18.0f);
        params.fieldGainDb = clamp(params.fieldGainDb, -60.0f, 18.0f);
        params.directionSmoothingMs = clamp(params.directionSmoothingMs, 0.0f, 500.0f);
        params.objectConfidence = clamp(params.objectConfidence, 0.0f, 0.95f);
        params.objectHighpassHz = clamp(params.objectHighpassHz, 0.0f, 5000.0f);

        const auto oldLayout = params_.decoder.layout;
        const uint32_t oldSpeakers = params_.decoder.activeSpeakers;
        params_ = params;
        field_.setParams(params_.decoder);
        params_.decoder = field_.params();
        if (params_.decoder.layout != oldLayout || params_.decoder.activeSpeakers != oldSpeakers) {
            syncObjectLayout();
        }
        updateObjectParams();
    }

    AmbiObjectDecoderParams params() const { return params_; }
    const AmbiSpeakerDecoder& fieldDecoder() const { return field_; }

    void processFrame(const float* input, float* output, uint32_t inputChannels, uint32_t outputChannels)
    {
        if (!output) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiSpeakerDecoderMaxSpeakers);
        for (uint32_t i = 0; i < outputChannels; ++i) output[i] = 0.0f;
        if (!input || inputChannels == 0u) return;

        std::array<float, kAmbiSpeakerDecoderMaxSpeakers> fieldOut {};
        std::array<float, kAmbiSpeakerDecoderMaxSpeakers> objectOut {};
        std::array<float, kLayoutPannerSources> objectIn {};

        field_.processFrame(input, fieldOut.data());
        const DirectionCue cue = directionCue(input, inputChannels);
        const Vec3 dir = smoothDirection(cue.dir, cue.confidence);
        LayoutPannerSource source {};
        source.azimuthDeg = std::atan2(dir.y, dir.x) * 180.0f / kPi;
        source.elevationDeg = std::asin(clamp(dir.z, -1.0f, 1.0f)) * 180.0f / kPi;
        source.distance = 1.0f;
        source.gainDb = params_.objectGainDb;
        layoutPannerSyncSourcePositionFromAed(source);
        object_.setSource(0, source);
        objectIn[0] = cue.signal;
        object_.processFrame(objectIn.data(), objectOut.data(), 1u, outputChannels);

        const float blend = params_.objectBlend;
        const float fieldGain = dbToGain(params_.fieldGainDb);
        const float outputGain = dbToGain(params_.decoder.outputGainDb);
        for (uint32_t spk = 0; spk < outputChannels; ++spk) {
            output[spk] = flushDenormal(fieldOut[spk] * fieldGain * (1.0f - blend) + objectOut[spk] * outputGain * blend);
        }
    }

    template <typename Sample>
    void processBlock(const Sample* const* input, Sample* const* output, uint32_t inputChannels, uint32_t outputChannels, uint32_t frames)
    {
        if (!output || frames == 0u) return;
        outputChannels = std::min<uint32_t>(outputChannels, kAmbiSpeakerDecoderMaxSpeakers);
        for (uint32_t ch = 0; ch < outputChannels; ++ch) {
            if (output[ch]) std::fill(output[ch], output[ch] + frames, static_cast<Sample>(0));
        }
        if (!input) return;

        std::array<float, kAmbiSpeakerDecoderMaxChannels> frameIn {};
        std::array<float, kAmbiSpeakerDecoderMaxSpeakers> frameOut {};
        for (uint32_t frame = 0; frame < frames; ++frame) {
            const uint32_t channels = std::min<uint32_t>(inputChannels, kAmbiSpeakerDecoderMaxChannels);
            for (uint32_t ch = 0; ch < channels; ++ch) frameIn[ch] = input[ch] ? static_cast<float>(input[ch][frame]) : 0.0f;
            for (uint32_t ch = channels; ch < kAmbiSpeakerDecoderMaxChannels; ++ch) frameIn[ch] = 0.0f;
            processFrame(frameIn.data(), frameOut.data(), channels, outputChannels);
            for (uint32_t ch = 0; ch < outputChannels; ++ch) {
                if (output[ch]) output[ch][frame] = static_cast<Sample>(frameOut[ch]);
            }
        }
    }

private:
    struct DirectionCue {
        Vec3 dir { 1.0f, 0.0f, 0.0f };
        float signal = 0.0f;
        float confidence = 0.0f;
    };

    struct HighpassState {
        float lp = 0.0f;
    };

    float objectHighpass(float x, HighpassState& state) const
    {
        const float cutoff = params_.objectHighpassHz;
        if (cutoff <= 1.0f) return x;
        const float alpha = 1.0f - std::exp(-2.0f * kPi * cutoff / static_cast<float>(sampleRate_));
        state.lp += alpha * (x - state.lp);
        return x - state.lp;
    }

    static float smoothstep(float edge0, float edge1, float x)
    {
        const float t = clamp((x - edge0) / std::max(0.000001f, edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    DirectionCue directionCue(const float* input, uint32_t inputChannels)
    {
        const float w = objectHighpass(inputChannels > 0u ? input[0] : 0.0f, hpW_);
        const float y = objectHighpass(inputChannels > 1u ? input[1] : 0.0f, hpY_);
        const float z = objectHighpass(inputChannels > 2u ? input[2] : 0.0f, hpZ_);
        const float x = objectHighpass(inputChannels > 3u ? input[3] : 0.0f, hpX_);
        Vec3 dir { w * x, w * y, w * z };
        const float intensity = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        const float directional = std::sqrt(x * x + y * y + z * z);
        const float confidence = directional / std::max(0.000001f, directional + 0.70710678f * std::abs(w));
        const float gate = smoothstep(params_.objectConfidence, 1.0f, confidence);
        if (intensity < 0.000001f) dir = smoothedDir_;
        else dir = normalize(dir);
        return { dir, w * gate, confidence };
    }

    Vec3 smoothDirection(Vec3 target, float confidence)
    {
        const float adaptive = 1.0f + (1.0f - clamp(confidence, 0.0f, 1.0f)) * 3.0f;
        const float ms = params_.directionSmoothingMs * adaptive;
        const float coef = ms <= 0.0f ? 0.0f : std::exp(-1.0f / static_cast<float>(sampleRate_ * ms * 0.001));
        smoothedDir_.x = target.x + (smoothedDir_.x - target.x) * coef;
        smoothedDir_.y = target.y + (smoothedDir_.y - target.y) * coef;
        smoothedDir_.z = target.z + (smoothedDir_.z - target.z) * coef;
        smoothedDir_ = normalize(smoothedDir_);
        return smoothedDir_;
    }

    void syncObjectLayout()
    {
        std::array<LayoutPannerSpeaker, kLayoutPannerMaxSpeakers> speakers {};
        const auto& fieldSpeakers = field_.speakers();
        for (uint32_t i = 0; i < kAmbiSpeakerDecoderMaxSpeakers; ++i) {
            speakers[i].azimuthDeg = fieldSpeakers[i].azimuthDeg;
            speakers[i].elevationDeg = fieldSpeakers[i].elevationDeg;
            speakers[i].distance = fieldSpeakers[i].distance;
        }
        object_.setSpeakers(speakers, params_.decoder.activeSpeakers);
    }

    void updateObjectParams()
    {
        auto p = object_.params();
        p.layout = LayoutPannerPreset::Custom;
        p.activeSources = 1u;
        p.activeSpeakers = params_.decoder.activeSpeakers;
        p.method = params_.objectMethod == AmbiObjectMethod::Lbap
            ? LayoutPannerMethod::Lbap
            : (params_.objectMethod == AmbiObjectMethod::Dbap ? LayoutPannerMethod::Dbap : LayoutPannerMethod::Vbap);
        p.outputGainDb = 0.0f;
        p.smoothingMs = 8.0f;
        p.focus = 1.0f;
        p.distanceRolloffDb = 6.0f;
        p.insideMode = LayoutPannerInsideMode::Hold;
        object_.setParams(p);
    }

    double sampleRate_ = 48000.0;
    AmbiObjectDecoderParams params_ {};
    AmbiSpeakerDecoder field_ {};
    LayoutPanner object_ {};
    Vec3 smoothedDir_ { 1.0f, 0.0f, 0.0f };
    HighpassState hpW_ {};
    HighpassState hpX_ {};
    HighpassState hpY_ {};
    HighpassState hpZ_ {};
};

inline const char* ambiObjectMethodName(AmbiObjectMethod method)
{
    switch (method) {
    case AmbiObjectMethod::Lbap: return "LBAP";
    case AmbiObjectMethod::Dbap: return "DBAP";
    case AmbiObjectMethod::Vbap:
    default: return "VBAP";
    }
}

} // namespace s3g
