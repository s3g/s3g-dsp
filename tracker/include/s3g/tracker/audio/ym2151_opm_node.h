#pragma once

#include "s3g/tracker/audio/audio_node.h"

#include <memory>

namespace s3g::tracker::audio {

// The tracker accepts the complete MIDI note domain. The OPM key-code format
// has eight octaves, so notes outside this interval repeat the nearest endpoint
// rather than becoming silent.
constexpr int kYm2151MinimumDistinctMidiNote = 12;
constexpr int kYm2151MaximumDistinctMidiNote = 107;

// A playable eight-voice YM2151 instrument backed by Aaron Giles' BSD-3
// ymfm core. Tracker notes are translated to OPM key codes and the chip's
// native clock domain is linearly rate-converted into the host callback.
class Ym2151OpmNode final : public InstrumentNode {
public:
    Ym2151OpmNode();
    ~Ym2151OpmNode() override;

    Ym2151OpmNode(const Ym2151OpmNode&) = delete;
    Ym2151OpmNode& operator=(const Ym2151OpmNode&) = delete;

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
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace s3g::tracker::audio
