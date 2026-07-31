#pragma once

#include "s3g_ambi_effect_dj_filter.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#if defined(__APPLE__)
#define S3G_HAS_PARTIAL_TRACE_FFT 1
#include <Accelerate/Accelerate.h>
#else
#define S3G_HAS_PARTIAL_TRACE_FFT 0
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kPartialTraceMaxPartials = 16u;
constexpr uint32_t kPartialTraceAnalysisSize = 2048u;
constexpr uint32_t kPartialTraceAnalysisHop = 512u;
constexpr uint32_t kPartialTraceBins = kPartialTraceAnalysisSize / 2u + 1u;

struct AmbiEffectPartialTraceParams {
    uint32_t order = 7u;
    AmbiEffectBody body = AmbiEffectBody::Auto;
    AmbiEffectTopology topology = AmbiEffectTopology::Local;
    uint32_t enabled = 1u;
    uint32_t partialCount = 10u;
    float sensitivity = 0.62f;
    float minimumFrequencyHz = 55.0f;
    float maximumFrequencyHz = 12000.0f;
    float trackingMs = 120.0f;
    float releaseMs = 420.0f;
    float transposeSemitones = 0.0f;
    float traceGainDb = -3.0f;
    float topologyAmount = 0.65f;
    float roamingRateHz = 0.08f;
    float mix = 0.55f;
    float outputGainDb = 0.0f;
    float maskAmount = 0.0f;
    float maskAzimuthDeg = 0.0f;
    float maskElevationDeg = 0.0f;
    float maskWidth = 0.35f;
    float maskCurve = 0.5f;
    float maskDry = 1.0f;
    uint32_t freeze = 0u;
    float smear = 0.0f;
};

struct PartialTraceFrozenVoiceState {
    uint32_t active = 0u;
    float sourceFrequency = 440.0f;
    float currentFrequency = 440.0f;
    float amplitude = 0.0f;
    float phase = 0.0f;
    std::array<float, kAmbiEffectDjFilterMaxPickups> ownership {};
};

inline AmbiEffectPartialTraceParams sanitizeAmbiEffectPartialTraceParams(
    AmbiEffectPartialTraceParams params)
{
    params.order = std::clamp<uint32_t>(params.order, 1u,
        kAmbiEffectDjFilterMaxOrder);
    params.body = static_cast<AmbiEffectBody>(std::min<uint32_t>(
        static_cast<uint32_t>(params.body), 5u));
    if (params.body == AmbiEffectBody::Tetra4
        || params.body == AmbiEffectBody::Cube8) {
        params.body = AmbiEffectBody::Icosa12;
    }
    params.topology = static_cast<AmbiEffectTopology>(std::min<uint32_t>(
        static_cast<uint32_t>(params.topology), 3u));
    params.enabled = params.enabled ? 1u : 0u;
    params.partialCount = std::clamp<uint32_t>(params.partialCount, 1u,
        kPartialTraceMaxPartials);
    params.sensitivity = clamp(params.sensitivity, 0.0f, 1.0f);
    params.minimumFrequencyHz = clamp(params.minimumFrequencyHz, 20.0f,
        4000.0f);
    params.maximumFrequencyHz = clamp(params.maximumFrequencyHz, 200.0f,
        20000.0f);
    if (params.maximumFrequencyHz < params.minimumFrequencyHz + 20.0f) {
        params.maximumFrequencyHz = params.minimumFrequencyHz + 20.0f;
    }
    params.trackingMs = clamp(params.trackingMs, 20.0f, 1000.0f);
    params.releaseMs = clamp(params.releaseMs, 40.0f, 4000.0f);
    params.transposeSemitones = clamp(params.transposeSemitones, -24.0f, 24.0f);
    params.traceGainDb = clamp(params.traceGainDb, -36.0f, 12.0f);
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
    params.freeze = params.freeze ? 1u : 0u;
    params.smear = clamp(params.smear, 0.0f, 1.0f);
    return params;
}

class AmbiEffectPartialTrace {
private:
    enum class SpatialTransitionStage : uint32_t {
        Stable = 0u,
        FadingOut,
        FadingIn,
    };

    struct Partial {
        bool active = false;
        uint32_t age = 0u;
        float sourceFrequency = 440.0f;
        float targetFrequency = 440.0f;
        float currentFrequency = 440.0f;
        float targetAmplitude = 0.0f;
        float currentAmplitude = 0.0f;
        float phase = 0.0f;
        std::array<float, kAmbiEffectDjFilterMaxPickups> targetOwnership {};
        std::array<float, kAmbiEffectDjFilterMaxPickups> currentOwnership {};
    };

public:
    AmbiEffectPartialTrace() = default;
    ~AmbiEffectPartialTrace() { releaseFft(); }
    AmbiEffectPartialTrace(const AmbiEffectPartialTrace&) = delete;
    AmbiEffectPartialTrace& operator=(const AmbiEffectPartialTrace&) = delete;

    bool prepare(double sampleRate)
    {
        const bool restoreFrozen = params_.freeze != 0u
            && activePartialCount_ > 0u;
        const AmbiEffectBody frozenBody = spatial_.resolvedBody();
        std::array<PartialTraceFrozenVoiceState,
            kPartialTraceMaxPartials> frozenVoices {};
        if (restoreFrozen) {
            for (uint32_t index = 0u; index < frozenVoices.size(); ++index) {
                frozenVoices[index] = frozenVoiceState(index);
            }
        }
        prepared_ = false;
        sampleRate_ = std::clamp(std::isfinite(sampleRate) ? sampleRate : 48000.0,
            1000.0, 768000.0);
        safetyRelease_ = onePole(0.300f);
        traceGovernorRelease_ = onePole(0.250f);
        outputCoefficient_ = onePole(0.025f);
        globalCoefficient_ = onePole(0.025f);
        switchCoefficient_ = onePole(0.015f);
        maskCoefficient_ = onePole(0.020f);
        levelCoefficient_ = onePole(0.055f);
        attackCoefficient_ = onePole(0.022f);
        spatialTransitionStep_ = 1.0f / std::max(1.0f,
            static_cast<float>(sampleRate_ * 0.012));
        spatial_.setParams(spatialParams(params_));
        spatial_.prepare(sampleRate_);
#if S3G_HAS_PARTIAL_TRACE_FFT
        releaseFft();
        fftSetup_ = vDSP_create_fftsetup(11u, kFFTRadix2);
        if (!fftSetup_) return false;
#endif
        for (uint32_t index = 0u; index < kPartialTraceAnalysisSize; ++index) {
            analysisWindow_[index] = 0.5f - 0.5f * std::cos(
                2.0f * kPi * static_cast<float>(index)
                / static_cast<float>(kPartialTraceAnalysisSize));
        }
        setParams(params_);
        reset();
        if (restoreFrozen) {
            restoreFrozenState(frozenBody, frozenVoices.data(),
                static_cast<uint32_t>(frozenVoices.size()));
        }
        prepared_ = true;
        return S3G_HAS_PARTIAL_TRACE_FFT != 0;
    }

    void reset()
    {
        const bool preserveFrozen = params_.freeze != 0u
            && activePartialCount_ > 0u;
        const AmbiEffectBody frozenBody = spatial_.resolvedBody();
        std::array<PartialTraceFrozenVoiceState,
            kPartialTraceMaxPartials> frozenVoices {};
        if (preserveFrozen) {
            for (uint32_t index = 0u; index < frozenVoices.size(); ++index) {
                frozenVoices[index] = frozenVoiceState(index);
            }
        }
        spatial_.setParams(spatialParams(params_));
        spatial_.reset();
        analysisRing_.fill(0.0f);
        for (auto& ring : listenerRing_) ring.fill(0.0f);
        analysisWrite_ = 0u;
        samplesUntilAnalysis_ = kPartialTraceAnalysisSize;
        roamingPhase_ = 0.0f;
        topologyFade_ = 1.0f;
        previousTopology_ = targetTopology_ = params_.topology;
        currentTopologyAmount_ = params_.topologyAmount;
        currentRoamingRateHz_ = params_.roamingRateHz;
        currentMix_ = params_.mix;
        currentTraceGain_ = dbToGain(params_.traceGainDb);
        currentEnabled_ = params_.enabled ? 1.0f : 0.0f;
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
        traceGovernorGain_ = 1.0f;
        safetyGain_ = 1.0f;
        nodeLevel_.fill(0.0f);
        nodeTraceLevel_.fill(0.0f);
        for (auto& partial : partials_) partial = Partial {};
        activePartialCount_ = 0u;
        strongestFrequencyHz_ = 0.0f;
        if (preserveFrozen) {
            restoreFrozenState(frozenBody, frozenVoices.data(),
                static_cast<uint32_t>(frozenVoices.size()));
        }
    }

    void setParams(AmbiEffectPartialTraceParams params)
    {
        const auto next = sanitizeAmbiEffectPartialTraceParams(params);
        const auto nextSpatial = spatialParams(next);
        const auto& currentSpatial = spatial_.params();
        const bool spatialChanged = nextSpatial.order != currentSpatial.order
            || resolveAmbiEffectBody(nextSpatial.body, nextSpatial.order)
                != resolveAmbiEffectBody(currentSpatial.body,
                    currentSpatial.order);
        if (next.topology != targetTopology_) {
            previousTopology_ = targetTopology_;
            targetTopology_ = next.topology;
            topologyFade_ = 0.0f;
        }
        const bool freezeStarted = next.freeze != 0u && params_.freeze == 0u;
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
            } else if (spatialTransitionStage_
                == SpatialTransitionStage::FadingOut) {
                spatialTransitionStage_ = SpatialTransitionStage::FadingIn;
            }
        }
        if (freezeStarted) freezeCurrentTrace();
        updateTransposedFrequencies();
        updateTraceCoefficients();
        updateMask();
    }

    const AmbiEffectPartialTraceParams& params() const { return params_; }

    void setOutputGainTarget(float outputGainDb)
    {
        outputTargetDb_.store(clamp(std::isfinite(outputGainDb)
            ? outputGainDb : 0.0f, -60.0f, 12.0f),
            std::memory_order_relaxed);
    }

    AmbiEffectBody resolvedBody() const { return spatial_.resolvedBody(); }
    uint32_t activePickupCount() const { return spatial_.activePickupCount(); }
    uint32_t activePartialCount() const { return activePartialCount_; }
    float strongestFrequencyHz() const { return strongestFrequencyHz_; }
    float roamingPhase() const { return roamingPhase_; }
    float traceGovernorGain() const { return traceGovernorGain_; }
    float safetyGain() const { return safetyGain_; }
    float nodeLevel(uint32_t node) const
    {
        return node < nodeLevel_.size() ? nodeLevel_[node] : 0.0f;
    }
    float nodeTraceLevel(uint32_t node) const
    {
        return node < nodeTraceLevel_.size() ? nodeTraceLevel_[node] : 0.0f;
    }
    float nodeWetMask(uint32_t node) const
    {
        return node < currentMaskGain_.size()
            ? currentMaskGain_[node] : 1.0f;
    }

    PartialTraceFrozenVoiceState frozenVoiceState(uint32_t index) const
    {
        PartialTraceFrozenVoiceState state {};
        if (index >= partials_.size()) return state;
        const Partial& partial = partials_[index];
        state.active = partial.active ? 1u : 0u;
        state.sourceFrequency = partial.sourceFrequency;
        state.currentFrequency = partial.currentFrequency;
        state.amplitude = partial.currentAmplitude;
        state.phase = partial.phase;
        state.ownership = partial.currentOwnership;
        return state;
    }

    void restoreFrozenState(AmbiEffectBody savedBody,
        const PartialTraceFrozenVoiceState* voices, uint32_t count)
    {
        for (auto& partial : partials_) partial = Partial {};
        activePartialCount_ = 0u;
        strongestFrequencyHz_ = 0.0f;
        if (!voices || count == 0u) return;
        count = std::min<uint32_t>(count, partials_.size());
        float strongestAmplitude = 0.0f;
        for (uint32_t index = 0u; index < count; ++index) {
            const auto& state = voices[index];
            if (!state.active || !std::isfinite(state.sourceFrequency)
                || !std::isfinite(state.currentFrequency)
                || !std::isfinite(state.amplitude)
                || !std::isfinite(state.phase)) continue;
            auto& partial = partials_[index];
            partial.active = true;
            partial.age = 1u;
            partial.sourceFrequency = clamp(
                state.sourceFrequency, 20.0f,
                static_cast<float>(sampleRate_ * 0.46));
            partial.currentFrequency = clamp(
                state.currentFrequency, 20.0f,
                static_cast<float>(sampleRate_ * 0.46));
            partial.targetFrequency = partial.currentFrequency;
            partial.currentAmplitude = clamp(state.amplitude, 0.0f, 4.0f);
            partial.targetAmplitude = partial.currentAmplitude;
            partial.phase = state.phase - std::floor(state.phase);
            for (uint32_t node = 0u; node < state.ownership.size(); ++node) {
                const float value = std::isfinite(state.ownership[node])
                    ? state.ownership[node] : 0.0f;
                partial.currentOwnership[node] = clamp(value, 0.0f, 1.0f);
                partial.targetOwnership[node] = partial.currentOwnership[node];
            }
            if (partial.currentAmplitude > strongestAmplitude) {
                strongestAmplitude = partial.currentAmplitude;
                strongestFrequencyHz_ = partial.currentFrequency;
            }
            ++activePartialCount_;
        }
        savedBody = resolveAmbiEffectBody(savedBody, params_.order);
        remapOwnership(savedBody, spatial_.resolvedBody());
        updateTransposedFrequencies();
    }

    template <typename Sample>
    void process(Sample** input, Sample** output, uint32_t inputChannels,
        uint32_t outputChannels, uint32_t frames)
    {
        if (!output) return;
        const uint32_t inCount = std::min<uint32_t>(inputChannels,
            kAmbiEffectDjFilterMaxChannels);
        const uint32_t outCount = std::min<uint32_t>(outputChannels,
            kAmbiEffectDjFilterMaxChannels);
        std::array<float, kAmbiEffectDjFilterMaxChannels> field {};
        std::array<float, kAmbiEffectDjFilterMaxChannels> correction {};
        std::array<float, kAmbiEffectDjFilterMaxChannels> outputFrame {};
        std::array<float, kAmbiEffectDjFilterMaxPickups> ears {};
        std::array<float, kAmbiEffectDjFilterMaxPickups> traced {};
        std::array<float, kAmbiEffectDjFilterMaxPickups> routedFrom {};
        std::array<float, kAmbiEffectDjFilterMaxPickups> routedTo {};
        std::array<float, kAmbiEffectDjFilterMaxPickups> delta {};

        for (uint32_t frameIndex = 0u; frameIndex < frames; ++frameIndex) {
            smoothGlobals();
            serviceSpatialTransition();
            const uint32_t channels = spatial_.spatialChannelCount();
            const uint32_t pickupCount = spatial_.activePickupCount();
            roamingPhase_ += currentRoamingRateHz_
                / static_cast<float>(sampleRate_);
            roamingPhase_ -= std::floor(roamingPhase_);
            topologyFade_ += (1.0f - topologyFade_) * topologyCoefficient_;
            if (topologyFade_ > 0.99999f) {
                topologyFade_ = 1.0f;
                previousTopology_ = targetTopology_;
            }

            for (uint32_t channel = 0u;
                channel < kAmbiEffectDjFilterMaxChannels; ++channel) {
                const float value = channel < inCount && input && input[channel]
                    ? static_cast<float>(input[channel][frameIndex]) : 0.0f;
                field[channel] = std::isfinite(value) ? value : 0.0f;
            }
            spatial_.decodeField(field.data(), channels, ears);
            for (uint32_t node = 0u; node < pickupCount; ++node) {
                nodeLevel_[node] += (std::abs(ears[node]) - nodeLevel_[node])
                    * levelCoefficient_;
            }
            pushAnalysis(field[0], ears, pickupCount);
            synthesize(traced, pickupCount);

            spatial_.routeNodes(traced, routedFrom, previousTopology_,
                roamingPhase_);
            spatial_.routeNodes(traced, routedTo, targetTopology_,
                roamingPhase_);
            delta.fill(0.0f);
            for (uint32_t node = 0u; node < pickupCount; ++node) {
                const float routed = lerp(routedFrom[node], routedTo[node],
                    topologyFade_);
                const float processed = lerp(traced[node], routed,
                    currentTopologyAmount_);
                const float target = lerp(ears[node] * currentMaskDry_,
                    processed, currentMaskGain_[node]);
                delta[node] = target - ears[node];
            }
            correction.fill(0.0f);
            spatial_.encodeNodes(delta, correction.data(), channels);

            float peak = 0.0f;
            const float wetGain = currentMix_ * currentEnabled_
                * spatialTransitionGain_;
            for (uint32_t channel = 0u; channel < outCount; ++channel) {
                const float wet = channel < channels
                    ? correction[channel] * wetGain : 0.0f;
                const float value = (field[channel]
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
        }
    }

private:
    struct Candidate {
        uint32_t bin = 0u;
        float frequency = 0.0f;
        float amplitude = 0.0f;
        float magnitude = 0.0f;
    };

    float onePole(float seconds) const
    {
        return 1.0f - std::exp(-1.0f
            / std::max(1.0f, static_cast<float>(sampleRate_ * seconds)));
    }

    static AmbiEffectDjFilterParams spatialParams(
        const AmbiEffectPartialTraceParams& params)
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

    void pushAnalysis(float omni,
        const std::array<float, kAmbiEffectDjFilterMaxPickups>& ears,
        uint32_t pickupCount)
    {
        analysisRing_[analysisWrite_] = flushDenormal(omni);
        for (uint32_t node = 0u; node < pickupCount; ++node) {
            listenerRing_[node][analysisWrite_] = ears[node];
        }
        analysisWrite_ = (analysisWrite_ + 1u) % kPartialTraceAnalysisSize;
        if (samplesUntilAnalysis_ > 0u) --samplesUntilAnalysis_;
        if (samplesUntilAnalysis_ == 0u) {
            if (!params_.freeze) analyze();
            samplesUntilAnalysis_ = kPartialTraceAnalysisHop;
        }
    }

    void analyze()
    {
#if S3G_HAS_PARTIAL_TRACE_FFT
        for (uint32_t index = 0u; index < kPartialTraceAnalysisSize; ++index) {
            const uint32_t source = (analysisWrite_ + index)
                % kPartialTraceAnalysisSize;
            fftTime_[index] = analysisRing_[source] * analysisWindow_[index];
        }
        DSPSplitComplex split { fftReal_.data(), fftImag_.data() };
        vDSP_ctoz(reinterpret_cast<const DSPComplex*>(fftTime_.data()), 2,
            &split, 1, kPartialTraceAnalysisSize / 2u);
        vDSP_fft_zrip(fftSetup_, &split, 1, 11u, FFT_FORWARD);
        magnitudes_[0] = std::abs(fftReal_[0]);
        for (uint32_t bin = 1u; bin < kPartialTraceBins - 1u; ++bin) {
            magnitudes_[bin] = std::hypot(fftReal_[bin], fftImag_[bin]);
        }
        magnitudes_[kPartialTraceBins - 1u] = std::abs(fftImag_[0]);

        const uint32_t firstBin = std::max<uint32_t>(1u,
            static_cast<uint32_t>(std::ceil(params_.minimumFrequencyHz
                * kPartialTraceAnalysisSize / sampleRate_)));
        const uint32_t lastBin = std::min<uint32_t>(kPartialTraceBins - 2u,
            static_cast<uint32_t>(std::floor(params_.maximumFrequencyHz
                * kPartialTraceAnalysisSize / sampleRate_)));
        float maximum = 0.0f;
        for (uint32_t bin = firstBin; bin <= lastBin; ++bin) {
            maximum = std::max(maximum, magnitudes_[bin]);
        }
        const float relativeDb = lerp(-28.0f, -68.0f, params_.sensitivity);
        const float threshold = std::max(1.0e-7f,
            maximum * std::pow(10.0f, relativeDb / 20.0f));
        std::array<Candidate, 64u> candidates {};
        uint32_t candidateCount = 0u;
        for (uint32_t bin = firstBin; bin <= lastBin
            && candidateCount < candidates.size(); ++bin) {
            const float magnitude = magnitudes_[bin];
            if (magnitude < threshold || magnitude <= magnitudes_[bin - 1u]
                || magnitude < magnitudes_[bin + 1u]) continue;
            const float left = magnitudes_[bin - 1u];
            const float right = magnitudes_[bin + 1u];
            const float divisor = left - 2.0f * magnitude + right;
            const float offset = std::abs(divisor) > 1.0e-9f
                ? clamp(0.5f * (left - right) / divisor, -0.5f, 0.5f)
                : 0.0f;
            Candidate candidate {};
            candidate.bin = bin;
            candidate.frequency = (static_cast<float>(bin) + offset)
                * static_cast<float>(sampleRate_)
                / static_cast<float>(kPartialTraceAnalysisSize);
            candidate.magnitude = magnitude;
            candidate.amplitude = clamp(4.0f * magnitude
                / static_cast<float>(kPartialTraceAnalysisSize), 0.0f, 4.0f);
            candidates[candidateCount++] = candidate;
        }
        std::sort(candidates.begin(), candidates.begin() + candidateCount,
            [](const Candidate& a, const Candidate& b) {
                return a.magnitude > b.magnitude;
            });
        candidateCount = std::min<uint32_t>(candidateCount,
            params_.partialCount);
        updateTracks(candidates, candidateCount);
#endif
    }

    void updateTracks(const std::array<Candidate, 64u>& candidates,
        uint32_t candidateCount)
    {
        std::array<bool, kPartialTraceMaxPartials> claimed {};
        for (auto& partial : partials_) partial.targetAmplitude = 0.0f;
        strongestFrequencyHz_ = candidateCount > 0u ? candidates[0].frequency : 0.0f;

        for (uint32_t candidateIndex = 0u; candidateIndex < candidateCount;
            ++candidateIndex) {
            const Candidate& candidate = candidates[candidateIndex];
            uint32_t selected = kPartialTraceMaxPartials;
            const float matchingCents = lerp(90.0f, 18.0f, params_.smear);
            float bestCents = matchingCents;
            bool replaceExisting = false;
            for (uint32_t index = 0u; index < partials_.size(); ++index) {
                if (!partials_[index].active || claimed[index]) continue;
                const float cents = std::abs(1200.0f * std::log2(
                    candidate.frequency
                    / std::max(1.0f, partials_[index].sourceFrequency)));
                if (cents < bestCents) {
                    bestCents = cents;
                    selected = index;
                }
            }
            if (selected == kPartialTraceMaxPartials) {
                for (uint32_t index = 0u; index < partials_.size(); ++index) {
                    if (!partials_[index].active && !claimed[index]) {
                        selected = index;
                        break;
                    }
                }
            }
            if (selected == kPartialTraceMaxPartials
                && params_.smear > 0.001f) {
                float quietest = 1000000.0f;
                for (uint32_t index = 0u; index < partials_.size(); ++index) {
                    if (claimed[index]) continue;
                    const float level = std::max(partials_[index].currentAmplitude,
                        partials_[index].targetAmplitude);
                    if (level < quietest) {
                        quietest = level;
                        selected = index;
                        replaceExisting = true;
                    }
                }
            }
            if (selected == kPartialTraceMaxPartials) continue;
            auto& partial = partials_[selected];
            if (replaceExisting) partial = Partial {};
            if (!partial.active) {
                partial = Partial {};
                partial.active = true;
                partial.sourceFrequency = candidate.frequency;
                partial.currentFrequency = candidate.frequency;
                partial.phase = 0.0f;
            }
            claimed[selected] = true;
            partial.sourceFrequency = candidate.frequency;
            partial.targetFrequency = candidate.frequency * std::pow(
                2.0f, params_.transposeSemitones / 12.0f);
            partial.targetFrequency = clamp(partial.targetFrequency, 20.0f,
                static_cast<float>(sampleRate_ * 0.46));
            partial.targetAmplitude = candidate.amplitude;
            ++partial.age;
            estimateOwnership(candidate.frequency, partial.targetOwnership);
        }
        activePartialCount_ = 0u;
        for (auto& partial : partials_) {
            if (partial.active) ++activePartialCount_;
        }
    }

    void estimateOwnership(float frequency,
        std::array<float, kAmbiEffectDjFilterMaxPickups>& ownership) const
    {
        ownership.fill(0.0f);
        const uint32_t count = spatial_.activePickupCount();
        if (count == 0u) return;
        std::array<float, kAmbiEffectDjFilterMaxPickups> real {};
        std::array<float, kAmbiEffectDjFilterMaxPickups> imag {};
        const float omega = 2.0f * kPi * frequency
            / static_cast<float>(sampleRate_);
        const float rotationReal = std::cos(omega);
        const float rotationImag = -std::sin(omega);
        float oscillatorReal = 1.0f;
        float oscillatorImag = 0.0f;
        for (uint32_t index = 0u; index < kPartialTraceAnalysisSize; ++index) {
            const uint32_t source = (analysisWrite_ + index)
                % kPartialTraceAnalysisSize;
            const float window = analysisWindow_[index];
            for (uint32_t node = 0u; node < count; ++node) {
                const float value = listenerRing_[node][source] * window;
                real[node] += value * oscillatorReal;
                imag[node] += value * oscillatorImag;
            }
            const float nextReal = oscillatorReal * rotationReal
                - oscillatorImag * rotationImag;
            oscillatorImag = oscillatorReal * rotationImag
                + oscillatorImag * rotationReal;
            oscillatorReal = nextReal;
        }
        float maximum = 1.0e-9f;
        for (uint32_t node = 0u; node < count; ++node) {
            maximum = std::max(maximum, std::hypot(real[node], imag[node]));
        }
        for (uint32_t node = 0u; node < count; ++node) {
            const float normalized = std::hypot(real[node], imag[node]) / maximum;
            ownership[node] = normalized * normalized
                * (3.0f - 2.0f * normalized);
        }
    }

    void synthesize(
        std::array<float, kAmbiEffectDjFilterMaxPickups>& traced,
        uint32_t pickupCount)
    {
        traced.fill(0.0f);
        const float traceGain = currentTraceGain_;
        activePartialCount_ = 0u;
        for (auto& partial : partials_) {
            if (!partial.active) continue;
            partial.currentFrequency += (partial.targetFrequency
                - partial.currentFrequency) * frequencyCoefficient_;
            const float amplitudeCoefficient = partial.targetAmplitude
                    > partial.currentAmplitude
                ? attackCoefficient_ : releaseCoefficient_;
            partial.currentAmplitude += (partial.targetAmplitude
                - partial.currentAmplitude) * amplitudeCoefficient;
            partial.phase += partial.currentFrequency
                / static_cast<float>(sampleRate_);
            partial.phase -= std::floor(partial.phase);
            const float sample = std::sin(2.0f * kPi * partial.phase)
                * partial.currentAmplitude * traceGain;
            for (uint32_t node = 0u; node < pickupCount; ++node) {
                partial.currentOwnership[node] += (
                    partial.targetOwnership[node]
                    - partial.currentOwnership[node]) * ownershipCoefficient_;
                traced[node] += sample * partial.currentOwnership[node];
            }
            if (partial.targetAmplitude <= 0.0f
                && partial.currentAmplitude < 1.0e-6f) {
                partial = Partial {};
            } else {
                ++activePartialCount_;
            }
        }
        float tracePeak = 0.0f;
        for (uint32_t node = 0u; node < pickupCount; ++node) {
            if (!std::isfinite(traced[node])) traced[node] = 0.0f;
            tracePeak = std::max(tracePeak, std::abs(traced[node]));
        }
        updateTraceGovernor(tracePeak);
        for (uint32_t node = 0u; node < pickupCount; ++node) {
            traced[node] = std::tanh(traced[node] * traceGovernorGain_);
            nodeTraceLevel_[node] += (std::abs(traced[node])
                - nodeTraceLevel_[node]) * levelCoefficient_;
        }
        for (uint32_t node = pickupCount; node < nodeTraceLevel_.size(); ++node) {
            nodeTraceLevel_[node] += (0.0f - nodeTraceLevel_[node])
                * levelCoefficient_;
        }
    }

    void smoothGlobals()
    {
        currentTopologyAmount_ += (params_.topologyAmount
            - currentTopologyAmount_) * globalCoefficient_;
        currentRoamingRateHz_ += (params_.roamingRateHz
            - currentRoamingRateHz_) * globalCoefficient_;
        currentMix_ += (params_.mix - currentMix_) * globalCoefficient_;
        currentTraceGain_ += (dbToGain(params_.traceGainDb)
            - currentTraceGain_) * globalCoefficient_;
        currentEnabled_ += ((params_.enabled ? 1.0f : 0.0f)
            - currentEnabled_) * switchCoefficient_;
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
            if (params_.freeze) {
                remapOwnership(previousBody, spatial_.resolvedBody());
            } else {
                clearOwnership();
            }
            setChannelGainTargets(spatial_.params().order);
            updateMask();
            spatialTransitionStage_ = SpatialTransitionStage::FadingIn;
            return;
        }
        spatialTransitionGain_ = std::min(
            1.0f, spatialTransitionGain_ + spatialTransitionStep_);
        if (spatialTransitionGain_ >= 1.0f) {
            spatialTransitionGain_ = 1.0f;
            spatialTransitionStage_ = SpatialTransitionStage::Stable;
        }
    }

    void updateTraceGovernor(float peak)
    {
        constexpr float ceiling = 0.5f;
        const float target = peak > ceiling ? ceiling / peak : 1.0f;
        if (!std::isfinite(traceGovernorGain_) || target < traceGovernorGain_) {
            traceGovernorGain_ = target;
        } else {
            traceGovernorGain_ += (1.0f - traceGovernorGain_)
                * traceGovernorRelease_;
        }
        traceGovernorGain_ = clamp(
            std::min(traceGovernorGain_, target), 0.0f, 1.0f);
    }

    void updateSafety(float peak)
    {
        constexpr float ceiling = 0.89125094f;
        const float target = peak > ceiling ? ceiling / peak : 1.0f;
        if (!std::isfinite(safetyGain_) || target < safetyGain_) {
            safetyGain_ = target;
        } else {
            safetyGain_ += (1.0f - safetyGain_) * safetyRelease_;
        }
        safetyGain_ = clamp(std::min(safetyGain_, target), 0.0f, 1.0f);
    }

    void clearOwnership()
    {
        for (auto& partial : partials_) {
            partial.targetOwnership.fill(0.0f);
            partial.currentOwnership.fill(0.0f);
        }
    }

    void updateTraceCoefficients()
    {
        const float smearCurve = params_.smear * params_.smear;
        frequencyCoefficient_ = onePole(params_.trackingMs * 0.001f
            * lerp(1.0f, 3.0f, smearCurve));
        const float releaseSeconds = std::min(30.0f,
            params_.releaseMs * 0.001f * std::pow(30.0f, params_.smear));
        releaseCoefficient_ = onePole(releaseSeconds);
        ownershipCoefficient_ = onePole(
            lerp(0.060f, 1.5f, smearCurve));
    }

    void freezeCurrentTrace()
    {
        const float transpose = std::pow(
            2.0f, params_.transposeSemitones / 12.0f);
        for (auto& partial : partials_) {
            if (!partial.active) continue;
            partial.sourceFrequency = partial.currentFrequency
                / std::max(0.000001f, transpose);
            partial.targetFrequency = partial.currentFrequency;
            partial.targetAmplitude = partial.currentAmplitude;
            partial.targetOwnership = partial.currentOwnership;
        }
    }

    void updateTransposedFrequencies()
    {
        const float transpose = std::pow(
            2.0f, params_.transposeSemitones / 12.0f);
        for (auto& partial : partials_) {
            if (!partial.active) continue;
            partial.targetFrequency = clamp(
                partial.sourceFrequency * transpose, 20.0f,
                static_cast<float>(sampleRate_ * 0.46));
        }
    }

    void remapOwnership(AmbiEffectBody previousBody, AmbiEffectBody nextBody)
    {
        if (previousBody == nextBody) return;
        const auto previousDirections = ambiEffectBodyDirections(previousBody);
        const auto nextDirections = ambiEffectBodyDirections(nextBody);
        const uint32_t previousCount = ambiEffectBodyPickupCount(previousBody);
        const uint32_t nextCount = ambiEffectBodyPickupCount(nextBody);
        for (auto& partial : partials_) {
            if (!partial.active) continue;
            const auto previousTarget = partial.targetOwnership;
            const auto previousCurrent = partial.currentOwnership;
            partial.targetOwnership.fill(0.0f);
            partial.currentOwnership.fill(0.0f);
            for (uint32_t node = 0u; node < nextCount; ++node) {
                uint32_t nearest = 0u;
                float bestDot = -2.0f;
                for (uint32_t candidate = 0u;
                    candidate < previousCount; ++candidate) {
                    const float relation = nextDirections[node].x
                            * previousDirections[candidate].x
                        + nextDirections[node].y
                            * previousDirections[candidate].y
                        + nextDirections[node].z
                            * previousDirections[candidate].z;
                    if (relation > bestDot) {
                        bestDot = relation;
                        nearest = candidate;
                    }
                }
                partial.targetOwnership[node] = previousTarget[nearest];
                partial.currentOwnership[node] = previousCurrent[nearest];
            }
        }
    }

    void releaseFft()
    {
#if S3G_HAS_PARTIAL_TRACE_FFT
        if (fftSetup_) vDSP_destroy_fftsetup(fftSetup_);
        fftSetup_ = nullptr;
#endif
    }

    double sampleRate_ = 48000.0;
    AmbiEffectPartialTraceParams params_ {};
    AmbiEffectDjFilter spatial_ {};
    std::atomic<float> outputTargetDb_ { 0.0f };
    std::array<Partial, kPartialTraceMaxPartials> partials_ {};
    std::array<float, kPartialTraceAnalysisSize> analysisRing_ {};
    std::array<std::array<float, kPartialTraceAnalysisSize>,
        kAmbiEffectDjFilterMaxPickups> listenerRing_ {};
    std::array<float, kPartialTraceAnalysisSize> analysisWindow_ {};
    std::array<float, kPartialTraceAnalysisSize> fftTime_ {};
    std::array<float, kPartialTraceAnalysisSize / 2u> fftReal_ {};
    std::array<float, kPartialTraceAnalysisSize / 2u> fftImag_ {};
    std::array<float, kPartialTraceBins> magnitudes_ {};
#if S3G_HAS_PARTIAL_TRACE_FFT
    FFTSetup fftSetup_ = nullptr;
#endif
    uint32_t analysisWrite_ = 0u;
    uint32_t samplesUntilAnalysis_ = kPartialTraceAnalysisSize;
    uint32_t activePartialCount_ = 0u;
    float strongestFrequencyHz_ = 0.0f;
    std::array<float, kAmbiEffectDjFilterMaxPickups> targetMaskGain_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> currentMaskGain_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> nodeLevel_ {};
    std::array<float, kAmbiEffectDjFilterMaxPickups> nodeTraceLevel_ {};
    std::array<float, kAmbiEffectDjFilterMaxChannels> targetChannelGain_ {};
    std::array<float, kAmbiEffectDjFilterMaxChannels> currentChannelGain_ {};
    AmbiEffectDjFilterParams pendingSpatialParams_ {};
    SpatialTransitionStage spatialTransitionStage_
        = SpatialTransitionStage::Stable;
    bool prepared_ = false;
    AmbiEffectTopology previousTopology_ = AmbiEffectTopology::Local;
    AmbiEffectTopology targetTopology_ = AmbiEffectTopology::Local;
    float topologyFade_ = 1.0f;
    float spatialTransitionGain_ = 1.0f;
    float spatialTransitionStep_ = 0.001f;
    float roamingPhase_ = 0.0f;
    float currentTopologyAmount_ = 0.65f;
    float currentRoamingRateHz_ = 0.08f;
    float currentMix_ = 0.55f;
    float currentTraceGain_ = 0.7079f;
    float currentEnabled_ = 1.0f;
    float currentMaskDry_ = 1.0f;
    float currentOutputGain_ = 1.0f;
    float traceGovernorGain_ = 1.0f;
    float safetyGain_ = 1.0f;
    float traceGovernorRelease_ = 0.0001f;
    float safetyRelease_ = 0.0001f;
    float outputCoefficient_ = 0.001f;
    float globalCoefficient_ = 0.001f;
    float switchCoefficient_ = 0.001f;
    float maskCoefficient_ = 0.001f;
    float levelCoefficient_ = 0.001f;
    float attackCoefficient_ = 0.001f;
    float frequencyCoefficient_ = 0.001f;
    float releaseCoefficient_ = 0.001f;
    float ownershipCoefficient_ = 0.001f;
    float topologyCoefficient_ = 0.0015f;
};

} // namespace s3g
