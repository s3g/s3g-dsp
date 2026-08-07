#pragma once

#include "s3g/tracker/audio/audio_node.h"
#include "s3g/tracker/instrument_rack.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace s3g::tracker::audio {

// A one-shot native adapter for the five selected DaisySP drum models. Each
// rack node owns a separate instance and therefore separate oscillator,
// filter, envelope, and random state. MIDI note pitch offsets the editor's
// base frequency around the conventional GM reference note for the voice.
class DaisyDrumNode final : public InstrumentNode {
public:
    DaisyDrumNode();
    ~DaisyDrumNode() override;

    DaisyDrumNode(const DaisyDrumNode&) = delete;
    DaisyDrumNode& operator=(const DaisyDrumNode&) = delete;

    bool configure(InstrumentKind kind) noexcept;
    InstrumentKind kind() const noexcept;

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
