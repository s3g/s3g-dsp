#pragma once

#include "s3g_ambi_effect_dj_filter.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#if defined(__APPLE__)
#define S3G_HAS_RESPONSE_TRACE_FFT 1
#include <Accelerate/Accelerate.h>
#else
#define S3G_HAS_RESPONSE_TRACE_FFT 0
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kResponseTracePartitionSize = 1024u;
constexpr float kResponseTraceMaximumSeconds = 1.5f;
constexpr uint32_t kResponseTraceMaximumFrames = 262144u;

struct AmbiEffectResponseTraceParams {
    uint32_t order = 7u;
    AmbiEffectBody body = AmbiEffectBody::Auto;
    AmbiEffectTopology topology = AmbiEffectTopology::Local;
    uint32_t responseEnabled = 0u;
    float captureSeconds = 0.50f;
    float responseGainDb = -6.0f;
    float tone = 0.72f;
    float topologyAmount = 0.65f;
    float roamingRateHz = 0.08f;
    float mix = 0.50f;
    float outputGainDb = 0.0f;
    float maskAmount = 0.0f;
    float maskAzimuthDeg = 0.0f;
    float maskElevationDeg = 0.0f;
    float maskWidth = 0.35f;
    float maskCurve = 0.5f;
    float maskDry = 1.0f;
};

inline AmbiEffectResponseTraceParams sanitizeAmbiEffectResponseTraceParams(
    AmbiEffectResponseTraceParams params)
{
    const AmbiEffectResponseTraceParams defaults {};
    const auto finiteOr = [](float value, float fallback) {
        return std::isfinite(value) ? value : fallback;
    };
    params.order = std::clamp<uint32_t>(params.order, 1u,
        kAmbiEffectDjFilterMaxOrder);
    params.body = static_cast<AmbiEffectBody>(std::min<uint32_t>(
        static_cast<uint32_t>(params.body), 5u));
    params.topology = static_cast<AmbiEffectTopology>(std::min<uint32_t>(
        static_cast<uint32_t>(params.topology), 3u));
    params.responseEnabled = params.responseEnabled ? 1u : 0u;
    params.captureSeconds = finiteOr(
        params.captureSeconds, defaults.captureSeconds);
    params.responseGainDb = finiteOr(
        params.responseGainDb, defaults.responseGainDb);
    params.tone = finiteOr(params.tone, defaults.tone);
    params.topologyAmount = finiteOr(
        params.topologyAmount, defaults.topologyAmount);
    params.roamingRateHz = finiteOr(
        params.roamingRateHz, defaults.roamingRateHz);
    params.mix = finiteOr(params.mix, defaults.mix);
    params.outputGainDb = finiteOr(
        params.outputGainDb, defaults.outputGainDb);
    params.maskAmount = finiteOr(params.maskAmount, defaults.maskAmount);
    params.maskAzimuthDeg = finiteOr(
        params.maskAzimuthDeg, defaults.maskAzimuthDeg);
    params.maskElevationDeg = finiteOr(
        params.maskElevationDeg, defaults.maskElevationDeg);
    params.maskWidth = finiteOr(params.maskWidth, defaults.maskWidth);
    params.maskCurve = finiteOr(params.maskCurve, defaults.maskCurve);
    params.maskDry = finiteOr(params.maskDry, defaults.maskDry);
    params.captureSeconds = clamp(params.captureSeconds, 0.05f,
        kResponseTraceMaximumSeconds);
    params.responseGainDb = clamp(params.responseGainDb, -36.0f, 12.0f);
    params.tone = clamp(params.tone, 0.0f, 1.0f);
    params.topologyAmount = clamp(params.topologyAmount, 0.0f, 1.0f);
    params.roamingRateHz = clamp(params.roamingRateHz, 0.005f, 2.0f);
    params.mix = clamp(params.mix, 0.0f, 1.0f);
    params.outputGainDb = clamp(params.outputGainDb, -60.0f, 12.0f);
    params.maskAmount = clamp(params.maskAmount, 0.0f, 1.0f);
    params.maskAzimuthDeg = clamp(params.maskAzimuthDeg, -180.0f, 180.0f);
    params.maskElevationDeg = clamp(params.maskElevationDeg, -90.0f, 90.0f);
    params.maskWidth = clamp(params.maskWidth, 0.0f, 1.0f);
    params.maskCurve = clamp(params.maskCurve, 0.0f, 1.0f);
    params.maskDry = clamp(params.maskDry, 0.0f, 1.0f);
    return params;
}

namespace response_trace_detail {

class PartitionedConvolutionBank {
public:
    PartitionedConvolutionBank() = default;
    ~PartitionedConvolutionBank() { release(); }
    PartitionedConvolutionBank(const PartitionedConvolutionBank&) = delete;
    PartitionedConvolutionBank& operator=(const PartitionedConvolutionBank&) = delete;

    bool prepare(uint32_t maximumKernelFrames, uint32_t nodeCount)
    {
#if !S3G_HAS_RESPONSE_TRACE_FFT
        (void)maximumKernelFrames;
        (void)nodeCount;
        return false;
#else
        release();
        nodeCount_ = std::min<uint32_t>(nodeCount,
            kAmbiEffectDjFilterMaxPickups);
        partitionCount_ = std::max<uint32_t>(1u,
            (maximumKernelFrames + kResponseTracePartitionSize - 1u)
                / kResponseTracePartitionSize);
        fftSize_ = kResponseTracePartitionSize * 2u;
        halfSize_ = fftSize_ / 2u;
        setup_ = vDSP_create_fftsetup(11u, kFFTRadix2);
        if (!setup_) {
            release();
            return false;
        }
        const size_t kernelBins = static_cast<size_t>(nodeCount_)
            * partitionCount_ * halfSize_;
        kernelReal_.assign(kernelBins, 0.0f);
        kernelImag_.assign(kernelBins, 0.0f);
        kernelFrame_.assign(fftSize_, 0.0f);
        kernelSplitReal_.assign(halfSize_, 0.0f);
        kernelSplitImag_.assign(halfSize_, 0.0f);
        for (uint32_t node = 0u; node < nodeCount_; ++node) {
            Node& state = nodes_[node];
            state.overlap.assign(kResponseTracePartitionSize, 0.0f);
            state.input.assign(kResponseTracePartitionSize, 0.0f);
            state.output.assign(kResponseTracePartitionSize, 0.0f);
            state.frame.assign(fftSize_, 0.0f);
            state.accumulatorReal.assign(halfSize_, 0.0f);
            state.accumulatorImag.assign(halfSize_, 0.0f);
            state.historyReal.assign(
                static_cast<size_t>(partitionCount_) * halfSize_, 0.0f);
            state.historyImag.assign(
                static_cast<size_t>(partitionCount_) * halfSize_, 0.0f);
        }
        activePartitions_.fill(0u);
        hasKernel_.fill(false);
        return true;
#endif
    }

    bool setKernel(uint32_t node, const float* kernel, uint32_t frames)
    {
#if !S3G_HAS_RESPONSE_TRACE_FFT
        (void)node;
        (void)kernel;
        (void)frames;
        return false;
#else
        if (!beginKernel(node, frames)) return false;
        bool succeeded = true;
        for (uint32_t partition = 0u;
            partition < activePartitions_[node]; ++partition) {
            succeeded = setKernelPartition(
                node, kernel, frames, partition) && succeeded;
        }
        return finishKernel(node) && succeeded;
#endif
    }

    bool beginKernel(uint32_t node, uint32_t frames)
    {
#if !S3G_HAS_RESPONSE_TRACE_FFT
        (void)node;
        (void)frames;
        return false;
#else
        if (node >= nodeCount_ || !setup_ || frames == 0u) return false;
        frames = std::min<uint32_t>(frames,
            partitionCount_ * kResponseTracePartitionSize);
        activePartitions_[node] = std::max<uint32_t>(1u,
            (frames + kResponseTracePartitionSize - 1u)
                / kResponseTracePartitionSize);
        hasKernel_[node] = false;
        return true;
#endif
    }

    bool setKernelPartition(uint32_t node, const float* kernel,
        uint32_t frames, uint32_t partition)
    {
#if !S3G_HAS_RESPONSE_TRACE_FFT
        (void)node;
        (void)kernel;
        (void)frames;
        (void)partition;
        return false;
#else
        if (node >= nodeCount_ || !setup_ || !kernel || frames == 0u
            || partition >= activePartitions_[node]) return false;
        frames = std::min<uint32_t>(frames,
            partitionCount_ * kResponseTracePartitionSize);
        std::fill(kernelFrame_.begin(), kernelFrame_.end(), 0.0f);
        const uint32_t offset = partition * kResponseTracePartitionSize;
        if (offset >= frames) return false;
        const uint32_t count = std::min<uint32_t>(
            kResponseTracePartitionSize, frames - offset);
        std::copy_n(kernel + offset, count, kernelFrame_.data());
        forward(kernelFrame_.data(), kernelSplitReal_.data(),
            kernelSplitImag_.data());
        const size_t destination = static_cast<size_t>(node)
                * partitionCount_ * halfSize_
            + static_cast<size_t>(partition) * halfSize_;
        std::copy_n(kernelSplitReal_.data(), halfSize_,
            kernelReal_.data() + destination);
        std::copy_n(kernelSplitImag_.data(), halfSize_,
            kernelImag_.data() + destination);
        return true;
#endif
    }

    bool finishKernel(uint32_t node)
    {
        if (node >= nodeCount_ || activePartitions_[node] == 0u) return false;
        hasKernel_[node] = true;
        return true;
    }

    void clearKernels()
    {
        hasKernel_.fill(false);
        activePartitions_.fill(0u);
        reset();
    }

    void invalidateKernels()
    {
        hasKernel_.fill(false);
        activePartitions_.fill(0u);
    }

    void reset()
    {
        for (uint32_t node = 0u; node < nodeCount_; ++node) {
            resetNode(node);
        }
    }

    void resetNode(uint32_t node)
    {
        if (node >= nodeCount_) return;
        Node& state = nodes_[node];
        state.inputPosition = 0u;
        state.historyPosition = 0u;
        std::fill(state.overlap.begin(), state.overlap.end(), 0.0f);
        std::fill(state.input.begin(), state.input.end(), 0.0f);
        std::fill(state.output.begin(), state.output.end(), 0.0f);
        std::fill(state.historyReal.begin(), state.historyReal.end(), 0.0f);
        std::fill(state.historyImag.begin(), state.historyImag.end(), 0.0f);
    }

    float processSample(uint32_t node, float input)
    {
#if !S3G_HAS_RESPONSE_TRACE_FFT
        (void)node;
        (void)input;
        return 0.0f;
#else
        if (node >= nodeCount_ || !hasKernel_[node]) return 0.0f;
        Node& state = nodes_[node];
        const float output = state.output[state.inputPosition];
        state.input[state.inputPosition] = flushDenormal(input);
        ++state.inputPosition;
        if (state.inputPosition >= kResponseTracePartitionSize) {
            processBlock(node, state);
            state.inputPosition = 0u;
        }
        return flushDenormal(output);
#endif
    }

    bool ready() const
    {
        return std::any_of(hasKernel_.begin(), hasKernel_.end(),
            [](bool value) { return value; });
    }
    uint32_t latencyFrames() const { return kResponseTracePartitionSize; }

private:
    struct Node {
        uint32_t inputPosition = 0u;
        uint32_t historyPosition = 0u;
        std::vector<float> overlap;
        std::vector<float> input;
        std::vector<float> output;
        std::vector<float> frame;
        std::vector<float> accumulatorReal;
        std::vector<float> accumulatorImag;
        std::vector<float> historyReal;
        std::vector<float> historyImag;
    };

#if S3G_HAS_RESPONSE_TRACE_FFT
    void forward(float* time, float* real, float* imag)
    {
        DSPSplitComplex split { real, imag };
        vDSP_ctoz(reinterpret_cast<const DSPComplex*>(time), 2,
            &split, 1, halfSize_);
        vDSP_fft_zrip(setup_, &split, 1, 11u, FFT_FORWARD);
    }

    void inverse(float* real, float* imag, float* time)
    {
        DSPSplitComplex split { real, imag };
        vDSP_fft_zrip(setup_, &split, 1, 11u, FFT_INVERSE);
        vDSP_ztoc(&split, 1, reinterpret_cast<DSPComplex*>(time), 2,
            halfSize_);
    }

    void processBlock(uint32_t node, Node& state)
    {
        std::copy(state.overlap.begin(), state.overlap.end(),
            state.frame.begin());
        std::copy(state.input.begin(), state.input.end(),
            state.frame.begin() + kResponseTracePartitionSize);
        std::copy(state.input.begin(), state.input.end(), state.overlap.begin());
        float* historyReal = state.historyReal.data()
            + static_cast<size_t>(state.historyPosition) * halfSize_;
        float* historyImag = state.historyImag.data()
            + static_cast<size_t>(state.historyPosition) * halfSize_;
        forward(state.frame.data(), historyReal, historyImag);
        std::fill(state.accumulatorReal.begin(), state.accumulatorReal.end(), 0.0f);
        std::fill(state.accumulatorImag.begin(), state.accumulatorImag.end(), 0.0f);
        const size_t nodeOffset = static_cast<size_t>(node)
            * partitionCount_ * halfSize_;
        for (uint32_t partition = 0u;
            partition < activePartitions_[node]; ++partition) {
            const uint32_t history = (state.historyPosition
                + partitionCount_ - partition) % partitionCount_;
            float* xr = state.historyReal.data()
                + static_cast<size_t>(history) * halfSize_;
            float* xi = state.historyImag.data()
                + static_cast<size_t>(history) * halfSize_;
            float* hr = kernelReal_.data()
                + nodeOffset + static_cast<size_t>(partition) * halfSize_;
            float* hi = kernelImag_.data()
                + nodeOffset + static_cast<size_t>(partition) * halfSize_;
            state.accumulatorReal[0] += xr[0] * hr[0];
            state.accumulatorImag[0] += xi[0] * hi[0];
            if (halfSize_ > 1u) {
                DSPSplitComplex x { xr + 1u, xi + 1u };
                DSPSplitComplex h { hr + 1u, hi + 1u };
                DSPSplitComplex a { state.accumulatorReal.data() + 1u,
                    state.accumulatorImag.data() + 1u };
                vDSP_zvma(&x, 1, &h, 1, &a, 1, &a, 1, halfSize_ - 1u);
            }
        }
        inverse(state.accumulatorReal.data(), state.accumulatorImag.data(),
            state.frame.data());
        const float scale = 1.0f / static_cast<float>(4u * fftSize_);
        for (uint32_t index = 0u; index < kResponseTracePartitionSize; ++index) {
            state.output[index] = flushDenormal(
                state.frame[kResponseTracePartitionSize + index] * scale);
        }
        state.historyPosition = (state.historyPosition + 1u) % partitionCount_;
    }
#endif

    void release()
    {
#if S3G_HAS_RESPONSE_TRACE_FFT
        if (setup_) vDSP_destroy_fftsetup(setup_);
        setup_ = nullptr;
#endif
        nodeCount_ = 0u;
        partitionCount_ = 0u;
        activePartitions_.fill(0u);
        fftSize_ = 0u;
        halfSize_ = 0u;
        hasKernel_.fill(false);
        kernelReal_.clear();
        kernelImag_.clear();
        kernelFrame_.clear();
        kernelSplitReal_.clear();
        kernelSplitImag_.clear();
        for (auto& node : nodes_) node = Node {};
    }

#if S3G_HAS_RESPONSE_TRACE_FFT
    FFTSetup setup_ = nullptr;
#endif
    uint32_t nodeCount_ = 0u;
    uint32_t partitionCount_ = 0u;
    std::array<uint32_t, kAmbiEffectDjFilterMaxPickups>
        activePartitions_ {};
    uint32_t fftSize_ = 0u;
    uint32_t halfSize_ = 0u;
    std::array<bool, kAmbiEffectDjFilterMaxPickups> hasKernel_ {};
    std::vector<float> kernelReal_;
    std::vector<float> kernelImag_;
    std::vector<float> kernelFrame_;
    std::vector<float> kernelSplitReal_;
    std::vector<float> kernelSplitImag_;
    std::array<Node, kAmbiEffectDjFilterMaxPickups> nodes_ {};
};

} // namespace response_trace_detail

class AmbiEffectResponseTrace {
private:
    enum class SpatialTransitionStage : uint32_t {
        Stable = 0u,
        FadingOut,
        FadingIn,
    };

    enum class ResponsePreparationStage : uint32_t {
        Idle = 0u,
        CopyAndMean,
        Condition,
        Scale,
    };

public:
    bool prepare(double sampleRate)
    {
        prepared_ = false;
        sampleRate_ = std::clamp(std::isfinite(sampleRate) ? sampleRate : 48000.0,
            1000.0, 768000.0);
        maximumResponseFrames_ = std::min<uint32_t>(
            kResponseTraceMaximumFrames, static_cast<uint32_t>(std::ceil(
                sampleRate_ * kResponseTraceMaximumSeconds)));
        try {
            for (auto& buffer : captureBuffer_) {
                buffer.assign(maximumResponseFrames_, 0.0f);
            }
            for (auto& buffer : responseBuffer_) {
                buffer.assign(maximumResponseFrames_, 0.0f);
            }
        } catch (...) {
            maximumResponseFrames_ = 0u;
            return false;
        }
        responseFrames_ = 0u;
        responseSampleRate_ = 0.0;
        capturedBody_ = AmbiEffectBody::Auto;
        capturedPickupCount_ = 0u;
        kernelRebuildPending_ = false;
        kernelBuildNode_ = 0u;
        kernelBuildPartition_ = 0u;
        kernelBuildTargetCount_ = 0u;
        kernelBuildSucceeded_ = true;
        kernelsReady_ = false;
        responsePreparationStage_ = ResponsePreparationStage::Idle;
        for (auto& channel : dryDelay_) {
            channel.assign(kResponseTracePartitionSize, 0.0f);
        }
        spatial_.setAllowCompactBodies(true);
        spatial_.setParams(spatialParams(params_));
        spatial_.prepare(sampleRate_);
        try {
            if (!convolution_.prepare(maximumResponseFrames_,
                kAmbiEffectDjFilterMaxPickups)) return false;
        } catch (...) {
            return false;
        }
        safetyRelease_ = onePole(0.300f);
        excitationRelease_ = onePole(0.250f);
        responseGovernorRelease_ = onePole(0.250f);
        outputCoefficient_ = onePole(0.025f);
        globalCoefficient_ = onePole(0.025f);
        switchCoefficient_ = onePole(0.015f);
        maskCoefficient_ = onePole(0.020f);
        toneCoefficient_ = onePole(0.025f);
        levelCoefficient_ = onePole(0.055f);
        spatialTransitionStep_ = 1.0f / std::max(1.0f,
            static_cast<float>(sampleRate_ * 0.012));
        setParams(params_);
        reset();
        prepared_ = true;
        return S3G_HAS_RESPONSE_TRACE_FFT != 0;
    }

    void reset()
    {
        spatial_.setParams(spatialParams(params_));
        spatial_.reset();
        convolution_.reset();
        for (auto& channel : dryDelay_) {
            std::fill(channel.begin(), channel.end(), 0.0f);
        }
        dryPosition_ = 0u;
        roamingPhase_ = 0.0f;
        targetTopology_ = params_.topology;
        currentTopologyWeights_.fill(0.0f);
        targetTopologyWeights_.fill(0.0f);
        currentTopologyWeights_[static_cast<uint32_t>(targetTopology_)] = 1.0f;
        targetTopologyWeights_ = currentTopologyWeights_;
        currentTopologyAmount_ = params_.topologyAmount;
        currentRoamingRateHz_ = params_.roamingRateHz;
        currentMix_ = params_.mix;
        currentResponseGain_ = dbToGain(params_.responseGainDb);
        currentResponseEnabled_ = responseApplied() ? 1.0f : 0.0f;
        currentTone_ = params_.tone;
        currentMaskDry_ = params_.maskDry;
        currentOutputGain_ = dbToGain(outputTargetDb_.load(
            std::memory_order_relaxed));
        updateMask();
        currentMaskGain_ = targetMaskGain_;
        setChannelGainTargets(spatial_.params().order);
        currentChannelGain_ = targetChannelGain_;
        pendingSpatialParams_ = spatial_.params();
        spatialTransitionStage_ = SpatialTransitionStage::Stable;
        spatialTransitionGain_ = 1.0f;
        safetyGain_ = 1.0f;
        excitationGain_ = 1.0f;
        responseGovernorGain_ = 1.0f;
        capturing_ = false;
        capturePending_ = false;
        captureFrames_ = 0u;
        captureTargetFrames_ = 0u;
        captureBody_ = AmbiEffectBody::Auto;
        capturePickupCount_ = 0u;
        captureOrder_ = 0u;
        nodeLevel_.fill(0.0f);
        nodeResponseLevel_.fill(0.0f);
        toneState_.fill(0.0f);
        responseSlewState_.fill(0.0f);
    }

    void setParams(AmbiEffectResponseTraceParams params)
    {
        auto next = sanitizeAmbiEffectResponseTraceParams(params);
        const auto nextSpatial = spatialParams(next);
        const auto& currentSpatial = spatial_.params();
        const bool spatialChanged = nextSpatial.order != currentSpatial.order
            || resolveAmbiEffectBody(nextSpatial.body, nextSpatial.order, true)
                != resolveAmbiEffectBody(currentSpatial.body,
                    currentSpatial.order, true);
        if (next.topology != targetTopology_) {
            targetTopology_ = next.topology;
            targetTopologyWeights_.fill(0.0f);
            targetTopologyWeights_[static_cast<uint32_t>(targetTopology_)]
                = 1.0f;
        }
        params_ = next;
        outputTargetDb_.store(params_.outputGainDb, std::memory_order_relaxed);
        if (!prepared_) {
            spatial_.setParams(nextSpatial);
            pendingSpatialParams_ = nextSpatial;
            setChannelGainTargets(nextSpatial.order);
        } else if (spatialChanged
            || spatialTransitionStage_ != SpatialTransitionStage::Stable) {
            pendingSpatialParams_ = nextSpatial;
            if (spatialChanged) {
                spatialTransitionStage_ = SpatialTransitionStage::FadingOut;
                if (capturing_) cancelCapture();
            } else if (spatialTransitionStage_
                == SpatialTransitionStage::FadingOut) {
                spatialTransitionStage_ = SpatialTransitionStage::FadingIn;
            }
        }
        updateMask();
    }

    const AmbiEffectResponseTraceParams& params() const { return params_; }

    void setOutputGainTarget(float outputGainDb)
    {
        outputTargetDb_.store(clamp(std::isfinite(outputGainDb)
            ? outputGainDb : 0.0f, -60.0f, 12.0f),
            std::memory_order_relaxed);
    }

    void startCapture()
    {
        if (captureBuffer_[0].empty() || capturing_ || capturePending_) return;
        clearPending_ = false;
        params_.responseEnabled = 0u;
        capturePending_ = true;
        servicePendingCapture();
    }

    void servicePendingCapture()
    {
        if (!capturePending_ || capturing_
            || responsePreparationStage_ != ResponsePreparationStage::Idle
            || kernelRebuildPending_
            || spatialTransitionStage_ != SpatialTransitionStage::Stable) return;
        capturePending_ = false;
        capturing_ = true;
        captureBody_ = spatial_.resolvedBody();
        capturePickupCount_ = spatial_.activePickupCount();
        captureOrder_ = spatial_.params().order;
        captureFrames_ = 0u;
        captureTargetFrames_ = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::round(
                params_.captureSeconds * static_cast<float>(sampleRate_))),
            1u, maximumResponseFrames_);
    }

    void cancelCapture()
    {
        capturePending_ = false;
        capturing_ = false;
        captureFrames_ = 0u;
        captureTargetFrames_ = 0u;
        captureBody_ = AmbiEffectBody::Auto;
        capturePickupCount_ = 0u;
        captureOrder_ = 0u;
    }

    void clearResponse()
    {
        cancelCapture();
        params_.responseEnabled = 0u;
        if (prepared_ && (hasStoredResponse()
                || responsePreparationStage_ != ResponsePreparationStage::Idle
                || kernelRebuildPending_)) {
            clearPending_ = true;
            return;
        }
        clearResponseImmediately();
    }

    void clearResponseImmediately()
    {
        clearPending_ = false;
        responseFrames_ = 0u;
        responseSampleRate_ = 0.0;
        capturedPickupCount_ = 0u;
        capturedBody_ = AmbiEffectBody::Auto;
        kernelRebuildPending_ = false;
        kernelBuildNode_ = 0u;
        kernelBuildPartition_ = 0u;
        kernelBuildTargetCount_ = 0u;
        kernelsReady_ = false;
        responsePreparationStage_ = ResponsePreparationStage::Idle;
        convolution_.invalidateKernels();
        toneState_.fill(0.0f);
        responseSlewState_.fill(0.0f);
    }

    bool loadResponse(const float* samples, uint32_t pickupCount,
        AmbiEffectBody capturedBody, uint32_t frames, double sampleRate,
        bool condition = true)
    {
        if (!samples || frames == 0u || !std::isfinite(sampleRate)
            || sampleRate <= 0.0 || responseBuffer_[0].empty()) return false;
        clearPending_ = false;
        responsePreparationStage_ = ResponsePreparationStage::Idle;
        kernelRebuildPending_ = false;
        capturedBody = static_cast<AmbiEffectBody>(std::min<uint32_t>(
            static_cast<uint32_t>(capturedBody), 5u));
        if (capturedBody == AmbiEffectBody::Auto) {
            capturedBody = spatial_.resolvedBody();
        }
        pickupCount = std::min<uint32_t>(pickupCount,
            ambiEffectBodyPickupCount(capturedBody));
        if (pickupCount == 0u) return false;
        params_.responseEnabled = 0u;
        cancelCapture();
        capturedBody_ = capturedBody;
        capturedPickupCount_ = pickupCount;
        const double ratio = sampleRate / sampleRate_;
        responseFrames_ = std::min<uint32_t>(maximumResponseFrames_,
            static_cast<uint32_t>(std::ceil(static_cast<double>(frames) / ratio)));
        for (uint32_t node = 0u; node < capturedPickupCount_; ++node) {
            const float* source = samples + static_cast<size_t>(node) * frames;
            for (uint32_t index = 0u; index < responseFrames_; ++index) {
                const double position = static_cast<double>(index) * ratio;
                const uint32_t first = std::min<uint32_t>(
                    static_cast<uint32_t>(position), frames - 1u);
                const uint32_t second = std::min<uint32_t>(
                    first + 1u, frames - 1u);
                const float fraction = static_cast<float>(position
                    - std::floor(position));
                const float value = lerp(
                    source[first], source[second], fraction);
                responseBuffer_[node][index] = std::isfinite(value)
                    ? value : 0.0f;
            }
        }
        if (condition) conditionResponse(responseFrames_);
        responseSampleRate_ = sampleRate_;
        return rebuildConvolutionKernels();
    }

    bool hasResponse() const
    {
        return hasStoredResponse() && kernelsReady_ && convolution_.ready();
    }
    bool responseApplied() const
    {
        return hasResponse() && params_.responseEnabled != 0u
            && !capturing_ && !capturePending_;
    }
    bool capturing() const { return capturing_ || capturePending_; }
    bool preparingResponse() const
    {
        return responsePreparationStage_ != ResponsePreparationStage::Idle
            || kernelRebuildPending_
            || (hasStoredResponse()
                && spatialTransitionStage_ != SpatialTransitionStage::Stable);
    }
    float captureProgress() const
    {
        return capturing_ && captureTargetFrames_ > 0u
            ? clamp(static_cast<float>(captureFrames_)
                / static_cast<float>(captureTargetFrames_), 0.0f, 1.0f)
            : 0.0f;
    }
    float preparationProgress() const
    {
        if (responsePreparationStage_ != ResponsePreparationStage::Idle
            && capturedPickupCount_ > 0u && responseFrames_ > 0u) {
            const double nodeFraction = static_cast<double>(
                preparationFrame_) / static_cast<double>(responseFrames_);
            double completed = 0.0;
            if (responsePreparationStage_
                == ResponsePreparationStage::CopyAndMean) {
                completed = static_cast<double>(preparationNode_) * 2.0
                    + nodeFraction;
            } else if (responsePreparationStage_
                == ResponsePreparationStage::Condition) {
                completed = static_cast<double>(preparationNode_) * 2.0
                    + 1.0 + nodeFraction;
            } else {
                completed = static_cast<double>(capturedPickupCount_) * 2.0
                    + static_cast<double>(preparationNode_) + nodeFraction;
            }
            return clamp(static_cast<float>(0.5 * completed
                / (static_cast<double>(capturedPickupCount_) * 3.0)),
                0.0f, 0.5f);
        }
        if (kernelRebuildPending_ && kernelBuildTargetCount_ > 0u) {
            return 0.5f + 0.5f * clamp((static_cast<float>(kernelBuildNode_)
                    + static_cast<float>(kernelBuildPartition_)
                        / static_cast<float>(std::max<uint32_t>(
                            1u, responsePartitionCount())))
                / static_cast<float>(kernelBuildTargetCount_), 0.0f, 1.0f);
        }
        if (spatialTransitionStage_ == SpatialTransitionStage::FadingOut) {
            return 0.0f;
        }
        if (spatialTransitionStage_ == SpatialTransitionStage::FadingIn) {
            return 0.5f + 0.5f * spatialTransitionGain_;
        }
        return kernelsReady_ ? 1.0f : 0.0f;
    }
    uint32_t responseFrames() const { return responseFrames_; }
    double responseSampleRate() const { return responseSampleRate_; }
    AmbiEffectBody capturedBody() const { return capturedBody_; }
    uint32_t capturedPickupCount() const { return capturedPickupCount_; }
    float responseSample(uint32_t node, uint32_t frame) const
    {
        return node < capturedPickupCount_ && frame < responseFrames_
            ? responseBuffer_[node][frame] : 0.0f;
    }
    uint32_t latencyFrames() const { return kResponseTracePartitionSize; }
    uint32_t tailFrames() const
    {
        return hasStoredResponse() && (params_.responseEnabled != 0u
                || clearPending_
                || currentResponseEnabled_ > 1.0e-4f)
            ? latencyFrames() + responseFrames_ : 0u;
    }
    bool responseAudible() const { return currentResponseEnabled_ > 1.0e-4f; }
    AmbiEffectBody resolvedBody() const { return spatial_.resolvedBody(); }
    uint32_t activePickupCount() const { return spatial_.activePickupCount(); }
    float roamingPhase() const { return roamingPhase_; }
    float safetyGain() const { return safetyGain_; }
    float excitationGovernorGain() const { return excitationGain_; }
    float responseGovernorGain() const { return responseGovernorGain_; }
    float containmentGain() const
    {
        return std::min(excitationGain_, responseGovernorGain_);
    }
    float nodeLevel(uint32_t node) const
    {
        return node < nodeLevel_.size() ? nodeLevel_[node] : 0.0f;
    }
    float nodeResponseLevel(uint32_t node) const
    {
        return node < nodeResponseLevel_.size()
            ? nodeResponseLevel_[node] : 0.0f;
    }
    float nodeWetMask(uint32_t node) const
    {
        return node < currentMaskGain_.size()
            ? currentMaskGain_[node] : 1.0f;
    }

    template <typename Sample>
    void process(Sample** input, Sample** output, uint32_t inputChannels,
        uint32_t outputChannels, uint32_t frames)
    {
        if (!output) return;
        const uint32_t preparationBudget = std::clamp<uint32_t>(
            frames * 32u, 512u, 8192u);
        const uint32_t partitionBudget = std::clamp<uint32_t>(
            std::max<uint32_t>(1u, frames / 64u), 1u, 4u);
        serviceCapturedResponsePreparation(preparationBudget);
        serviceConvolutionKernelRebuild(partitionBudget);
        const uint32_t inCount = std::min<uint32_t>(inputChannels,
            kAmbiEffectDjFilterMaxChannels);
        const uint32_t outCount = std::min<uint32_t>(outputChannels,
            kAmbiEffectDjFilterMaxChannels);
        std::array<float, kAmbiEffectDjFilterMaxChannels> field {};
        std::array<float, kAmbiEffectDjFilterMaxChannels> delayedField {};
        std::array<float, kAmbiEffectDjFilterMaxChannels> correction {};
        std::array<float, kAmbiEffectDjFilterMaxChannels> outputFrame {};
        std::array<float, kAmbiEffectDjFilterMaxPickups> ears {};
        std::array<float, kAmbiEffectDjFilterMaxPickups> delayedEars {};
        std::array<float, kAmbiEffectDjFilterMaxPickups> response {};
        std::array<std::array<float, kAmbiEffectDjFilterMaxPickups>, 4u>
            topologyRoutes {};
        std::array<float, kAmbiEffectDjFilterMaxPickups> delta {};

        for (uint32_t frameIndex = 0u; frameIndex < frames; ++frameIndex) {
            smoothGlobals();
            servicePendingClear();
            serviceSpatialTransition();
            servicePendingCapture();
            const uint32_t channels = spatial_.spatialChannelCount();
            const uint32_t pickupCount = spatial_.activePickupCount();
            roamingPhase_ += currentRoamingRateHz_
                / static_cast<float>(sampleRate_);
            roamingPhase_ -= std::floor(roamingPhase_);
            for (uint32_t channel = 0u;
                channel < kAmbiEffectDjFilterMaxChannels; ++channel) {
                const float value = channel < inCount && input && input[channel]
                    ? static_cast<float>(input[channel][frameIndex]) : 0.0f;
                field[channel] = std::isfinite(value) ? value : 0.0f;
                delayedField[channel] = dryDelay_[channel][dryPosition_];
                dryDelay_[channel][dryPosition_] = field[channel];
            }
            spatial_.decodeField(field.data(), channels, ears);
            spatial_.decodeField(delayedField.data(), channels, delayedEars);
            captureFrame(ears, pickupCount);

            float excitationPeak = 0.0f;
            for (uint32_t node = 0u; node < pickupCount; ++node) {
                nodeLevel_[node] += (std::abs(ears[node]) - nodeLevel_[node])
                    * levelCoefficient_;
                excitationPeak = std::max(excitationPeak, std::abs(ears[node]));
            }
            constexpr float excitationCeiling = 0.25f;
            const float excitationTarget = hasStoredResponse()
                    && excitationPeak > excitationCeiling
                ? excitationCeiling / excitationPeak : 1.0f;
            if (excitationTarget < excitationGain_) {
                excitationGain_ = excitationTarget;
            } else {
                excitationGain_ += (1.0f - excitationGain_)
                    * excitationRelease_;
            }
            excitationGain_ = clamp(excitationGain_, 0.0f, 1.0f);

            const float cutoff = 500.0f * std::pow(
                19000.0f / 500.0f, currentTone_);
            const float responseToneCoefficient = 1.0f - std::exp(
                -2.0f * kPi * cutoff / static_cast<float>(sampleRate_));
            float responsePeak = 0.0f;
            for (uint32_t node = 0u; node < pickupCount; ++node) {
                float convolved = kernelsReady_
                    ? convolution_.processSample(node,
                        ears[node] * excitationGain_)
                    : 0.0f;
                if (!std::isfinite(convolved)) convolved = 0.0f;
                toneState_[node] += (convolved - toneState_[node])
                    * responseToneCoefficient;
                if (!std::isfinite(toneState_[node])) toneState_[node] = 0.0f;
                response[node] = toneState_[node] * currentResponseGain_;
                if (!std::isfinite(response[node])) response[node] = 0.0f;
                responsePeak = std::max(responsePeak, std::abs(response[node]));
            }
            updateResponseGovernor(responsePeak);
            const float maximumResponseStep = clamp(0.035f * 48000.0f
                    / static_cast<float>(sampleRate_), 0.001f, 0.08f);
            for (uint32_t node = 0u; node < pickupCount; ++node) {
                const float bounded = std::tanh(
                    response[node] * responseGovernorGain_);
                responseSlewState_[node] += clamp(
                    bounded - responseSlewState_[node],
                    -maximumResponseStep, maximumResponseStep);
                response[node] = flushDenormal(responseSlewState_[node]);
                nodeResponseLevel_[node] += (std::abs(response[node])
                    - nodeResponseLevel_[node]) * levelCoefficient_;
            }
            for (uint32_t node = pickupCount;
                node < kAmbiEffectDjFilterMaxPickups; ++node) {
                response[node] = 0.0f;
                nodeResponseLevel_[node] += (0.0f - nodeResponseLevel_[node])
                    * levelCoefficient_;
            }

            for (uint32_t topology = 0u; topology < topologyRoutes.size();
                ++topology) {
                spatial_.routeNodes(response, topologyRoutes[topology],
                    static_cast<AmbiEffectTopology>(topology), roamingPhase_);
            }
            delta.fill(0.0f);
            for (uint32_t node = 0u; node < pickupCount; ++node) {
                float routed = 0.0f;
                for (uint32_t topology = 0u;
                    topology < topologyRoutes.size(); ++topology) {
                    routed += topologyRoutes[topology][node]
                        * currentTopologyWeights_[topology];
                }
                const float processed = lerp(response[node], routed,
                    currentTopologyAmount_);
                const float target = lerp(delayedEars[node] * currentMaskDry_,
                    processed, currentMaskGain_[node]);
                delta[node] = target - delayedEars[node];
            }
            correction.fill(0.0f);
            spatial_.encodeNodes(delta, correction.data(), channels);
            float peak = 0.0f;
            const float wetGain = currentMix_ * currentResponseEnabled_
                * spatialTransitionGain_;
            for (uint32_t channel = 0u; channel < outCount; ++channel) {
                const float wet = channel < channels
                    ? correction[channel] * wetGain : 0.0f;
                const float value = (delayedField[channel]
                    * currentChannelGain_[channel] + wet)
                    * currentOutputGain_;
                outputFrame[channel] = flushDenormal(
                    std::isfinite(value) ? value : 0.0f);
                peak = std::max(peak, std::abs(outputFrame[channel]));
            }
            updateSafety(peak);
            for (uint32_t channel = 0u; channel < outCount; ++channel) {
                if (output[channel]) output[channel][frameIndex]
                    = static_cast<Sample>(outputFrame[channel] * safetyGain_);
            }
            for (uint32_t channel = outCount; channel < outputChannels;
                ++channel) {
                if (output[channel]) output[channel][frameIndex] = Sample(0);
            }
            dryPosition_ = (dryPosition_ + 1u) % kResponseTracePartitionSize;
        }
    }

private:
    float onePole(float seconds) const
    {
        return 1.0f - std::exp(-1.0f
            / std::max(1.0f, static_cast<float>(sampleRate_ * seconds)));
    }

    static AmbiEffectDjFilterParams spatialParams(
        const AmbiEffectResponseTraceParams& params)
    {
        AmbiEffectDjFilterParams result {};
        result.engine = AmbiEffectEngine::Gain;
        result.order = params.order;
        result.body = params.body;
        result.topology = params.topology;
        result.maskAmount = params.maskAmount;
        result.maskAzimuthDeg = params.maskAzimuthDeg;
        result.maskElevationDeg = params.maskElevationDeg;
        result.maskWidth = params.maskWidth;
        result.maskCurve = params.maskCurve;
        result.maskDry = params.maskDry;
        return result;
    }

    bool hasStoredResponse() const
    {
        return responseFrames_ > 0u && capturedPickupCount_ > 0u;
    }

    uint32_t responsePartitionCount() const
    {
        return responseFrames_ > 0u
            ? std::max<uint32_t>(1u, (responseFrames_
                + kResponseTracePartitionSize - 1u)
                    / kResponseTracePartitionSize)
            : 0u;
    }

    void beginCapturedResponsePreparation()
    {
        responseSampleRate_ = 0.0;
        kernelsReady_ = false;
        kernelRebuildPending_ = false;
        kernelBuildNode_ = 0u;
        kernelBuildPartition_ = 0u;
        kernelBuildTargetCount_ = 0u;
        convolution_.invalidateKernels();
        toneState_.fill(0.0f);
        preparationNode_ = 0u;
        preparationFrame_ = 0u;
        preparationSum_ = 0.0;
        preparationNodeEnergy_ = 0.0;
        preparationNodeAbsoluteSum_ = 0.0;
        preparationMaximumEnergy_ = 0.0;
        preparationMaximumAbsoluteSum_ = 0.0;
        preparationPeak_ = 0.0f;
        preparationMean_ = 0.0f;
        preparationHighpassState_ = 0.0f;
        preparationPrevious_ = 0.0f;
        preparationScale_ = 1.0f;
        responsePreparationStage_ = hasStoredResponse()
            ? ResponsePreparationStage::CopyAndMean
            : ResponsePreparationStage::Idle;
    }

    void serviceCapturedResponsePreparation(uint32_t workBudget)
    {
        if (responsePreparationStage_ == ResponsePreparationStage::Idle
            || clearPending_ || responseFrames_ == 0u
            || capturedPickupCount_ == 0u) return;
        const uint32_t fadeFrames = std::min<uint32_t>(128u,
            std::max<uint32_t>(1u, responseFrames_ / 8u));
        const float highpassPole = std::exp(-2.0f * kPi * 20.0f
            / static_cast<float>(sampleRate_));
        while (workBudget > 0u
            && responsePreparationStage_ != ResponsePreparationStage::Idle) {
            if (responsePreparationStage_
                == ResponsePreparationStage::CopyAndMean) {
                const float value = captureBuffer_[preparationNode_]
                    [preparationFrame_];
                responseBuffer_[preparationNode_][preparationFrame_] = value;
                preparationSum_ += value;
                ++preparationFrame_;
                --workBudget;
                if (preparationFrame_ < responseFrames_) continue;
                preparationMean_ = static_cast<float>(preparationSum_
                    / static_cast<double>(responseFrames_));
                preparationFrame_ = 0u;
                preparationNodeEnergy_ = 0.0;
                preparationNodeAbsoluteSum_ = 0.0;
                preparationHighpassState_ = 0.0f;
                preparationPrevious_ = 0.0f;
                responsePreparationStage_ = ResponsePreparationStage::Condition;
                continue;
            }
            if (responsePreparationStage_
                == ResponsePreparationStage::Condition) {
                float value = responseBuffer_[preparationNode_]
                    [preparationFrame_] - preparationMean_;
                preparationHighpassState_ = highpassPole
                    * (preparationHighpassState_ + value
                        - preparationPrevious_);
                preparationPrevious_ = value;
                value = preparationHighpassState_;
                if (preparationFrame_ + fadeFrames > responseFrames_) {
                    value *= static_cast<float>(responseFrames_
                            - preparationFrame_)
                        / static_cast<float>(fadeFrames);
                }
                value = std::isfinite(value) ? flushDenormal(value) : 0.0f;
                responseBuffer_[preparationNode_][preparationFrame_] = value;
                preparationPeak_ = std::max(preparationPeak_, std::abs(value));
                preparationNodeEnergy_ += static_cast<double>(value) * value;
                preparationNodeAbsoluteSum_ += std::abs(value);
                ++preparationFrame_;
                --workBudget;
                if (preparationFrame_ < responseFrames_) continue;
                preparationMaximumEnergy_ = std::max(
                    preparationMaximumEnergy_, preparationNodeEnergy_);
                preparationMaximumAbsoluteSum_ = std::max(
                    preparationMaximumAbsoluteSum_,
                    preparationNodeAbsoluteSum_);
                ++preparationNode_;
                preparationFrame_ = 0u;
                preparationSum_ = 0.0;
                if (preparationNode_ < capturedPickupCount_) {
                    responsePreparationStage_
                        = ResponsePreparationStage::CopyAndMean;
                    continue;
                }
                preparationScale_ = 1.0f;
                if (preparationPeak_ > 1.0e-9f) {
                    preparationScale_ = std::min(
                        preparationScale_, 0.75f / preparationPeak_);
                }
                if (preparationMaximumEnergy_ > 1.0e-18) {
                    preparationScale_ = std::min(preparationScale_,
                        0.65f / static_cast<float>(
                            std::sqrt(preparationMaximumEnergy_)));
                }
                if (preparationMaximumAbsoluteSum_ > 1.0e-12) {
                    preparationScale_ = std::min(preparationScale_,
                        6.0f / static_cast<float>(
                            preparationMaximumAbsoluteSum_));
                }
                preparationNode_ = 0u;
                responsePreparationStage_ = ResponsePreparationStage::Scale;
                continue;
            }
            float& value = responseBuffer_[preparationNode_][preparationFrame_];
            value = flushDenormal(value * preparationScale_);
            ++preparationFrame_;
            --workBudget;
            if (preparationFrame_ < responseFrames_) continue;
            preparationFrame_ = 0u;
            ++preparationNode_;
            if (preparationNode_ < capturedPickupCount_) continue;
            responsePreparationStage_ = ResponsePreparationStage::Idle;
            responseSampleRate_ = sampleRate_;
            beginConvolutionKernelRebuild();
        }
    }

    void captureFrame(
        const std::array<float, kAmbiEffectDjFilterMaxPickups>& ears,
        uint32_t pickupCount)
    {
        if (!capturing_ || captureFrames_ >= captureTargetFrames_) return;
        if (pickupCount != capturePickupCount_
            || spatial_.resolvedBody() != captureBody_
            || spatial_.params().order != captureOrder_) {
            cancelCapture();
            return;
        }
        for (uint32_t node = 0u; node < capturePickupCount_; ++node) {
            captureBuffer_[node][captureFrames_] = std::isfinite(ears[node])
                ? ears[node] : 0.0f;
        }
        ++captureFrames_;
        if (captureFrames_ < captureTargetFrames_) return;
        responseFrames_ = captureFrames_;
        capturedBody_ = captureBody_;
        capturedPickupCount_ = capturePickupCount_;
        capturing_ = false;
        captureTargetFrames_ = 0u;
        captureBody_ = AmbiEffectBody::Auto;
        capturePickupCount_ = 0u;
        captureOrder_ = 0u;
        beginCapturedResponsePreparation();
    }

    void conditionResponse(uint32_t frames)
    {
        if (frames == 0u || capturedPickupCount_ == 0u) return;
        const uint32_t fadeFrames = std::min<uint32_t>(128u,
            std::max<uint32_t>(1u, frames / 8u));
        float peak = 0.0f;
        double maximumEnergy = 0.0;
        double maximumAbsoluteSum = 0.0;
        const float highpassPole = std::exp(-2.0f * kPi * 20.0f
            / static_cast<float>(sampleRate_));
        for (uint32_t node = 0u; node < capturedPickupCount_; ++node) {
            double mean = 0.0;
            for (uint32_t index = 0u; index < frames; ++index) {
                mean += responseBuffer_[node][index];
            }
            mean /= static_cast<double>(frames);
            double energy = 0.0;
            double absoluteSum = 0.0;
            float highpassState = 0.0f;
            float previous = 0.0f;
            for (uint32_t index = 0u; index < frames; ++index) {
                float value = responseBuffer_[node][index]
                    - static_cast<float>(mean);
                highpassState = highpassPole
                    * (highpassState + value - previous);
                previous = value;
                value = highpassState;
                if (index + fadeFrames > frames) {
                    value *= static_cast<float>(frames - index)
                        / static_cast<float>(fadeFrames);
                }
                responseBuffer_[node][index] = flushDenormal(value);
                peak = std::max(peak, std::abs(value));
                energy += static_cast<double>(value) * value;
                absoluteSum += std::abs(value);
            }
            maximumEnergy = std::max(maximumEnergy, energy);
            maximumAbsoluteSum = std::max(maximumAbsoluteSum, absoluteSum);
        }
        float scale = 1.0f;
        if (peak > 1.0e-9f) scale = std::min(scale, 0.75f / peak);
        if (maximumEnergy > 1.0e-18) scale = std::min(scale,
            0.65f / static_cast<float>(std::sqrt(maximumEnergy)));
        if (maximumAbsoluteSum > 1.0e-12) scale = std::min(scale,
            6.0f / static_cast<float>(maximumAbsoluteSum));
        for (uint32_t node = 0u; node < capturedPickupCount_; ++node) {
            for (uint32_t index = 0u; index < frames; ++index) {
                responseBuffer_[node][index] *= scale;
            }
        }
    }

    uint32_t nearestCapturedPickup(uint32_t node) const
    {
        const Vec3 target = spatial_.nodeDirection(node);
        const auto capturedDirections = ambiEffectBodyDirections(capturedBody_);
        uint32_t best = 0u;
        float bestDot = -2.0f;
        for (uint32_t candidate = 0u;
            candidate < capturedPickupCount_; ++candidate) {
            const Vec3 source = capturedDirections[candidate];
            const float value = target.x * source.x + target.y * source.y
                + target.z * source.z;
            if (value > bestDot) {
                bestDot = value;
                best = candidate;
            }
        }
        return best;
    }

    void beginConvolutionKernelRebuild()
    {
        convolution_.invalidateKernels();
        kernelBuildNode_ = 0u;
        kernelBuildPartition_ = 0u;
        kernelBuildTargetCount_ = hasStoredResponse()
            ? spatial_.activePickupCount() : 0u;
        kernelBuildSucceeded_ = true;
        kernelsReady_ = false;
        kernelRebuildPending_ = kernelBuildTargetCount_ > 0u;
        toneState_.fill(0.0f);
        responseSlewState_.fill(0.0f);
    }

    void serviceConvolutionKernelRebuild(uint32_t partitionBudget)
    {
        const uint32_t partitions = responsePartitionCount();
        while (kernelRebuildPending_ && partitionBudget > 0u) {
            const uint32_t captured = nearestCapturedPickup(kernelBuildNode_);
            bool succeeded = true;
            if (kernelBuildPartition_ == 0u) {
                succeeded = convolution_.beginKernel(
                    kernelBuildNode_, responseFrames_);
            }
            succeeded = convolution_.setKernelPartition(kernelBuildNode_,
                responseBuffer_[captured].data(), responseFrames_,
                kernelBuildPartition_) && succeeded;
            kernelBuildSucceeded_ = succeeded && kernelBuildSucceeded_;
            ++kernelBuildPartition_;
            --partitionBudget;
            if (kernelBuildPartition_ < partitions) continue;
            kernelBuildSucceeded_ = convolution_.finishKernel(kernelBuildNode_)
                && kernelBuildSucceeded_;
            convolution_.resetNode(kernelBuildNode_);
            kernelBuildPartition_ = 0u;
            ++kernelBuildNode_;
            if (kernelBuildNode_ < kernelBuildTargetCount_) continue;
            kernelRebuildPending_ = false;
            kernelsReady_ = kernelBuildSucceeded_;
        }
    }

    bool rebuildConvolutionKernels()
    {
        beginConvolutionKernelRebuild();
        while (kernelRebuildPending_) {
            serviceConvolutionKernelRebuild(64u);
        }
        return kernelsReady_;
    }

    void smoothGlobals()
    {
        float topologyWeightSum = 0.0f;
        for (uint32_t topology = 0u;
            topology < currentTopologyWeights_.size(); ++topology) {
            currentTopologyWeights_[topology] += (
                targetTopologyWeights_[topology]
                - currentTopologyWeights_[topology]) * topologyCoefficient_;
            topologyWeightSum += currentTopologyWeights_[topology];
        }
        if (topologyWeightSum > 1.0e-9f) {
            const float inverse = 1.0f / topologyWeightSum;
            for (float& weight : currentTopologyWeights_) weight *= inverse;
        } else {
            currentTopologyWeights_ = targetTopologyWeights_;
        }
        currentTopologyAmount_ += (params_.topologyAmount
            - currentTopologyAmount_) * globalCoefficient_;
        currentRoamingRateHz_ += (params_.roamingRateHz
            - currentRoamingRateHz_) * globalCoefficient_;
        currentMix_ += (params_.mix - currentMix_) * globalCoefficient_;
        currentResponseGain_ += (dbToGain(params_.responseGainDb)
            - currentResponseGain_) * globalCoefficient_;
        const bool responseCanSound = params_.responseEnabled != 0u
            && hasResponse() && !capturing_ && !capturePending_
            && !clearPending_
            && responsePreparationStage_ == ResponsePreparationStage::Idle
            && spatialTransitionStage_ != SpatialTransitionStage::FadingOut;
        currentResponseEnabled_ += ((responseCanSound ? 1.0f : 0.0f)
            - currentResponseEnabled_) * switchCoefficient_;
        currentTone_ += (params_.tone - currentTone_) * toneCoefficient_;
        currentMaskDry_ += (params_.maskDry - currentMaskDry_)
            * maskCoefficient_;
        for (uint32_t node = 0u; node < currentMaskGain_.size(); ++node) {
            currentMaskGain_[node] += (targetMaskGain_[node]
                - currentMaskGain_[node]) * maskCoefficient_;
        }
        for (uint32_t channel = 0u;
            channel < currentChannelGain_.size(); ++channel) {
            currentChannelGain_[channel] += (targetChannelGain_[channel]
                - currentChannelGain_[channel]) * switchCoefficient_;
        }
        currentOutputGain_ += (dbToGain(outputTargetDb_.load(
                std::memory_order_relaxed)) - currentOutputGain_)
            * outputCoefficient_;
    }

    void updateMask()
    {
        targetMaskGain_.fill(1.0f);
        if (params_.maskAmount <= 0.0f) return;
        const Vec3 direction = directionFromAed(
            params_.maskAzimuthDeg, params_.maskElevationDeg);
        const uint32_t count = spatial_.activePickupCount();
        float maximum = 0.000001f;
        std::array<float, kAmbiEffectDjFilterMaxPickups> alignment {};
        for (uint32_t node = 0u; node < count; ++node) {
            const Vec3 nodeDirection = spatial_.nodeDirection(node);
            const float relation = nodeDirection.x * direction.x
                + nodeDirection.y * direction.y
                + nodeDirection.z * direction.z;
            alignment[node] = clamp((relation + 1.0f) * 0.5f, 0.0f, 1.0f);
            maximum = std::max(maximum, alignment[node]);
        }
        const float exponent = ambiEffectMaskExponent(params_.maskWidth,
            params_.maskCurve);
        for (uint32_t node = 0u; node < count; ++node) {
            const float directional = std::pow(clamp(
                alignment[node] / maximum, 0.0f, 1.0f), exponent);
            targetMaskGain_[node] = lerp(
                1.0f, directional, params_.maskAmount);
        }
    }

    void setChannelGainTargets(uint32_t order)
    {
        const uint32_t channels = std::min<uint32_t>(
            (order + 1u) * (order + 1u), targetChannelGain_.size());
        for (uint32_t channel = 0u;
            channel < targetChannelGain_.size(); ++channel) {
            targetChannelGain_[channel] = channel < channels ? 1.0f : 0.0f;
        }
    }

    void serviceSpatialTransition()
    {
        if (spatialTransitionStage_ == SpatialTransitionStage::Stable) return;
        if (spatialTransitionStage_ == SpatialTransitionStage::FadingOut) {
            spatialTransitionGain_ = std::max(
                0.0f, spatialTransitionGain_ - spatialTransitionStep_);
            if (spatialTransitionGain_ > 0.0f) return;
            const AmbiEffectBody previousBody = spatial_.resolvedBody();
            spatial_.setParams(pendingSpatialParams_);
            setChannelGainTargets(spatial_.params().order);
            updateMask();
            if (hasStoredResponse()
                && responsePreparationStage_ == ResponsePreparationStage::Idle
                && previousBody != spatial_.resolvedBody()) {
                beginConvolutionKernelRebuild();
            }
            spatialTransitionStage_ = SpatialTransitionStage::FadingIn;
            return;
        }
        if (kernelRebuildPending_) return;
        spatialTransitionGain_ = std::min(
            1.0f, spatialTransitionGain_ + spatialTransitionStep_);
        if (spatialTransitionGain_ >= 1.0f) {
            spatialTransitionGain_ = 1.0f;
            spatialTransitionStage_ = SpatialTransitionStage::Stable;
        }
    }

    void servicePendingClear()
    {
        if (!clearPending_ || currentResponseEnabled_ > 1.0e-4f) return;
        clearResponseImmediately();
    }

    void updateResponseGovernor(float peak)
    {
        constexpr float ceiling = 0.5f;
        const float target = std::isfinite(peak) && peak > ceiling
            ? ceiling / peak : 1.0f;
        if (!std::isfinite(responseGovernorGain_)
            || target < responseGovernorGain_) {
            responseGovernorGain_ = target;
        } else {
            responseGovernorGain_ += (1.0f - responseGovernorGain_)
                * responseGovernorRelease_;
        }
        responseGovernorGain_ = clamp(
            std::min(responseGovernorGain_, target), 0.0f, 1.0f);
    }

    void updateSafety(float peak)
    {
        constexpr float ceiling = 0.89125094f;
        constexpr float knee = 0.75f;
        float target = 1.0f;
        if (std::isfinite(peak) && peak > knee) {
            const float range = ceiling - knee;
            const float protectedPeak = knee + range * std::tanh(
                (peak - knee) / range);
            target = protectedPeak / peak;
        }
        if (!std::isfinite(safetyGain_) || target < safetyGain_) {
            safetyGain_ = target;
        } else {
            safetyGain_ += (1.0f - safetyGain_) * safetyRelease_;
        }
        safetyGain_ = clamp(std::min(safetyGain_, target), 0.0f, 1.0f);
    }

    double sampleRate_ = 48000.0;
    AmbiEffectResponseTraceParams params_ {};
    AmbiEffectDjFilter spatial_ {};
    response_trace_detail::PartitionedConvolutionBank convolution_ {};
    std::atomic<float> outputTargetDb_ { 0.0f };
    uint32_t maximumResponseFrames_ = 0u;
    std::array<std::vector<float>, kAmbiEffectDjFilterMaxPickups>
        captureBuffer_ {};
    std::array<std::vector<float>, kAmbiEffectDjFilterMaxPickups>
        responseBuffer_ {};
    uint32_t captureFrames_ = 0u;
    uint32_t captureTargetFrames_ = 0u;
    uint32_t responseFrames_ = 0u;
    double responseSampleRate_ = 0.0;
    AmbiEffectBody capturedBody_ = AmbiEffectBody::Auto;
    uint32_t capturedPickupCount_ = 0u;
    bool kernelRebuildPending_ = false;
    uint32_t kernelBuildNode_ = 0u;
    uint32_t kernelBuildPartition_ = 0u;
    uint32_t kernelBuildTargetCount_ = 0u;
    bool kernelBuildSucceeded_ = true;
    bool kernelsReady_ = false;
    ResponsePreparationStage responsePreparationStage_
        = ResponsePreparationStage::Idle;
    uint32_t preparationNode_ = 0u;
    uint32_t preparationFrame_ = 0u;
    double preparationSum_ = 0.0;
    double preparationNodeEnergy_ = 0.0;
    double preparationNodeAbsoluteSum_ = 0.0;
    double preparationMaximumEnergy_ = 0.0;
    double preparationMaximumAbsoluteSum_ = 0.0;
    float preparationPeak_ = 0.0f;
    float preparationMean_ = 0.0f;
    float preparationHighpassState_ = 0.0f;
    float preparationPrevious_ = 0.0f;
    float preparationScale_ = 1.0f;
    bool capturing_ = false;
    bool capturePending_ = false;
    bool clearPending_ = false;
    AmbiEffectBody captureBody_ = AmbiEffectBody::Auto;
    uint32_t capturePickupCount_ = 0u;
    uint32_t captureOrder_ = 0u;
    std::array<std::vector<float>, kAmbiEffectDjFilterMaxChannels> dryDelay_;
    uint32_t dryPosition_ = 0u;
    std::array<float, kAmbiEffectDjFilterMaxPickups> toneState_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> responseSlewState_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> targetMaskGain_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> currentMaskGain_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> nodeLevel_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> nodeResponseLevel_ {};
    std::array<float, kAmbiEffectDjFilterMaxChannels> targetChannelGain_ {};
    std::array<float, kAmbiEffectDjFilterMaxChannels> currentChannelGain_ {};
    AmbiEffectDjFilterParams pendingSpatialParams_ {};
    SpatialTransitionStage spatialTransitionStage_
        = SpatialTransitionStage::Stable;
    bool prepared_ = false;
    AmbiEffectTopology targetTopology_ = AmbiEffectTopology::Local;
    std::array<float, 4u> currentTopologyWeights_ { 1.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, 4u> targetTopologyWeights_ { 1.0f, 0.0f, 0.0f, 0.0f };
    float spatialTransitionGain_ = 1.0f;
    float spatialTransitionStep_ = 0.001f;
    float roamingPhase_ = 0.0f;
    float currentTopologyAmount_ = 0.65f;
    float currentRoamingRateHz_ = 0.08f;
    float currentMix_ = 0.50f;
    float currentResponseGain_ = 0.50f;
    float currentResponseEnabled_ = 0.0f;
    float currentTone_ = 0.72f;
    float currentMaskDry_ = 1.0f;
    float currentOutputGain_ = 1.0f;
    float responseGovernorGain_ = 1.0f;
    float safetyGain_ = 1.0f;
    float excitationGain_ = 1.0f;
    float responseGovernorRelease_ = 0.0001f;
    float safetyRelease_ = 0.0001f;
    float excitationRelease_ = 0.0001f;
    float outputCoefficient_ = 0.001f;
    float globalCoefficient_ = 0.001f;
    float switchCoefficient_ = 0.001f;
    float maskCoefficient_ = 0.001f;
    float toneCoefficient_ = 0.001f;
    float levelCoefficient_ = 0.001f;
    float topologyCoefficient_ = 0.0015f;
};

} // namespace s3g
