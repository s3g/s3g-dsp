#pragma once

#include "s3g/tracker/audio/audio_node.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace s3g::tracker::audio {

// An original, register-free performance instrument based on the SN76489
// signal topology: three square tone generators plus a 15-bit LFSR noise
// generator. It intentionally does not copy an emulator core or reproduce a
// console clock/register interface; tracker notes address it directly with
// sample-offset events.
class Sn76489PsgNode final : public InstrumentNode {
public:
    bool prepare(const AudioRenderSpec& spec) override;
    void unprepare() override;
    bool startProcessing() noexcept override;
    void stopProcessing() noexcept override;
    bool isProcessing() const noexcept override;
    void reset() noexcept override;
    AudioLayout outputLayout() const noexcept override;
    uint32_t latencyFrames() const noexcept override;
    void render(const InstrumentRenderEvent* events, std::size_t eventCount,
        const PlanarAudioBlock& output) noexcept override;

private:
    struct ToneVoice {
        uint64_t noteId = 0u;
        double phase = 0.0;
        double phaseIncrement = 0.0;
        float envelope = 0.0f;
        float releaseRate = 0.0f;
        bool active = false;
        bool releasing = false;
    };

    void handleEvent(const InstrumentRenderEvent& event) noexcept;
    float nextSample() noexcept;

    std::array<ToneVoice, 3u> tones_ {};
    double sampleRate_ = 48000.0;
    double noisePhase_ = 0.0;
    double noiseIncrement_ = 0.0;
    uint16_t noiseLfsr_ = 0x4000u;
    float noiseEnvelope_ = 0.0f;
    float noiseDecay_ = 0.999f;
    float toneMix_ = 0.18f;
    float noiseMix_ = 0.20f;
    double releaseSeconds_ = 0.018;
    std::size_t nextVoice_ = 0u;
    bool prepared_ = false;
    bool processing_ = false;
};

} // namespace s3g::tracker::audio
