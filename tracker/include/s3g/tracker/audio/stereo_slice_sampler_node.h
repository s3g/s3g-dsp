#pragma once

#include "s3g/tracker/audio/audio_node.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace s3g::tracker::audio {

constexpr std::size_t kMaximumSamplerSlices = 128u;
constexpr std::size_t kMaximumSamplerVoices = 32u;
constexpr double kMaximumSamplerEnvelopeMilliseconds = 10000.0;

// Per-instrument amplitude envelope. Milliseconds are stored in native units
// so project files and the sampler editor remain readable. A short default
// attack/release protects arbitrary (non-zero-crossing) slice boundaries
// without materially softening drum transients.
struct SamplerEnvelope {
    double attackMilliseconds = 0.1;
    double decayMilliseconds = 0.0;
    float sustain = 1.0f;
    double releaseMilliseconds = 5.0;

    bool valid() const noexcept;
};

// Assets are decoded and sanitized away from the render thread, then shared
// with the node as immutable channel data. An empty right channel represents a
// mono source and is duplicated to stereo at render time.
struct StereoSampleAsset {
    double sampleRate = 48000.0;
    std::vector<float> left;
    std::vector<float> right;

    bool valid() const noexcept;
    uint32_t frameCount() const noexcept;
};

// Control-side waveform and onset data. This is deliberately separate from
// StereoSampleAsset: the render node only retains decoded samples, while an
// editor can publish or discard immutable derived analysis independently.
struct SamplePeak {
    float minimum = 0.0f;
    float maximum = 0.0f;
};

struct SampleTransient {
    uint32_t frame = 0u;
    float strength = 0.0f;
};

struct StereoSampleAnalysis {
    uint32_t sourceFrameCount = 0u;
    uint32_t peakStrideFrames = 1u;
    std::vector<SamplePeak> peaks;
    std::vector<SampleTransient> transients;

    bool validFor(const StereoSampleAsset& asset) const noexcept;
};

struct SampleAnalysisSettings {
    // A fixed upper bound keeps waveform drawing independent of source length.
    std::size_t maximumPeakCount = 4096u;
    float minimumTransientLevel = 0.06f;
    float transientSensitivity = 2.25f;
    double minimumTransientSpacingSeconds = 0.025;
    double transientLookaheadSeconds = 0.002;
};

struct SampleSlice {
    uint32_t startFrame = 0u;
    uint32_t endFrame = 0u; // exclusive
    float gain = 1.0f;
    bool reverse = false;
};

// All of these helpers run on the control/background side. They may allocate,
// but never touch an InstrumentNode and are deterministic for identical input.
StereoSampleAnalysis analyzeStereoSample(const StereoSampleAsset& asset,
    const SampleAnalysisSettings& settings = {});
uint32_t nearestStereoZeroFrame(const StereoSampleAsset& asset,
    uint32_t frame, uint32_t searchRadiusFrames) noexcept;
std::vector<SampleSlice> makeTransientSampleSlices(
    const StereoSampleAsset& asset, const StereoSampleAnalysis& analysis,
    std::size_t maximumSliceCount = kMaximumSamplerSlices,
    uint32_t zeroCrossingRadiusFrames = 0u);

// Marker indices name slice starts. The outer start/end remain asset bounds;
// internal markers can be split, dragged, or removed without destructive audio
// edits. Marker moves maintain a contiguous, non-empty slice table.
bool addSampleSliceMarker(SampleSlice* slices, std::size_t& count,
    std::size_t capacity, uint32_t frame) noexcept;
bool moveSampleSliceMarker(SampleSlice* slices, std::size_t count,
    std::size_t markerIndex, uint32_t frame) noexcept;
bool deleteSampleSliceMarker(SampleSlice* slices, std::size_t& count,
    std::size_t markerIndex) noexcept;

// Tracker authoring boundary. S000..S127 identifies a slice independently of
// its eventual pitch/rate semantics; the current sampler maps it through the
// instrument's base note when playback is authored as MIDI-style notes. Slice
// indices are deliberately decimal rather than tracker-style hexadecimal.
bool parseSamplerSliceToken(std::string_view text,
    uint8_t& sliceIndex) noexcept;
std::array<char, 5u> formatSamplerSliceToken(uint8_t sliceIndex) noexcept;
bool samplerNoteForSlice(uint8_t baseNote, uint8_t sliceIndex,
    uint8_t& note) noexcept;

// First native sampler boundary: one immutable stereo asset, a fixed slice
// table, note-to-slice mapping, and bounded one-shot voices. All mutation is
// control-thread-only while stopped; render performs no allocation or I/O.
class StereoSliceSamplerNode final : public InstrumentNode {
public:
    bool setAsset(std::shared_ptr<const StereoSampleAsset> asset) noexcept;
    bool clearAsset() noexcept;
    std::shared_ptr<const StereoSampleAsset> asset() const noexcept;
    bool setSlices(const SampleSlice* slices, std::size_t count) noexcept;
    std::size_t sliceCount() const noexcept;
    SampleSlice slice(std::size_t index) const noexcept;
    bool setBaseNote(uint8_t note) noexcept;
    uint8_t baseNote() const noexcept;
    bool setEnvelope(const SamplerEnvelope& envelope) noexcept;
    SamplerEnvelope envelope() const noexcept;

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
    struct Voice {
        enum class EnvelopeStage : uint8_t {
            Attack,
            Decay,
            Sustain,
            Release,
        };

        uint64_t noteId = 0u;
        uint64_t age = 0u;
        double position = 0.0;
        double increment = 1.0;
        uint32_t sliceIndex = 0u;
        float level = 0.0f;
        float envelopeLevel = 0.0f;
        float releaseStartLevel = 0.0f;
        float sustainLevel = 1.0f;
        uint32_t attackFrames = 0u;
        uint32_t decayFrames = 0u;
        uint32_t releaseFrames = 0u;
        uint32_t naturalFadeFrames = 2u;
        uint32_t envelopeFrame = 0u;
        EnvelopeStage envelopeStage = EnvelopeStage::Sustain;
        bool active = false;
    };

    void handleEvent(const InstrumentRenderEvent& event) noexcept;
    void startVoice(const InstrumentRenderEvent& event) noexcept;
    void releaseVoice(uint64_t noteId) noexcept;
    void advanceEnvelope(Voice& voice) noexcept;
    float naturalTailEnvelope(const Voice& voice,
        const SampleSlice& slice) const noexcept;

    std::shared_ptr<const StereoSampleAsset> asset_;
    std::array<SampleSlice, kMaximumSamplerSlices> slices_ {};
    std::array<Voice, kMaximumSamplerVoices> voices_ {};
    std::size_t sliceCount_ = 0u;
    double hostSampleRate_ = 48000.0;
    uint64_t voiceAge_ = 0u;
    uint32_t naturalFadeFrames_ = 96u;
    uint8_t baseNote_ = 36u;
    SamplerEnvelope envelope_ {};
    bool prepared_ = false;
    bool processing_ = false;
};

} // namespace s3g::tracker::audio
