#include "s3g/tracker/audio/stereo_slice_sampler_node.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace s3g::tracker::audio {
namespace {

bool finiteSamples(const std::vector<float>& samples) noexcept
{
    return std::all_of(samples.begin(), samples.end(),
        [](float sample) { return std::isfinite(sample); });
}

float interpolated(const std::vector<float>& samples, double position,
    uint32_t start, uint32_t end) noexcept
{
    if (samples.empty() || start >= end) return 0.0f;
    const double bounded = std::clamp(position, static_cast<double>(start),
        static_cast<double>(end - 1u));
    const auto first = static_cast<uint32_t>(bounded);
    const auto second = std::min(first + 1u, end - 1u);
    const float fraction = static_cast<float>(
        bounded - static_cast<double>(first));
    return samples[first] + (samples[second] - samples[first]) * fraction;
}

float stereoMagnitude(const StereoSampleAsset& asset, uint32_t frame) noexcept
{
    const float left = std::abs(asset.left[frame]);
    const float right = asset.right.empty()
        ? left : std::abs(asset.right[frame]);
    return std::max(left, right);
}

bool validAssetShape(const StereoSampleAsset& asset) noexcept
{
    return asset.sampleRate > 0.0 && std::isfinite(asset.sampleRate)
        && !asset.left.empty()
        && asset.left.size() <= std::numeric_limits<uint32_t>::max()
        && (asset.right.empty() || asset.right.size() == asset.left.size());
}

uint32_t envelopeFrameCount(double milliseconds,
    double sampleRate) noexcept
{
    if (!(milliseconds > 0.0)) return 0u;
    const double frames = std::round(milliseconds * 0.001 * sampleRate);
    return static_cast<uint32_t>(std::clamp<double>(frames, 1.0,
        std::numeric_limits<uint32_t>::max()));
}

} // namespace

bool SamplerEnvelope::valid() const noexcept
{
    return std::isfinite(attackMilliseconds)
        && attackMilliseconds >= 0.0
        && attackMilliseconds <= kMaximumSamplerEnvelopeMilliseconds
        && std::isfinite(decayMilliseconds)
        && decayMilliseconds >= 0.0
        && decayMilliseconds <= kMaximumSamplerEnvelopeMilliseconds
        && std::isfinite(sustain) && sustain >= 0.0f && sustain <= 1.0f
        && std::isfinite(releaseMilliseconds)
        && releaseMilliseconds >= 0.0
        && releaseMilliseconds <= kMaximumSamplerEnvelopeMilliseconds;
}

bool StereoSampleAsset::valid() const noexcept
{
    return sampleRate > 0.0 && std::isfinite(sampleRate) && !left.empty()
        && left.size() <= std::numeric_limits<uint32_t>::max()
        && (right.empty() || right.size() == left.size())
        && finiteSamples(left) && finiteSamples(right);
}

uint32_t StereoSampleAsset::frameCount() const noexcept
{
    return left.size() <= std::numeric_limits<uint32_t>::max()
        ? static_cast<uint32_t>(left.size()) : 0u;
}

bool StereoSampleAnalysis::validFor(
    const StereoSampleAsset& asset) const noexcept
{
    if (!validAssetShape(asset) || sourceFrameCount != asset.frameCount()
        || peakStrideFrames == 0u || peaks.empty()) return false;
    const std::size_t expectedPeakCount = static_cast<std::size_t>(
        (static_cast<uint64_t>(sourceFrameCount) + peakStrideFrames - 1u)
            / peakStrideFrames);
    if (peaks.size() != expectedPeakCount) return false;
    for (const auto& peak : peaks) {
        if (!std::isfinite(peak.minimum) || !std::isfinite(peak.maximum)
            || peak.minimum > peak.maximum) return false;
    }
    uint32_t previousFrame = 0u;
    bool first = true;
    for (const auto& transient : transients) {
        if (transient.frame >= sourceFrameCount
            || !std::isfinite(transient.strength)
            || transient.strength < 0.0f
            || (!first && transient.frame <= previousFrame)) return false;
        previousFrame = transient.frame;
        first = false;
    }
    return true;
}

StereoSampleAnalysis analyzeStereoSample(const StereoSampleAsset& asset,
    const SampleAnalysisSettings& settings)
{
    StereoSampleAnalysis result;
    if (!asset.valid() || settings.maximumPeakCount == 0u
        || !std::isfinite(settings.minimumTransientLevel)
        || settings.minimumTransientLevel < 0.0f
        || !std::isfinite(settings.transientSensitivity)
        || settings.transientSensitivity < 1.0f
        || !std::isfinite(settings.minimumTransientSpacingSeconds)
        || settings.minimumTransientSpacingSeconds < 0.0
        || !std::isfinite(settings.transientLookaheadSeconds)
        || settings.transientLookaheadSeconds < 0.0) return result;

    const uint32_t frames = asset.frameCount();
    result.sourceFrameCount = frames;
    const uint64_t requestedPeaks = std::min<uint64_t>(
        settings.maximumPeakCount, frames);
    result.peakStrideFrames = static_cast<uint32_t>(std::max<uint64_t>(1u,
        (static_cast<uint64_t>(frames) + requestedPeaks - 1u)
            / requestedPeaks));
    const std::size_t peakCount = static_cast<std::size_t>(
        (static_cast<uint64_t>(frames) + result.peakStrideFrames - 1u)
            / result.peakStrideFrames);
    result.peaks.resize(peakCount);
    for (std::size_t index = 0u; index < peakCount; ++index) {
        const uint32_t start = static_cast<uint32_t>(
            static_cast<uint64_t>(index) * result.peakStrideFrames);
        const uint32_t end = static_cast<uint32_t>(std::min<uint64_t>(frames,
            static_cast<uint64_t>(start) + result.peakStrideFrames));
        float minimum = std::numeric_limits<float>::max();
        float maximum = std::numeric_limits<float>::lowest();
        for (uint32_t frame = start; frame < end; ++frame) {
            minimum = std::min(minimum, asset.left[frame]);
            maximum = std::max(maximum, asset.left[frame]);
            if (!asset.right.empty()) {
                minimum = std::min(minimum, asset.right[frame]);
                maximum = std::max(maximum, asset.right[frame]);
            }
        }
        result.peaks[index] = { minimum, maximum };
    }

    const auto boundedFrameDuration = [&](double seconds, uint32_t minimum) {
        const double framesForDuration = std::round(asset.sampleRate * seconds);
        return static_cast<uint32_t>(std::clamp<double>(framesForDuration,
            minimum, std::numeric_limits<uint32_t>::max()));
    };
    const uint32_t minimumSpacing = boundedFrameDuration(
        settings.minimumTransientSpacingSeconds, 1u);
    const uint32_t lookahead = boundedFrameDuration(
        settings.transientLookaheadSeconds, 0u);
    const double attackCoefficient = 1.0 - std::exp(
        -1.0 / std::max(1.0, asset.sampleRate * 0.001));
    const double releaseCoefficient = 1.0 - std::exp(
        -1.0 / std::max(1.0, asset.sampleRate * 0.050));
    float envelope = 0.0f;
    float previous = 0.0f;
    uint64_t nextAllowedFrame = 0u;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        const float magnitude = stereoMagnitude(asset, frame);
        const float threshold = std::max(settings.minimumTransientLevel,
            envelope * settings.transientSensitivity);
        if (static_cast<uint64_t>(frame) >= nextAllowedFrame
            && magnitude >= threshold && magnitude > previous) {
            const uint32_t searchEnd = static_cast<uint32_t>(
                std::min<uint64_t>(frames, static_cast<uint64_t>(frame)
                    + lookahead + 1u));
            uint32_t peakFrame = frame;
            float peakMagnitude = magnitude;
            for (uint32_t candidate = frame + 1u; candidate < searchEnd;
                 ++candidate) {
                const float candidateMagnitude = stereoMagnitude(
                    asset, candidate);
                if (candidateMagnitude > peakMagnitude) {
                    peakMagnitude = candidateMagnitude;
                    peakFrame = candidate;
                }
            }
            const float denominator = std::max(0.000001f, threshold);
            result.transients.push_back({ peakFrame, std::min(1000000.0f,
                peakMagnitude / denominator) });
            envelope = std::max(envelope, peakMagnitude);
            nextAllowedFrame = static_cast<uint64_t>(peakFrame)
                + minimumSpacing;
        }
        const double coefficient = magnitude > envelope
            ? attackCoefficient : releaseCoefficient;
        envelope += static_cast<float>((magnitude - envelope)
            * coefficient);
        previous = magnitude;
    }
    return result;
}

uint32_t nearestStereoZeroFrame(const StereoSampleAsset& asset,
    uint32_t frame, uint32_t searchRadiusFrames) noexcept
{
    // Published assets have already had their full finite-sample validation.
    // Keep marker dragging bounded to the requested search radius.
    if (!validAssetShape(asset)) return 0u;
    const uint32_t lastFrame = asset.frameCount() - 1u;
    frame = std::min(frame, lastFrame);
    const uint32_t start = frame > searchRadiusFrames
        ? frame - searchRadiusFrames : 0u;
    const uint32_t end = static_cast<uint32_t>(std::min<uint64_t>(lastFrame,
        static_cast<uint64_t>(frame) + searchRadiusFrames));
    uint32_t bestFrame = frame;
    float bestMagnitude = stereoMagnitude(asset, frame);
    uint32_t bestDistance = 0u;
    for (uint32_t candidate = start; candidate <= end; ++candidate) {
        const float magnitude = stereoMagnitude(asset, candidate);
        const uint32_t distance = candidate > frame
            ? candidate - frame : frame - candidate;
        if (magnitude < bestMagnitude
            || (magnitude == bestMagnitude && distance < bestDistance)
            || (magnitude == bestMagnitude && distance == bestDistance
                && candidate < bestFrame)) {
            bestFrame = candidate;
            bestMagnitude = magnitude;
            bestDistance = distance;
        }
    }
    return bestFrame;
}

std::vector<SampleSlice> makeTransientSampleSlices(
    const StereoSampleAsset& asset, const StereoSampleAnalysis& analysis,
    std::size_t maximumSliceCount, uint32_t zeroCrossingRadiusFrames)
{
    std::vector<SampleSlice> result;
    if (!analysis.validFor(asset) || maximumSliceCount == 0u) return result;
    maximumSliceCount = std::min(maximumSliceCount, kMaximumSamplerSlices);
    std::vector<uint32_t> markers;
    markers.reserve(std::min(maximumSliceCount, analysis.transients.size() + 1u));
    markers.push_back(0u);
    const uint32_t frames = asset.frameCount();
    for (const auto& transient : analysis.transients) {
        uint32_t marker = transient.frame;
        if (zeroCrossingRadiusFrames != 0u)
            marker = nearestStereoZeroFrame(asset, marker,
                zeroCrossingRadiusFrames);
        if (marker > 0u && marker < frames) markers.push_back(marker);
    }
    std::sort(markers.begin() + 1, markers.end());
    markers.erase(std::unique(markers.begin(), markers.end()), markers.end());
    if (markers.size() > maximumSliceCount)
        markers.resize(maximumSliceCount);
    result.reserve(markers.size());
    for (std::size_t index = 0u; index < markers.size(); ++index) {
        const uint32_t end = index + 1u < markers.size()
            ? markers[index + 1u] : frames;
        if (markers[index] < end)
            result.push_back({ markers[index], end, 1.0f, false });
    }
    return result;
}

bool addSampleSliceMarker(SampleSlice* slices, std::size_t& count,
    std::size_t capacity, uint32_t frame) noexcept
{
    if (!slices || count == 0u || count >= capacity) return false;
    std::size_t selected = count;
    for (std::size_t index = 0u; index < count; ++index) {
        if (frame > slices[index].startFrame
            && frame < slices[index].endFrame) {
            selected = index;
            break;
        }
    }
    if (selected == count) return false;
    for (std::size_t index = count; index > selected + 1u; --index)
        slices[index] = slices[index - 1u];
    const SampleSlice right = { frame, slices[selected].endFrame,
        slices[selected].gain, slices[selected].reverse };
    slices[selected].endFrame = frame;
    slices[selected + 1u] = right;
    ++count;
    return true;
}

bool moveSampleSliceMarker(SampleSlice* slices, std::size_t count,
    std::size_t markerIndex, uint32_t frame) noexcept
{
    if (!slices || markerIndex == 0u || markerIndex >= count) return false;
    auto& left = slices[markerIndex - 1u];
    auto& right = slices[markerIndex];
    if (frame <= left.startFrame || frame >= right.endFrame) return false;
    left.endFrame = frame;
    right.startFrame = frame;
    return true;
}

bool deleteSampleSliceMarker(SampleSlice* slices, std::size_t& count,
    std::size_t markerIndex) noexcept
{
    if (!slices || markerIndex == 0u || markerIndex >= count) return false;
    slices[markerIndex - 1u].endFrame = slices[markerIndex].endFrame;
    for (std::size_t index = markerIndex; index + 1u < count; ++index)
        slices[index] = slices[index + 1u];
    --count;
    slices[count] = {};
    return true;
}

bool parseSamplerSliceToken(std::string_view text,
    uint8_t& sliceIndex) noexcept
{
    if (text.size() < 2u || text.size() > 4u
        || (text[0u] != 'S' && text[0u] != 's'))
        return false;
    uint32_t value = 0u;
    for (std::size_t index = 1u; index < text.size(); ++index) {
        const char digit = text[index];
        if (digit < '0' || digit > '9') return false;
        value = value * 10u + static_cast<uint32_t>(digit - '0');
    }
    if (value >= kMaximumSamplerSlices) return false;
    sliceIndex = static_cast<uint8_t>(value);
    return true;
}

std::array<char, 5u> formatSamplerSliceToken(uint8_t sliceIndex) noexcept
{
    sliceIndex = std::min<uint8_t>(sliceIndex,
        static_cast<uint8_t>(kMaximumSamplerSlices - 1u));
    return {{ 'S', static_cast<char>('0' + sliceIndex / 100u),
        static_cast<char>('0' + (sliceIndex / 10u) % 10u),
        static_cast<char>('0' + sliceIndex % 10u), '\0' }};
}

bool samplerNoteForSlice(uint8_t baseNote, uint8_t sliceIndex,
    uint8_t& note) noexcept
{
    const uint16_t mapped = static_cast<uint16_t>(baseNote)
        + static_cast<uint16_t>(sliceIndex);
    if (mapped > 127u) return false;
    note = static_cast<uint8_t>(mapped);
    return true;
}

bool StereoSliceSamplerNode::setAsset(
    std::shared_ptr<const StereoSampleAsset> asset) noexcept
{
    if (processing_ || !asset || !asset->valid()) return false;
    asset_ = std::move(asset);
    slices_ = {};
    slices_[0u] = { 0u, asset_->frameCount(), 1.0f, false };
    sliceCount_ = 1u;
    reset();
    return true;
}

bool StereoSliceSamplerNode::clearAsset() noexcept
{
    if (processing_) return false;
    asset_.reset();
    slices_ = {};
    sliceCount_ = 0u;
    reset();
    return true;
}


std::shared_ptr<const StereoSampleAsset>
StereoSliceSamplerNode::asset() const noexcept
{
    return asset_;
}

bool StereoSliceSamplerNode::setSlices(
    const SampleSlice* slices, std::size_t count) noexcept
{
    if (processing_ || !asset_ || !slices || count == 0u
        || count > slices_.size()) return false;
    for (std::size_t index = 0u; index < count; ++index) {
        const auto& candidate = slices[index];
        if (candidate.startFrame >= candidate.endFrame
            || candidate.endFrame > asset_->frameCount()
            || !std::isfinite(candidate.gain)
            || candidate.gain < 0.0f || candidate.gain > 2.0f) return false;
    }
    std::copy_n(slices, count, slices_.begin());
    std::fill(slices_.begin() + static_cast<std::ptrdiff_t>(count),
        slices_.end(), SampleSlice {});
    sliceCount_ = count;
    reset();
    return true;
}

std::size_t StereoSliceSamplerNode::sliceCount() const noexcept
{
    return sliceCount_;
}

SampleSlice StereoSliceSamplerNode::slice(std::size_t index) const noexcept
{
    return index < sliceCount_ ? slices_[index] : SampleSlice {};
}

bool StereoSliceSamplerNode::setBaseNote(uint8_t note) noexcept
{
    if (processing_ || note > 127u) return false;
    baseNote_ = note;
    return true;
}

uint8_t StereoSliceSamplerNode::baseNote() const noexcept
{
    return baseNote_;
}

bool StereoSliceSamplerNode::setEnvelope(
    const SamplerEnvelope& envelope) noexcept
{
    if (processing_ || !envelope.valid()) return false;
    envelope_ = envelope;
    return true;
}

SamplerEnvelope StereoSliceSamplerNode::envelope() const noexcept
{
    return envelope_;
}

bool StereoSliceSamplerNode::prepare(const AudioRenderSpec& spec)
{
    if (!(spec.sampleRate > 0.0) || !std::isfinite(spec.sampleRate)
        || spec.minimumFrames == 0u || spec.maximumFrames == 0u
        || spec.minimumFrames > spec.maximumFrames) return false;
    hostSampleRate_ = spec.sampleRate;
    naturalFadeFrames_ = std::max<uint32_t>(2u,
        static_cast<uint32_t>(std::lround(hostSampleRate_ * 0.002)));
    prepared_ = true;
    reset();
    return true;
}

void StereoSliceSamplerNode::unprepare()
{
    processing_ = false;
    prepared_ = false;
    reset();
}

bool StereoSliceSamplerNode::startProcessing() noexcept
{
    // The immutable asset was fully validated by setAsset() on the control
    // thread. Do not rescan arbitrarily large sample vectors here: this
    // lifecycle call belongs to the render thread.
    // Empty rack slots are valid silent instruments. Assets can be installed
    // later while the containing graph is safely stopped.
    processing_ = prepared_;
    return processing_;
}

void StereoSliceSamplerNode::stopProcessing() noexcept
{
    processing_ = false;
}

bool StereoSliceSamplerNode::isProcessing() const noexcept
{
    return processing_;
}

void StereoSliceSamplerNode::reset() noexcept
{
    voices_ = {};
    voiceAge_ = 0u;
}

AudioLayout StereoSliceSamplerNode::outputLayout() const noexcept
{
    return AudioLayout::Stereo;
}

uint32_t StereoSliceSamplerNode::latencyFrames() const noexcept
{
    return 0u;
}

void StereoSliceSamplerNode::startVoice(
    const InstrumentRenderEvent& event) noexcept
{
    const int mapped = static_cast<int>(event.key)
        - static_cast<int>(baseNote_);
    if (!asset_ || mapped < 0
        || static_cast<std::size_t>(mapped) >= sliceCount_) return;

    std::size_t selected = voices_.size();
    for (std::size_t index = 0u; index < voices_.size(); ++index) {
        if (!voices_[index].active) { selected = index; break; }
    }
    if (selected == voices_.size()) {
        selected = 0u;
        for (std::size_t index = 1u; index < voices_.size(); ++index) {
            if (voices_[index].age < voices_[selected].age) selected = index;
        }
    }

    const auto sliceIndex = static_cast<std::size_t>(mapped);
    const auto& slice = slices_[sliceIndex];
    auto& voice = voices_[selected];
    voice.noteId = event.noteId;
    voice.age = ++voiceAge_;
    voice.position = slice.reverse
        ? static_cast<double>(slice.endFrame - 1u)
        : static_cast<double>(slice.startFrame);
    const double rate = asset_->sampleRate / hostSampleRate_;
    voice.increment = slice.reverse ? -rate : rate;
    voice.sliceIndex = static_cast<uint32_t>(sliceIndex);
    voice.level = static_cast<float>(std::clamp(event.value, 0.0, 1.0))
        * slice.gain;
    voice.attackFrames = envelopeFrameCount(
        envelope_.attackMilliseconds, hostSampleRate_);
    voice.decayFrames = envelopeFrameCount(
        envelope_.decayMilliseconds, hostSampleRate_);
    voice.releaseFrames = envelopeFrameCount(
        envelope_.releaseMilliseconds, hostSampleRate_);
    const double outputLength = std::ceil(
        static_cast<double>(slice.endFrame - slice.startFrame)
            / std::max(rate, std::numeric_limits<double>::min()));
    const uint32_t boundedOutputLength = static_cast<uint32_t>(
        std::clamp<double>(outputLength, 1.0,
            std::numeric_limits<uint32_t>::max()));
    const uint32_t halfSlice = std::max<uint32_t>(2u,
        boundedOutputLength / 2u);
    voice.naturalFadeFrames = std::min(naturalFadeFrames_, halfSlice);
    voice.sustainLevel = envelope_.sustain;
    voice.envelopeFrame = 0u;
    voice.releaseStartLevel = 0.0f;
    if (voice.attackFrames != 0u) {
        voice.envelopeStage = Voice::EnvelopeStage::Attack;
        voice.envelopeLevel = 0.0f;
    } else if (voice.decayFrames != 0u) {
        voice.envelopeStage = Voice::EnvelopeStage::Decay;
        voice.envelopeLevel = 1.0f;
    } else {
        voice.envelopeStage = Voice::EnvelopeStage::Sustain;
        voice.envelopeLevel = voice.sustainLevel;
    }
    voice.active = voice.level > 0.0f;
}

void StereoSliceSamplerNode::releaseVoice(uint64_t noteId) noexcept
{
    for (auto& voice : voices_) {
        if (!voice.active || (noteId != 0u && voice.noteId != noteId))
            continue;
        if (voice.envelopeStage == Voice::EnvelopeStage::Release) continue;
        if (voice.releaseFrames == 0u || voice.envelopeLevel <= 0.0f) {
            voice.envelopeLevel = 0.0f;
            voice.active = false;
            continue;
        }
        voice.envelopeStage = Voice::EnvelopeStage::Release;
        voice.envelopeFrame = 0u;
        voice.releaseStartLevel = voice.envelopeLevel;
    }
}

void StereoSliceSamplerNode::advanceEnvelope(Voice& voice) noexcept
{
    switch (voice.envelopeStage) {
    case Voice::EnvelopeStage::Attack:
        ++voice.envelopeFrame;
        if (voice.envelopeFrame >= voice.attackFrames) {
            voice.envelopeFrame = 0u;
            if (voice.decayFrames != 0u) {
                voice.envelopeStage = Voice::EnvelopeStage::Decay;
                voice.envelopeLevel = 1.0f;
            } else {
                voice.envelopeStage = Voice::EnvelopeStage::Sustain;
                voice.envelopeLevel = voice.sustainLevel;
            }
        } else {
            voice.envelopeLevel = static_cast<float>(voice.envelopeFrame)
                / static_cast<float>(voice.attackFrames);
        }
        break;
    case Voice::EnvelopeStage::Decay:
        ++voice.envelopeFrame;
        if (voice.envelopeFrame >= voice.decayFrames) {
            voice.envelopeFrame = 0u;
            voice.envelopeStage = Voice::EnvelopeStage::Sustain;
            voice.envelopeLevel = voice.sustainLevel;
        } else {
            const float phase = static_cast<float>(voice.envelopeFrame)
                / static_cast<float>(voice.decayFrames);
            voice.envelopeLevel = 1.0f
                + (voice.sustainLevel - 1.0f) * phase;
        }
        break;
    case Voice::EnvelopeStage::Sustain:
        voice.envelopeLevel = voice.sustainLevel;
        break;
    case Voice::EnvelopeStage::Release:
        ++voice.envelopeFrame;
        if (voice.envelopeFrame >= voice.releaseFrames) {
            voice.envelopeLevel = 0.0f;
            voice.active = false;
        } else {
            voice.envelopeLevel = voice.releaseStartLevel
                * (1.0f - static_cast<float>(voice.envelopeFrame)
                    / static_cast<float>(voice.releaseFrames));
        }
        break;
    }
}

float StereoSliceSamplerNode::naturalTailEnvelope(const Voice& voice,
    const SampleSlice& slice) const noexcept
{
    const double rate = std::abs(voice.increment);
    if (!(rate > 0.0)) return 0.0f;
    double samplesRemaining = 1.0;
    if (voice.increment > 0.0) {
        const double distance = std::max(0.0,
            static_cast<double>(slice.endFrame) - voice.position);
        samplesRemaining = std::ceil(distance / rate);
    } else {
        const double distance = std::max(0.0, voice.position
            - static_cast<double>(slice.startFrame));
        samplesRemaining = std::floor(distance / rate) + 1.0;
    }
    if (!std::isfinite(samplesRemaining)
        || samplesRemaining >= static_cast<double>(
            std::numeric_limits<uint64_t>::max()))
        return 1.0f;
    const uint64_t remaining = static_cast<uint64_t>(std::max(1.0,
        samplesRemaining));
    if (remaining >= voice.naturalFadeFrames) return 1.0f;
    return static_cast<float>(remaining - 1u)
        / static_cast<float>(voice.naturalFadeFrames - 1u);
}

void StereoSliceSamplerNode::handleEvent(
    const InstrumentRenderEvent& event) noexcept
{
    switch (event.kind) {
    case InstrumentEventKind::NoteOn:
        startVoice(event);
        break;
    case InstrumentEventKind::NoteOff:
        releaseVoice(event.noteId);
        break;
    case InstrumentEventKind::Choke:
        releaseVoice(0u);
        break;
    case InstrumentEventKind::ParameterValue:
        break;
    }
}

void StereoSliceSamplerNode::render(const InstrumentRenderEvent* events,
    std::size_t eventCount, const PlanarAudioBlock& output) noexcept
{
    output.clear();
    if (!processing_ || !asset_ || !output.valid()
        || output.channelCount < 2u) return;
    if (!events) eventCount = 0u;
    std::size_t eventIndex = 0u;
    for (uint32_t frame = 0u; frame < output.frameCount; ++frame) {
        while (eventIndex < eventCount
            && events[eventIndex].frameOffset <= frame) {
            handleEvent(events[eventIndex++]);
        }
        float left = 0.0f;
        float right = 0.0f;
        for (auto& voice : voices_) {
            if (!voice.active) continue;
            const auto& slice = slices_[voice.sliceIndex];
            const float envelope = voice.envelopeLevel
                * naturalTailEnvelope(voice, slice);
            left += interpolated(asset_->left, voice.position,
                slice.startFrame, slice.endFrame) * voice.level * envelope;
            const auto& rightSamples = asset_->right.empty()
                ? asset_->left : asset_->right;
            right += interpolated(rightSamples, voice.position,
                slice.startFrame, slice.endFrame) * voice.level * envelope;

            voice.position += voice.increment;
            advanceEnvelope(voice);
            if (voice.position < static_cast<double>(slice.startFrame)
                || voice.position >= static_cast<double>(slice.endFrame))
                voice.active = false;
        }
        output.channels[0u][frame] = std::clamp(left, -1.0f, 1.0f);
        output.channels[1u][frame] = std::clamp(right, -1.0f, 1.0f);
    }
}

} // namespace s3g::tracker::audio
