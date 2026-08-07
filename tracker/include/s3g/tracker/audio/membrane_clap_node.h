#pragma once

#include "s3g/tracker/audio/audio_node.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace s3g::standalone {
class EmbeddedClapEntrySession;
}

namespace s3g::tracker::audio {

struct MembraneClapTelemetry {
    uint64_t processErrorCount = 0u;
    uint64_t droppedEventCount = 0u;
    uint64_t reorderedEventCount = 0u;
};

// Trusted in-process adapter for the reusable s3g-dsp membrane CLAP. The
// processor remains behind its public CLAP event/state boundary; Tracker does
// not reach into the membrane implementation or duplicate its parameters.
class MembraneClapNode final : public InstrumentNode {
public:
    MembraneClapNode();
    ~MembraneClapNode() override;

    MembraneClapNode(const MembraneClapNode&) = delete;
    MembraneClapNode& operator=(const MembraneClapNode&) = delete;

    bool create(
        const s3g::standalone::EmbeddedClapEntrySession& entrySession);
    void destroy();
    bool isCreated() const noexcept;

    bool prepare(const AudioRenderSpec& spec) override;
    void unprepare() override;
    bool startProcessing() noexcept override;
    void stopProcessing() noexcept override;
    bool isProcessing() const noexcept override;
    void reset() noexcept override;
    AudioLayout outputLayout() const noexcept override
    {
        return AudioLayout::HoaThirdOrder;
    }
    uint32_t latencyFrames() const noexcept override { return 0u; }

    void render(const InstrumentRenderEvent* events,
        std::size_t eventCount, const PlanarAudioBlock& output) noexcept
        override;

    bool saveState(std::vector<uint8_t>& destination) const;
    bool loadState(const std::vector<uint8_t>& source);
    MembraneClapTelemetry telemetry() const noexcept;
    void resetTelemetry() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace s3g::tracker::audio
