#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"
#include "s3g_spectral_spray.h"
#include "s3g_topology.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace s3g {

constexpr uint32_t kSpectralMeshMaxChannels = kSpectralSprayMaxChannels;
constexpr uint32_t kSpectralMeshHistoryFrames = 32u;
constexpr uint32_t kSpectralMeshPropagationFrames = 128u;

// A synchronized multichannel spectral engine. Local spray processing creates
// material at each node; an energy-normalized topology matrix then transports
// that material between nodes before field memory and resynthesis.
class SpectralMeshProcessor {
public:
    bool prepare(double sampleRate,
                 uint32_t channels,
                 uint32_t fftSize = 2048u,
                 uint32_t overlap = 4u,
                 uint32_t maxBlockFrames = 4096u)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        channels_ = std::clamp<uint32_t>(channels, 1u, kSpectralMeshMaxChannels);
        maxBlockFrames_ = std::max<uint32_t>(1u, maxBlockFrames);
        if (!fft_.prepare(channels_, fftSize, overlap)) {
            ready_ = false;
            return false;
        }

        bins_ = fft_.bins();
        const size_t spectrumSize = static_cast<size_t>(channels_) * bins_;
        inputMag_.assign(spectrumSize, 0.0f);
        previousInputMag_.assign(spectrumSize, 0.0f);
        inputPhaseR_.assign(spectrumSize, 1.0f);
        inputPhaseI_.assign(spectrumSize, 0.0f);
        previousInputPhase_.assign(spectrumSize, 0.0f);
        inputAdvance_.assign(spectrumSize, 0.0f);
        holdMag_.assign(spectrumSize, 0.0f);
        phaseMemoryR_.assign(spectrumSize, 1.0f);
        phaseMemoryI_.assign(spectrumSize, 0.0f);
        localMag_.assign(spectrumSize, 0.0f);
        localPhaseR_.assign(spectrumSize, 1.0f);
        localPhaseI_.assign(spectrumSize, 0.0f);
        sourceEnergy_.assign(spectrumSize, 0.0f);
        sourcePhaseR_.assign(spectrumSize, 1.0f);
        sourcePhaseI_.assign(spectrumSize, 0.0f);
        destinationEnergy_.assign(spectrumSize, 0.0f);
        destinationPhaseR_.assign(spectrumSize, 0.0f);
        destinationPhaseI_.assign(spectrumSize, 0.0f);
        fieldMag_.assign(spectrumSize, 0.0f);
        fieldPhaseR_.assign(spectrumSize, 1.0f);
        fieldPhaseI_.assign(spectrumSize, 0.0f);
        captureMag_.assign(spectrumSize, 0.0f);
        captureAdvance_.assign(spectrumSize, 0.0f);
        capturePhase_.assign(spectrumSize, 0.0f);
        historyMag_.assign(
            static_cast<size_t>(kSpectralMeshHistoryFrames) * spectrumSize, 0.0f);
        propagationHistoryR_.assign(
            static_cast<size_t>(kSpectralMeshPropagationFrames) * spectrumSize, 0.0f);
        propagationHistoryI_.assign(
            static_cast<size_t>(kSpectralMeshPropagationFrames) * spectrumSize, 0.0f);
        edgeLaunchHistory_.assign(
            static_cast<size_t>(kSpectralMeshPropagationFrames) * kGraphCapacity, 0.0f);

        wetBuffers_.assign(channels_, std::vector<float>(maxBlockFrames_, 0.0f));
        wetPtrs_.assign(channels_, nullptr);
        inputPtrs_.assign(channels_, nullptr);
        zeroInput_.assign(maxBlockFrames_, 0.0f);
        dryDelay_.assign(channels_, std::vector<float>(fft_.fftSize(), 0.0f));

        for (uint32_t ch = 0; ch < channels_; ++ch) {
            laneParams_[ch] = sanitize(laneParams_[ch]);
            smoothedLaneParams_[ch] = laneParams_[ch];
            mixSmoothed_[ch] = laneParams_[ch].mix;
            gainSmoothed_[ch] = dbToGain(laneParams_[ch].gainDb);
            safetySmoothed_[ch] = laneParams_[ch].safety;
        }
        rebuildGraphTargets();
        graphLowCurrent_ = graphLowTarget_;
        graphHighCurrent_ = graphHighTarget_;
        graphDistanceCurrent_ = graphDistanceTarget_;
        meshAmountSmoothed_ = meshAmountTarget_;
        centroidSmoothed_ = centroidTarget_;
        flareSmoothed_ = flareTarget_;
        propagationVelocitySmoothed_ = propagationVelocityTarget_;
        propagationDispersionSmoothed_ = propagationDispersionTarget_;
        propagationDampingSmoothed_ = propagationDampingTarget_;
        reset();
        ready_ = true;
        return true;
    }

    void reset()
    {
        fft_.reset();
        dryWritePos_ = 0u;
        frameCounter_ = 0u;
        historyWrite_ = 0u;
        propagationWrite_ = 0u;
        hasProcessed_ = false;
        freezeWasActive_ = false;
        captureRequested_.store(false, std::memory_order_relaxed);
        clearRequested_.store(false, std::memory_order_relaxed);
        captureValid_.store(false, std::memory_order_relaxed);
        phaseReady_.fill(false);
        transientAmount_.fill(0.0f);
        clearSpectralState();
        for (auto& channel : wetBuffers_) {
            std::fill(channel.begin(), channel.end(), 0.0f);
        }
        for (auto& channel : dryDelay_) {
            std::fill(channel.begin(), channel.end(), 0.0f);
        }
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            smoothedLaneParams_[ch] = laneParams_[ch];
            mixSmoothed_[ch] = laneParams_[ch].mix;
            gainSmoothed_[ch] = dbToGain(laneParams_[ch].gainDb);
            safetySmoothed_[ch] = laneParams_[ch].safety;
        }
        graphLowCurrent_ = graphLowTarget_;
        graphHighCurrent_ = graphHighTarget_;
        graphDistanceCurrent_ = graphDistanceTarget_;
        meshAmountSmoothed_ = meshAmountTarget_;
        centroidSmoothed_ = centroidTarget_;
        flareSmoothed_ = flareTarget_;
        propagationVelocitySmoothed_ = propagationVelocityTarget_;
        propagationDispersionSmoothed_ = propagationDispersionTarget_;
        propagationDampingSmoothed_ = propagationDampingTarget_;
        transientProtectSmoothed_ = transientProtectTarget_;
        for (auto& activity : edgeActivity_) {
            activity.store(0.0f, std::memory_order_relaxed);
        }
        for (auto& position : edgePulsePosition_) {
            position.store(0.0f, std::memory_order_relaxed);
        }
    }

    bool ready() const { return ready_; }
    uint32_t channels() const { return channels_; }
    uint32_t bins() const { return bins_; }
    uint32_t latencyFrames() const { return fft_.fftSize(); }

    void setLaneParams(uint32_t lane, const SpectralSprayParams& params)
    {
        if (lane >= kSpectralMeshMaxChannels) return;
        laneParams_[lane] = sanitize(params);
        if (!hasProcessed_) {
            smoothedLaneParams_[lane] = laneParams_[lane];
            mixSmoothed_[lane] = laneParams_[lane].mix;
            gainSmoothed_[lane] = dbToGain(laneParams_[lane].gainDb);
            safetySmoothed_[lane] = laneParams_[lane].safety;
        }
    }

    void setTopologyState(const TopologyState& topology)
    {
        topology_ = topology;
        topology_.amount = std::clamp(topology_.amount, 0.0, 1.0);
        topology_.neighborCount = std::clamp<uint32_t>(topology_.neighborCount, 1u, 3u);
        topology_.neighborRadius = std::clamp(topology_.neighborRadius, 0.0, 1.0);
        topology_.centroidAmount = std::clamp(topology_.centroidAmount, 0.0, 1.0);
        rebuildGraphTargets();
        if (!hasProcessed_) {
            graphLowCurrent_ = graphLowTarget_;
            graphHighCurrent_ = graphHighTarget_;
            graphDistanceCurrent_ = graphDistanceTarget_;
            meshAmountSmoothed_ = meshAmountTarget_;
            centroidSmoothed_ = centroidTarget_;
            flareSmoothed_ = flareTarget_;
        }
    }

    void setTransientProtect(float amount)
    {
        transientProtectTarget_ = clamp(amount, 0.0f, 1.0f);
        if (!hasProcessed_) transientProtectSmoothed_ = transientProtectTarget_;
    }

    float transientProtect() const { return transientProtectTarget_; }

    // VEL 1 is the original instantaneous graph. At VEL 0 the longest active
    // edge spans 126 FFT hops. Positive DISP makes high bins arrive later and
    // low bins earlier; negative values reverse that relationship. DAMP is a
    // distance- and frequency-dependent propagation loss.
    void setPropagation(float velocity, float dispersion, float damping)
    {
        propagationVelocityTarget_ = clamp(velocity, 0.0f, 1.0f);
        propagationDispersionTarget_ = clamp(dispersion, -1.0f, 1.0f);
        propagationDampingTarget_ = clamp(damping, 0.0f, 1.0f);
        if (!hasProcessed_) {
            propagationVelocitySmoothed_ = propagationVelocityTarget_;
            propagationDispersionSmoothed_ = propagationDispersionTarget_;
            propagationDampingSmoothed_ = propagationDampingTarget_;
        }
    }

    float propagationVelocity() const { return propagationVelocityTarget_; }
    float propagationDispersion() const { return propagationDispersionTarget_; }
    float propagationDamping() const { return propagationDampingTarget_; }

    void requestCapture()
    {
        captureRequested_.store(true, std::memory_order_release);
    }

    void requestClearCapture()
    {
        clearRequested_.store(true, std::memory_order_release);
    }

    bool hasCapture() const
    {
        return captureValid_.load(std::memory_order_acquire);
    }

    float edgeActivity(uint32_t source, uint32_t destination) const
    {
        if (source >= channels_ || destination >= channels_) return 0.0f;
        return edgeActivity_[graphIndex(source, destination)].load(std::memory_order_relaxed);
    }

    float edgePulsePosition(uint32_t source, uint32_t destination) const
    {
        if (source >= channels_ || destination >= channels_) return 0.0f;
        return edgePulsePosition_[graphIndex(source, destination)].load(
            std::memory_order_relaxed);
    }

    void process(const float* const* input,
                 uint32_t inputChannels,
                 float* const* output,
                 uint32_t outputChannels,
                 uint32_t frames)
    {
        if (!ready_ || !output || frames == 0u) return;
        frames = std::min(frames, maxBlockFrames_);
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            std::fill(wetBuffers_[ch].begin(), wetBuffers_[ch].begin() + frames, 0.0f);
            wetPtrs_[ch] = wetBuffers_[ch].data();
            inputPtrs_[ch] = input && ch < inputChannels && input[ch]
                ? input[ch]
                : zeroInput_.data();
        }

        fft_.processBlock(inputPtrs_.data(), wetPtrs_.data(), frames,
            [&](SpectralFrameBlockView block) { processSpectralBlock(block); });

        const uint32_t active = std::min(channels_, outputChannels);
        for (uint32_t i = 0; i < frames; ++i) {
            for (uint32_t ch = 0; ch < channels_; ++ch) {
                const auto& target = laneParams_[ch];
                mixSmoothed_[ch] += (target.mix - mixSmoothed_[ch]) * 0.0015f;
                gainSmoothed_[ch] += (dbToGain(target.gainDb) - gainSmoothed_[ch]) * 0.0015f;
                safetySmoothed_[ch] += (target.safety - safetySmoothed_[ch]) * 0.0015f;
                const float dryIn = inputPtrs_[ch][i];
                const float dry = dryDelay_[ch][dryWritePos_];
                dryDelay_[ch][dryWritePos_] = dryIn;
                if (ch < active && output[ch]) {
                    const float mixed = lerp(dry, wetBuffers_[ch][i], mixSmoothed_[ch])
                        * gainSmoothed_[ch];
                    output[ch][i] = safetyLimit(mixed, safetySmoothed_[ch]);
                }
            }
            ++dryWritePos_;
            if (dryWritePos_ >= fft_.fftSize()) dryWritePos_ = 0u;
        }
        for (uint32_t ch = active; ch < outputChannels; ++ch) {
            if (output[ch]) std::fill(output[ch], output[ch] + frames, 0.0f);
        }
        hasProcessed_ = true;
    }

private:
    static constexpr size_t kGraphCapacity =
        static_cast<size_t>(kSpectralMeshMaxChannels) * kSpectralMeshMaxChannels;

    static SpectralSprayParams sanitize(SpectralSprayParams p)
    {
        p.sprayBins = clamp(p.sprayBins, 0.0f, 256.0f);
        p.drift = clamp(p.drift, 0.0f, 1.0f);
        p.hold = clamp(p.hold, 0.0f, 1.0f);
        p.freeze = clamp(p.freeze, 0.0f, 1.0f);
        p.feedback = clamp(p.feedback, 0.0f, 0.85f);
        p.smear = clamp(p.smear, 0.0f, 1.0f);
        p.holes = clamp(p.holes, 0.0f, 0.95f);
        p.phaseBlur = clamp(p.phaseBlur, 0.0f, 1.0f);
        p.damage = clamp(p.damage, 0.0f, 1.0f);
        p.repeat = clamp(p.repeat, 0.0f, 1.0f);
        p.loFreq = clamp(p.loFreq, 0.0f, 23980.0f);
        p.hiFreq = clamp(p.hiFreq, 20.0f, 24000.0f);
        if (p.hiFreq < p.loFreq + 20.0f) p.hiFreq = p.loFreq + 20.0f;
        p.gainDb = clamp(p.gainDb, -60.0f, 18.0f);
        p.mix = clamp(p.mix, 0.0f, 1.0f);
        p.tilt = clamp(p.tilt, -1.0f, 1.0f);
        p.safety = clamp(p.safety, 0.05f, 1.0f);
        return p;
    }

    static uint32_t hash(uint32_t x)
    {
        x ^= x >> 16u;
        x *= 0x7feb352du;
        x ^= x >> 15u;
        x *= 0x846ca68bu;
        x ^= x >> 16u;
        return x;
    }

    static float hashSigned(uint32_t x)
    {
        return static_cast<float>(hash(x) & 0xffffu) / 32767.5f - 1.0f;
    }

    static float safetyLimit(float value, float safety)
    {
        const float threshold = clamp(safety, 0.05f, 0.98f);
        const float magnitude = std::abs(value);
        if (magnitude <= threshold) return value;
        const float room = std::max(0.001f, 1.0f - threshold);
        const float limited = threshold + room * std::tanh((magnitude - threshold) / room);
        return std::copysign(std::min(limited, 1.0f), value);
    }

    static float normalizedPhasor(float& real, float& imag)
    {
        const float length = std::sqrt(real * real + imag * imag);
        if (length <= 0.0000001f) {
            real = 1.0f;
            imag = 0.0f;
            return 0.0f;
        }
        real /= length;
        imag /= length;
        return length;
    }

    size_t spectrumIndex(uint32_t channel, uint32_t bin) const
    {
        return static_cast<size_t>(channel) * bins_ + bin;
    }

    static constexpr size_t graphIndex(uint32_t source, uint32_t destination)
    {
        return static_cast<size_t>(source) * kSpectralMeshMaxChannels + destination;
    }

    void clearSpectralState()
    {
        auto clear = [](std::vector<float>& values) {
            std::fill(values.begin(), values.end(), 0.0f);
        };
        clear(inputMag_);
        clear(previousInputMag_);
        std::fill(inputPhaseR_.begin(), inputPhaseR_.end(), 1.0f);
        clear(inputPhaseI_);
        clear(previousInputPhase_);
        clear(inputAdvance_);
        clear(holdMag_);
        std::fill(phaseMemoryR_.begin(), phaseMemoryR_.end(), 1.0f);
        clear(phaseMemoryI_);
        clear(localMag_);
        std::fill(localPhaseR_.begin(), localPhaseR_.end(), 1.0f);
        clear(localPhaseI_);
        clear(sourceEnergy_);
        std::fill(sourcePhaseR_.begin(), sourcePhaseR_.end(), 1.0f);
        clear(sourcePhaseI_);
        clear(destinationEnergy_);
        clear(destinationPhaseR_);
        clear(destinationPhaseI_);
        clear(fieldMag_);
        std::fill(fieldPhaseR_.begin(), fieldPhaseR_.end(), 1.0f);
        clear(fieldPhaseI_);
        clear(captureMag_);
        clear(captureAdvance_);
        clear(capturePhase_);
        clear(historyMag_);
        clear(propagationHistoryR_);
        clear(propagationHistoryI_);
        clear(edgeLaunchHistory_);
    }

    void rebuildGraphTargets()
    {
        graphLowTarget_.fill(0.0f);
        graphHighTarget_.fill(0.0f);
        graphDistanceTarget_.fill(0.0f);
        const auto controls = topologyControlsFromState(topology_);
        std::array<TopologyPoint, kSpectralMeshMaxChannels> points {};
        for (uint32_t lane = 0; lane < channels_; ++lane) {
            points[lane] = topologyPointForLane(lane, channels_, controls);
        }

        const float twist = static_cast<float>(std::clamp(controls.twist, -1.0, 1.0));
        float longestEdge = 0.0f;
        for (uint32_t source = 0; source < channels_; ++source) {
            const auto neighbors = nearestTopologyNeighbors(topology_, source, channels_);
            std::array<float, kSpectralMeshMaxChannels> base {};
            std::array<float, kSpectralMeshMaxChannels> direction {};
            const uint32_t requested = std::clamp<uint32_t>(topology_.neighborCount, 1u, 3u);
            for (uint32_t slot = 0; slot < requested; ++slot) {
                const int candidate = neighbors[slot];
                if (candidate < 0 || static_cast<uint32_t>(candidate) >= channels_
                    || static_cast<uint32_t>(candidate) == source) continue;
                const uint32_t destination = static_cast<uint32_t>(candidate);
                const double dx = points[source].x - points[destination].x;
                const double dy = points[source].y - points[destination].y;
                const double dz = points[source].z - points[destination].z;
                const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                const double reach = 0.20 + topology_.neighborRadius * 2.80;
                const float weight = static_cast<float>(
                    std::exp(-distance / std::max(0.08, reach)));
                base[destination] += std::max(0.0001f, weight);
                const float edgeDistance = static_cast<float>(distance);
                graphDistanceTarget_[graphIndex(source, destination)] = edgeDistance;
                longestEdge = std::max(longestEdge, edgeDistance);
                direction[destination] = static_cast<float>(std::clamp(
                    (points[destination].lane - points[source].lane) * 0.82
                        + (points[destination].y - points[source].y) * 0.28,
                    -1.0, 1.0));
            }

            float lowSum = 0.0f;
            float highSum = 0.0f;
            for (uint32_t destination = 0; destination < channels_; ++destination) {
                if (base[destination] <= 0.0f) continue;
                const float braid = twist * direction[destination] * 0.78f;
                const float low = base[destination] * std::max(0.08f, 1.0f - braid);
                const float high = base[destination] * std::max(0.08f, 1.0f + braid);
                graphLowTarget_[graphIndex(source, destination)] = low;
                graphHighTarget_[graphIndex(source, destination)] = high;
                lowSum += low;
                highSum += high;
            }
            if (lowSum <= 0.0f || highSum <= 0.0f) {
                graphLowTarget_[graphIndex(source, source)] = 1.0f;
                graphHighTarget_[graphIndex(source, source)] = 1.0f;
            } else {
                for (uint32_t destination = 0; destination < channels_; ++destination) {
                    graphLowTarget_[graphIndex(source, destination)] /= lowSum;
                    graphHighTarget_[graphIndex(source, destination)] /= highSum;
                }
            }
        }

        // Normalize against the longest currently active edge so VEL = 0 can
        // use almost the whole fixed history while preserving relative graph
        // distances. Degenerate/self edges remain instantaneous.
        if (longestEdge > 0.000001f) {
            for (size_t graph = 0; graph < kGraphCapacity; ++graph) {
                graphDistanceTarget_[graph] = clamp(
                    graphDistanceTarget_[graph] / longestEdge, 0.0f, 1.0f);
            }
        } else {
            graphDistanceTarget_.fill(0.0f);
        }

        meshAmountTarget_ = static_cast<float>(topology_.amount);
        centroidTarget_ = static_cast<float>(topology_.centroidAmount);
        flareTarget_ = static_cast<float>(std::clamp(controls.flare, -1.0, 1.0));
    }

    static void smoothParam(float& current, float target, float coefficient)
    {
        current += (target - current) * coefficient;
    }

    void updateSmoothing()
    {
        const float hopSeconds = static_cast<float>(fft_.hopSize())
            / static_cast<float>(sampleRate_);
        const float fast = 1.0f - std::exp(-hopSeconds / 0.030f);
        const float medium = 1.0f - std::exp(-hopSeconds / 0.080f);
        const float slow = 1.0f - std::exp(-hopSeconds / 0.150f);
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            auto& current = smoothedLaneParams_[ch];
            const auto& target = laneParams_[ch];
            smoothParam(current.sprayBins, target.sprayBins, slow);
            smoothParam(current.drift, target.drift, slow);
            smoothParam(current.hold, target.hold, medium);
            smoothParam(current.freeze, target.freeze, fast);
            smoothParam(current.feedback, target.feedback, medium);
            smoothParam(current.smear, target.smear, medium);
            smoothParam(current.holes, target.holes, medium);
            smoothParam(current.phaseBlur, target.phaseBlur, medium);
            smoothParam(current.damage, target.damage, slow);
            smoothParam(current.repeat, target.repeat, medium);
            smoothParam(current.loFreq, target.loFreq, slow);
            smoothParam(current.hiFreq, target.hiFreq, slow);
            smoothParam(current.tilt, target.tilt, medium);
        }
        for (size_t i = 0; i < kGraphCapacity; ++i) {
            smoothParam(graphLowCurrent_[i], graphLowTarget_[i], medium);
            smoothParam(graphHighCurrent_[i], graphHighTarget_[i], medium);
            smoothParam(graphDistanceCurrent_[i], graphDistanceTarget_[i], medium);
        }
        smoothParam(meshAmountSmoothed_, meshAmountTarget_, medium);
        smoothParam(centroidSmoothed_, centroidTarget_, medium);
        smoothParam(flareSmoothed_, flareTarget_, medium);
        smoothParam(transientProtectSmoothed_, transientProtectTarget_, fast);
        smoothParam(propagationVelocitySmoothed_, propagationVelocityTarget_, medium);
        smoothParam(propagationDispersionSmoothed_, propagationDispersionTarget_, medium);
        smoothParam(propagationDampingSmoothed_, propagationDampingTarget_, medium);
    }

    float bandMask(uint32_t bin, const SpectralSprayParams& params) const
    {
        if (bins_ <= 1u) return 1.0f;
        const float nyquist = static_cast<float>(sampleRate_ * 0.5);
        const float binHz = nyquist / static_cast<float>(bins_ - 1u);
        const float lo = params.loFreq / std::max(1.0f, binHz);
        const float hi = params.hiFreq / std::max(1.0f, binHz);
        const float edge = std::max(2.0f, (hi - lo) * 0.035f);
        const float b = static_cast<float>(bin);
        const float low = clamp((b - (lo - edge)) / edge, 0.0f, 1.0f);
        const float high = clamp(((hi + edge) - b) / edge, 0.0f, 1.0f);
        const float lowSmooth = low * low * (3.0f - 2.0f * low);
        const float highSmooth = high * high * (3.0f - 2.0f * high);
        return lowSmooth * highSmooth;
    }

    void analyzeInput(SpectralFrameBlockView block)
    {
        const float expectedBase = (2.0f * kPi) * static_cast<float>(fft_.hopSize())
            / static_cast<float>(fft_.fftSize());
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            const float* real = block.real(ch);
            const float* imag = block.imag(ch);
            float positiveFlux = 0.0f;
            float magnitudeSum = 0.0000001f;
            for (uint32_t bin = 0; bin < bins_; ++bin) {
                const size_t index = spectrumIndex(ch, bin);
                const float r = real[bin];
                const float i = imag[bin];
                const float magnitude = std::sqrt(r * r + i * i);
                float unitR = 1.0f;
                float unitI = 0.0f;
                if (magnitude > 0.0000001f) {
                    unitR = r / magnitude;
                    unitI = i / magnitude;
                }
                const float phase = std::atan2(unitI, unitR);
                const float expected = expectedBase * static_cast<float>(bin);
                const float advance = phaseReady_[ch]
                    ? expected + std::remainder(
                        phase - previousInputPhase_[index] - expected, 2.0f * kPi)
                    : expected;
                positiveFlux += std::max(0.0f, magnitude - previousInputMag_[index]);
                magnitudeSum += magnitude;
                inputMag_[index] = magnitude;
                inputPhaseR_[index] = unitR;
                inputPhaseI_[index] = unitI;
                inputAdvance_[index] = advance;
                previousInputPhase_[index] = phase;
                previousInputMag_[index] = magnitude;
            }
            phaseReady_[ch] = true;
            const float target = clamp((positiveFlux / magnitudeSum) * 3.5f, 0.0f, 1.0f);
            transientAmount_[ch] += (target - transientAmount_[ch]) * 0.42f;
        }
    }

    void createLocalMaterial()
    {
        const float hopSeconds = static_cast<float>(fft_.hopSize())
            / static_cast<float>(sampleRate_);
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            const auto& params = smoothedLaneParams_[ch];
            const float motion = static_cast<float>(frameCounter_) * params.drift * 0.03125f;
            const float damage = params.damage * params.damage;
            const uint32_t damageFrame = frameCounter_ / std::max<uint32_t>(
                1u, static_cast<uint32_t>(std::floor(9.0f - damage * 7.0f)));
            const uint32_t damageCell = std::max<uint32_t>(
                1u, static_cast<uint32_t>(std::floor(30.0f - damage * 24.0f)));
            // Exponential control: roughly 25 ms at zero, one second near the
            // legacy 72% default, and four seconds at maximum.
            const float holdSeconds = 0.025f * std::pow(160.0f, params.hold);
            const float holdDecay = params.hold <= 0.0001f
                ? 0.0f
                : std::exp(-hopSeconds / holdSeconds);
            const float transientDepth = 1.0f
                - transientProtectSmoothed_ * transientAmount_[ch];

            for (uint32_t bin = 0; bin < bins_; ++bin) {
                const size_t index = spectrumIndex(ch, bin);
                const float magnitude = inputMag_[index];
                holdMag_[index] = std::max(magnitude, holdMag_[index] * holdDecay);
            }

            for (uint32_t bin = 0; bin < bins_; ++bin) {
                const size_t index = spectrumIndex(ch, bin);
                const float magnitude = inputMag_[index];
                const float patternA = std::sin(static_cast<float>(bin) * 0.071f + motion);
                const float patternB = std::sin(
                    static_cast<float>(bin) * 0.017f + motion * 2.137f
                    + std::sin(static_cast<float>(bin) * 0.0031f) * kPi);
                const int offset = static_cast<int>(std::floor(
                    (patternA * 0.62f + patternB * 0.38f) * params.sprayBins + 0.5f));
                const uint32_t source = static_cast<uint32_t>(std::clamp<int>(
                    static_cast<int>(bin) + offset, 0, static_cast<int>(bins_ - 1u)));
                const uint32_t spread = std::max<uint32_t>(
                    1u, static_cast<uint32_t>(std::floor(params.sprayBins * 0.12f)));
                const uint32_t low = source > spread ? source - spread : 0u;
                const uint32_t high = std::min<uint32_t>(bins_ - 1u, source + spread);
                const uint32_t cell = bin / damageCell;
                const int brokenOffset = static_cast<int>(std::floor(
                    hashSigned(cell * 3511u + damageFrame * 1777u + ch * 97u)
                        * damage * 112.0f + 0.5f));
                const uint32_t damagedSource = static_cast<uint32_t>(std::clamp<int>(
                    static_cast<int>(source) + brokenOffset,
                    0, static_cast<int>(bins_ - 1u)));

                float transformed = holdMag_[spectrumIndex(ch, source)];
                transformed = lerp(
                    transformed,
                    holdMag_[spectrumIndex(ch, damagedSource)],
                    damage * 0.58f);
                const float wide = (
                    holdMag_[spectrumIndex(ch, low)] + transformed
                    + holdMag_[spectrumIndex(ch, high)]) * 0.33333334f;
                transformed = lerp(transformed, wide, params.smear);

                const float holePattern = 0.5f + 0.5f * std::sin(
                    static_cast<float>(bin) * 1.618f
                    + static_cast<float>(frameCounter_) * params.drift * 0.017f);
                const float holeThreshold = lerp(1.01f, 0.32f, params.holes);
                if (holePattern >= holeThreshold) {
                    transformed *= lerp(0.18f, 0.65f, params.smear);
                }
                const float dropPattern = static_cast<float>(hash(
                    cell * 4567u + damageFrame * 733u + ch * 53u) & 0xffffu)
                    / 65535.0f;
                if (dropPattern < damage * params.holes * 0.72f) {
                    transformed *= lerp(1.0f, 0.08f, damage);
                }

                const float norm = bins_ > 1u
                    ? static_cast<float>(bin) / static_cast<float>(bins_ - 1u)
                    : 0.0f;
                const float lowShelf = std::pow(std::max(1.0f - norm, 0.0f), 0.72f);
                const float highShelf = std::pow(std::max(norm, 0.0f), 0.72f);
                transformed *= lerp(1.0f, lowShelf, std::max(-params.tilt, 0.0f) * 0.45f);
                transformed *= lerp(1.0f, highShelf, std::max(params.tilt, 0.0f) * 0.45f);
                transformed = std::max(0.0f, transformed);

                const float mask = bandMask(bin, params) * transientDepth;
                localMag_[index] = lerp(magnitude, transformed, mask);

                const size_t damagedIndex = spectrumIndex(ch, damagedSource);
                const float phaseMix = std::min(
                    1.0f, params.phaseBlur + damage * 0.28f) * mask;
                float phaseR = lerp(
                    inputPhaseR_[index], phaseMemoryR_[damagedIndex], phaseMix);
                float phaseI = lerp(
                    inputPhaseI_[index], phaseMemoryI_[damagedIndex], phaseMix);
                normalizedPhasor(phaseR, phaseI);
                localPhaseR_[index] = phaseR;
                localPhaseI_[index] = phaseI;
                phaseMemoryR_[index] = inputPhaseR_[index];
                phaseMemoryI_[index] = inputPhaseI_[index];
            }
        }
    }

    void updateCapture()
    {
        const bool clear = clearRequested_.exchange(false, std::memory_order_acq_rel);
        if (clear) {
            std::fill(captureMag_.begin(), captureMag_.end(), 0.0f);
            captureValid_.store(false, std::memory_order_release);
        }

        bool freezeActive = false;
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            freezeActive = freezeActive || smoothedLaneParams_[ch].freeze > 0.001f;
        }
        const bool automaticCapture = freezeActive && !freezeWasActive_
            && !captureValid_.load(std::memory_order_acquire);
        const bool requested = captureRequested_.exchange(false, std::memory_order_acq_rel);
        if (requested || automaticCapture) {
            std::copy(localMag_.begin(), localMag_.end(), captureMag_.begin());
            std::copy(inputAdvance_.begin(), inputAdvance_.end(), captureAdvance_.begin());
            for (size_t i = 0; i < capturePhase_.size(); ++i) {
                capturePhase_[i] = std::atan2(localPhaseI_[i], localPhaseR_[i]);
            }
            captureValid_.store(true, std::memory_order_release);
        }
        freezeWasActive_ = freezeActive;
    }

    void prepareMeshSources()
    {
        const bool captured = captureValid_.load(std::memory_order_acquire);
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            const auto& params = smoothedLaneParams_[ch];
            const float transientReduction = 1.0f
                - transientProtectSmoothed_ * transientAmount_[ch] * 0.88f;
            for (uint32_t bin = 0; bin < bins_; ++bin) {
                const size_t index = spectrumIndex(ch, bin);
                const float memoryDepth = bandMask(bin, params) * transientReduction;
                const float freeze = captured ? params.freeze * memoryDepth : 0.0f;
                const float feedback = params.feedback * 0.92f * memoryDepth;
                float phaseR = localPhaseR_[index];
                float phaseI = localPhaseI_[index];
                float magnitude = localMag_[index];
                if (freeze > 0.0f) {
                    const float frozenR = std::cos(capturePhase_[index]);
                    const float frozenI = std::sin(capturePhase_[index]);
                    magnitude = lerp(magnitude, captureMag_[index], freeze);
                    phaseR = lerp(phaseR, frozenR, freeze);
                    phaseI = lerp(phaseI, frozenI, freeze);
                    normalizedPhasor(phaseR, phaseI);
                }
                const float feedbackMagnitude = fieldMag_[index] * feedback;
                const float withFeedback = magnitude + feedbackMagnitude;
                phaseR = magnitude * phaseR + feedbackMagnitude * fieldPhaseR_[index];
                phaseI = magnitude * phaseI + feedbackMagnitude * fieldPhaseI_[index];
                normalizedPhasor(phaseR, phaseI);
                sourceEnergy_[index] = withFeedback * withFeedback;
                sourcePhaseR_[index] = phaseR;
                sourcePhaseI_[index] = phaseI;
            }
        }
    }

    void writePropagationFrame()
    {
        const size_t spectrumSize = static_cast<size_t>(channels_) * bins_;
        const size_t frameOffset = static_cast<size_t>(propagationWrite_) * spectrumSize;
        for (size_t index = 0; index < spectrumSize; ++index) {
            const float magnitude = std::sqrt(std::max(0.0f, sourceEnergy_[index]));
            propagationHistoryR_[frameOffset + index] = magnitude * sourcePhaseR_[index];
            propagationHistoryI_[frameOffset + index] = magnitude * sourcePhaseI_[index];
        }
    }

    float propagationDelayFrames(size_t graph, float normalizedFrequency) const
    {
        if (propagationVelocitySmoothed_ >= 0.999999f) return 0.0f;
        constexpr float kMaximumDelay =
            static_cast<float>(kSpectralMeshPropagationFrames - 2u);
        const float distance = clamp(graphDistanceCurrent_[graph], 0.0f, 1.0f);
        const float baseDelay = (1.0f - propagationVelocitySmoothed_)
            * distance * kMaximumDelay;
        const float frequencySigned = normalizedFrequency * 2.0f - 1.0f;
        // Positive dispersion delays highs and advances lows. The centered
        // warp remains bounded so fractional reads never leave the ring.
        const float dispersionScale = clamp(
            1.0f + propagationDispersionSmoothed_ * frequencySigned * 0.75f,
            0.25f, 1.75f);
        return clamp(baseDelay * dispersionScale, 0.0f, kMaximumDelay);
    }

    void readPropagationSpectrum(uint32_t source,
                                 uint32_t bin,
                                 float delayFrames,
                                 float& energy,
                                 float& phaseReal,
                                 float& phaseImag) const
    {
        const size_t spectrumSize = static_cast<size_t>(channels_) * bins_;
        const size_t index = spectrumIndex(source, bin);
        const float bounded = clamp(
            delayFrames, 0.0f,
            static_cast<float>(kSpectralMeshPropagationFrames - 2u));
        const uint32_t whole = static_cast<uint32_t>(std::floor(bounded));
        const float fraction = bounded - static_cast<float>(whole);
        const uint32_t newerFrame = (
            propagationWrite_ + kSpectralMeshPropagationFrames - whole)
            % kSpectralMeshPropagationFrames;
        const uint32_t olderFrame = (
            newerFrame + kSpectralMeshPropagationFrames - 1u)
            % kSpectralMeshPropagationFrames;
        const size_t newer = static_cast<size_t>(newerFrame) * spectrumSize + index;
        const size_t older = static_cast<size_t>(olderFrame) * spectrumSize + index;
        const float newerR = propagationHistoryR_[newer];
        const float newerI = propagationHistoryI_[newer];
        const float olderR = propagationHistoryR_[older];
        const float olderI = propagationHistoryI_[older];
        const float newerEnergy = newerR * newerR + newerI * newerI;
        const float olderEnergy = olderR * olderR + olderI * olderI;
        energy = lerp(newerEnergy, olderEnergy, fraction);

        // Interpolating raw complex frames also interpolates their magnitude.
        // With overlap=4, a half-frame read would therefore null every bin
        // whose adjacent phasors are antipodal. Preserve interpolated power
        // and use the complex blend only for direction so fractional travel
        // does not create an unintended velocity-dependent comb filter.
        phaseReal = lerp(newerR, olderR, fraction);
        phaseImag = lerp(newerI, olderI, fraction);
        const float phaseLengthSquared =
            phaseReal * phaseReal + phaseImag * phaseImag;
        if (phaseLengthSquared > 0.000000000001f) {
            const float inverseLength = 1.0f / std::sqrt(phaseLengthSquared);
            phaseReal *= inverseLength;
            phaseImag *= inverseLength;
            return;
        }

        // Antipodal or silent endpoints have no interpolated direction. Pick
        // the nearer endpoint phasor; the independently interpolated power
        // still crossfades continuously through the fractional read.
        phaseReal = fraction < 0.5f ? newerR : olderR;
        phaseImag = fraction < 0.5f ? newerI : olderI;
        const float endpointLengthSquared =
            phaseReal * phaseReal + phaseImag * phaseImag;
        if (endpointLengthSquared > 0.000000000001f) {
            const float inverseLength = 1.0f / std::sqrt(endpointLengthSquared);
            phaseReal *= inverseLength;
            phaseImag *= inverseLength;
        } else {
            phaseReal = 1.0f;
            phaseImag = 0.0f;
        }
    }

    float propagationAmplitude(uint32_t source,
                               uint32_t destination,
                               float normalizedFrequency) const
    {
        if (propagationDampingSmoothed_ <= 0.000001f) return 1.0f;
        const float distance = clamp(
            graphDistanceCurrent_[graphIndex(source, destination)], 0.0f, 1.0f);
        // At maximum damping a full-length edge loses roughly 3 dB at DC and
        // 24 dB at Nyquist. Short edges lose proportionally less.
        const float exponent = propagationDampingSmoothed_ * distance
            * lerp(0.35f, 2.75f, normalizedFrequency);
        return std::exp(-exponent);
    }

    void transportMesh()
    {
        std::fill(destinationEnergy_.begin(), destinationEnergy_.end(), 0.0f);
        std::fill(destinationPhaseR_.begin(), destinationPhaseR_.end(), 0.0f);
        std::fill(destinationPhaseI_.begin(), destinationPhaseI_.end(), 0.0f);
        edgeAccum_.fill(0.0);
        edgeSourceTotal_.fill(0.0);
        edgeLaunchAccum_.fill(0.0);

        for (uint32_t bin = 0; bin < bins_; ++bin) {
            const float norm = bins_ > 1u
                ? static_cast<float>(bin) / static_cast<float>(bins_ - 1u)
                : 0.0f;
            const float frequencySigned = norm * 2.0f - 1.0f;
            double globalEnergy = 0.0;
            double globalEligibleEnergy = 0.0;
            double globalPhaseR = 0.0;
            double globalPhaseI = 0.0;
            for (uint32_t source = 0; source < channels_; ++source) {
                const size_t sourceIndex = spectrumIndex(source, bin);
                const float energy = sourceEnergy_[sourceIndex];
                const float transientReduction = 1.0f
                    - transientProtectSmoothed_ * transientAmount_[source] * 0.88f;
                const float processBand = bandMask(
                    bin, smoothedLaneParams_[source]);
                globalEnergy += energy;
                globalEligibleEnergy += energy * processBand * transientReduction;
                globalPhaseR += energy * sourcePhaseR_[sourceIndex];
                globalPhaseI += energy * sourcePhaseI_[sourceIndex];

                const float flare = clamp(
                    1.0f + flareSmoothed_ * frequencySigned * 0.65f,
                    0.20f, 1.80f);
                const float send = clamp(
                    meshAmountSmoothed_ * processBand * transientReduction * flare,
                    0.0f, 0.96f);
                const float kept = energy * (1.0f - send);
                const size_t selfIndex = spectrumIndex(source, bin);
                destinationEnergy_[selfIndex] += kept;
                destinationPhaseR_[selfIndex] += kept * sourcePhaseR_[sourceIndex];
                destinationPhaseI_[selfIndex] += kept * sourcePhaseI_[sourceIndex];

                for (uint32_t destination = 0; destination < channels_; ++destination) {
                    const size_t graph = graphIndex(source, destination);
                    const float weight = lerp(
                        graphLowCurrent_[graph], graphHighCurrent_[graph], norm);
                    if (weight <= 0.000001f) continue;
                    edgeLaunchAccum_[graph] += static_cast<double>(energy * send * weight);

                    const float delay = propagationDelayFrames(graph, norm);
                    float routedEnergy = energy;
                    float routedPhaseR = sourcePhaseR_[sourceIndex];
                    float routedPhaseI = sourcePhaseI_[sourceIndex];
                    if (delay > 0.000001f) {
                        readPropagationSpectrum(
                            source, bin, delay,
                            routedEnergy, routedPhaseR, routedPhaseI);
                    }
                    const float amplitude = propagationAmplitude(source, destination, norm);
                    const float contribution = routedEnergy * send * weight
                        * amplitude * amplitude;
                    const size_t destinationIndex = spectrumIndex(destination, bin);
                    destinationEnergy_[destinationIndex] += contribution;
                    destinationPhaseR_[destinationIndex] += contribution
                        * routedPhaseR;
                    destinationPhaseI_[destinationIndex] += contribution
                        * routedPhaseI;
                    edgeAccum_[graph] += static_cast<double>(contribution);
                    edgeSourceTotal_[source] += static_cast<double>(contribution);
                }
            }

            const float centroidEnergy = static_cast<float>(
                globalEnergy / static_cast<double>(channels_));
            const float centroidPhaseR = static_cast<float>(
                globalPhaseR / static_cast<double>(channels_));
            const float centroidPhaseI = static_cast<float>(
                globalPhaseI / static_cast<double>(channels_));
            const float centroidEligibility = globalEnergy > 0.000000001
                ? static_cast<float>(std::clamp(
                    globalEligibleEnergy / globalEnergy, 0.0, 1.0))
                : 0.0f;
            for (uint32_t destination = 0; destination < channels_; ++destination) {
                const size_t index = spectrumIndex(destination, bin);
                const float centroid = centroidSmoothed_ * meshAmountSmoothed_
                    * centroidEligibility
                    * bandMask(bin, smoothedLaneParams_[destination]);
                destinationEnergy_[index] = lerp(
                    destinationEnergy_[index], centroidEnergy, centroid);
                destinationPhaseR_[index] = lerp(
                    destinationPhaseR_[index], centroidPhaseR, centroid);
                destinationPhaseI_[index] = lerp(
                    destinationPhaseI_[index], centroidPhaseI, centroid);
            }
        }

        const size_t launchFrameOffset =
            static_cast<size_t>(propagationWrite_) * kGraphCapacity;
        std::fill(
            edgeLaunchHistory_.begin() + static_cast<std::ptrdiff_t>(launchFrameOffset),
            edgeLaunchHistory_.begin()
                + static_cast<std::ptrdiff_t>(launchFrameOffset + kGraphCapacity),
            0.0f);
        for (size_t graph = 0; graph < kGraphCapacity; ++graph) {
            edgeLaunchHistory_[launchFrameOffset + graph] = clamp(
                static_cast<float>(std::sqrt(
                    edgeLaunchAccum_[graph]
                    / static_cast<double>(std::max<uint32_t>(1u, bins_)))),
                0.0f, 1.0f);
        }

        for (uint32_t source = 0; source < channels_; ++source) {
            const double sentEnergy = edgeSourceTotal_[source];
            const double total = std::max(0.000000001, sentEnergy);
            const float sourceLevel = clamp(static_cast<float>(std::sqrt(
                sentEnergy / static_cast<double>(std::max<uint32_t>(1u, bins_)))),
                0.0f, 1.0f);
            for (uint32_t destination = 0; destination < channels_; ++destination) {
                const size_t graph = graphIndex(source, destination);
                const float arrival = static_cast<float>(std::clamp(
                    edgeAccum_[graph] / total, 0.0, 1.0)) * sourceLevel;
                const float travel = propagationDelayFrames(graph, 0.5f);
                float pulse = 0.0f;
                float inFlight = 0.0f;
                if (travel <= 0.0001f) {
                    inFlight = edgeLaunchHistory_[launchFrameOffset + graph];
                    pulse = inFlight > 0.000001f ? 1.0f : 0.0f;
                } else {
                    const uint32_t maximumAge = std::min<uint32_t>(
                        kSpectralMeshPropagationFrames - 2u,
                        static_cast<uint32_t>(std::ceil(travel)));
                    double levelSum = 0.0;
                    double positionSum = 0.0;
                    for (uint32_t age = 0; age <= maximumAge; ++age) {
                        const uint32_t frame = (
                            propagationWrite_ + kSpectralMeshPropagationFrames - age)
                            % kSpectralMeshPropagationFrames;
                        const float level = edgeLaunchHistory_[
                            static_cast<size_t>(frame) * kGraphCapacity + graph];
                        const float energyWeight = level * level;
                        levelSum += energyWeight;
                        positionSum += static_cast<double>(energyWeight)
                            * clamp(static_cast<float>(age) / travel, 0.0f, 1.0f);
                        inFlight = std::max(inFlight, level);
                    }
                    if (levelSum > 0.000000000001) {
                        pulse = clamp(
                            static_cast<float>(positionSum / levelSum), 0.0f, 1.0f);
                    }
                }
                edgePulsePosition_[graph].store(pulse, std::memory_order_relaxed);
                const float target = std::max(arrival, inFlight);
                const float previous = edgeActivity_[graph].load(std::memory_order_relaxed);
                edgeActivity_[graph].store(
                    previous + (target - previous) * 0.28f,
                    std::memory_order_relaxed);
            }
        }
    }

    void finishMeshFrame(SpectralFrameBlockView block)
    {
        const size_t spectrumSize = static_cast<size_t>(channels_) * bins_;
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            float* real = block.real(ch);
            float* imag = block.imag(ch);
            const auto& params = smoothedLaneParams_[ch];
            const uint32_t repeatDelay = 2u + static_cast<uint32_t>(
                std::floor(params.repeat * params.repeat
                    * static_cast<float>(kSpectralMeshHistoryFrames - 3u)));
            const uint32_t historyRead = (
                historyWrite_ + kSpectralMeshHistoryFrames - repeatDelay)
                % kSpectralMeshHistoryFrames;
            const float transientReduction = 1.0f
                - transientProtectSmoothed_ * transientAmount_[ch] * 0.88f;
            for (uint32_t bin = 0; bin < bins_; ++bin) {
                const size_t index = spectrumIndex(ch, bin);
                float magnitude = std::sqrt(std::max(0.0f, destinationEnergy_[index]));
                const float repeated = historyMag_[
                    static_cast<size_t>(historyRead) * spectrumSize + index];
                const float repeat = params.repeat * 0.82f
                    * bandMask(bin, params) * transientReduction;
                magnitude = lerp(magnitude, repeated, repeat);
                if (!std::isfinite(magnitude)) magnitude = 0.0f;
                magnitude = std::min(magnitude, 1000000.0f);
                fieldMag_[index] = magnitude;
                historyMag_[static_cast<size_t>(historyWrite_) * spectrumSize + index]
                    = magnitude;

                float phaseR = destinationPhaseR_[index];
                float phaseI = destinationPhaseI_[index];
                normalizedPhasor(phaseR, phaseI);
                fieldPhaseR_[index] = phaseR;
                fieldPhaseI_[index] = phaseI;
                real[bin] = phaseR * magnitude;
                imag[bin] = phaseI * magnitude;
            }
        }

        if (captureValid_.load(std::memory_order_acquire)) {
            for (size_t i = 0; i < capturePhase_.size(); ++i) {
                capturePhase_[i] = std::remainder(
                    capturePhase_[i] + captureAdvance_[i], 2.0f * kPi);
            }
        }
        historyWrite_ = (historyWrite_ + 1u) % kSpectralMeshHistoryFrames;
        propagationWrite_ = (propagationWrite_ + 1u) % kSpectralMeshPropagationFrames;
        ++frameCounter_;
    }

    void processSpectralBlock(SpectralFrameBlockView block)
    {
        if (block.channels != channels_ || block.bins != bins_) return;
        updateSmoothing();
        analyzeInput(block);
        createLocalMaterial();
        updateCapture();
        prepareMeshSources();
        writePropagationFrame();
        transportMesh();
        finishMeshFrame(block);
    }

    double sampleRate_ = 48000.0;
    uint32_t channels_ = 0u;
    uint32_t bins_ = 0u;
    uint32_t maxBlockFrames_ = 0u;
    uint32_t dryWritePos_ = 0u;
    uint32_t frameCounter_ = 0u;
    uint32_t historyWrite_ = 0u;
    uint32_t propagationWrite_ = 0u;
    bool ready_ = false;
    bool hasProcessed_ = false;
    bool freezeWasActive_ = false;

    SpectralFftProcessor fft_;
    TopologyState topology_ {};
    std::array<SpectralSprayParams, kSpectralMeshMaxChannels> laneParams_ {};
    std::array<SpectralSprayParams, kSpectralMeshMaxChannels> smoothedLaneParams_ {};
    std::array<float, kSpectralMeshMaxChannels> mixSmoothed_ {};
    std::array<float, kSpectralMeshMaxChannels> gainSmoothed_ {};
    std::array<float, kSpectralMeshMaxChannels> safetySmoothed_ {};
    std::array<float, kSpectralMeshMaxChannels> transientAmount_ {};
    std::array<bool, kSpectralMeshMaxChannels> phaseReady_ {};

    std::array<float, kGraphCapacity> graphLowTarget_ {};
    std::array<float, kGraphCapacity> graphHighTarget_ {};
    std::array<float, kGraphCapacity> graphLowCurrent_ {};
    std::array<float, kGraphCapacity> graphHighCurrent_ {};
    std::array<float, kGraphCapacity> graphDistanceTarget_ {};
    std::array<float, kGraphCapacity> graphDistanceCurrent_ {};
    std::array<double, kGraphCapacity> edgeAccum_ {};
    std::array<double, kGraphCapacity> edgeLaunchAccum_ {};
    std::array<double, kSpectralMeshMaxChannels> edgeSourceTotal_ {};
    std::array<std::atomic<float>, kGraphCapacity> edgeActivity_ {};
    std::array<std::atomic<float>, kGraphCapacity> edgePulsePosition_ {};
    float meshAmountTarget_ = 0.0f;
    float meshAmountSmoothed_ = 0.0f;
    float centroidTarget_ = 0.0f;
    float centroidSmoothed_ = 0.0f;
    float flareTarget_ = 0.0f;
    float flareSmoothed_ = 0.0f;
    float transientProtectTarget_ = 0.35f;
    float transientProtectSmoothed_ = 0.35f;
    float propagationVelocityTarget_ = 1.0f;
    float propagationVelocitySmoothed_ = 1.0f;
    float propagationDispersionTarget_ = 0.0f;
    float propagationDispersionSmoothed_ = 0.0f;
    float propagationDampingTarget_ = 0.0f;
    float propagationDampingSmoothed_ = 0.0f;

    std::atomic<bool> captureRequested_ { false };
    std::atomic<bool> clearRequested_ { false };
    std::atomic<bool> captureValid_ { false };

    std::vector<float> inputMag_;
    std::vector<float> previousInputMag_;
    std::vector<float> inputPhaseR_;
    std::vector<float> inputPhaseI_;
    std::vector<float> previousInputPhase_;
    std::vector<float> inputAdvance_;
    std::vector<float> holdMag_;
    std::vector<float> phaseMemoryR_;
    std::vector<float> phaseMemoryI_;
    std::vector<float> localMag_;
    std::vector<float> localPhaseR_;
    std::vector<float> localPhaseI_;
    std::vector<float> sourceEnergy_;
    std::vector<float> sourcePhaseR_;
    std::vector<float> sourcePhaseI_;
    std::vector<float> destinationEnergy_;
    std::vector<float> destinationPhaseR_;
    std::vector<float> destinationPhaseI_;
    std::vector<float> fieldMag_;
    std::vector<float> fieldPhaseR_;
    std::vector<float> fieldPhaseI_;
    std::vector<float> captureMag_;
    std::vector<float> captureAdvance_;
    std::vector<float> capturePhase_;
    std::vector<float> historyMag_;
    std::vector<float> propagationHistoryR_;
    std::vector<float> propagationHistoryI_;
    std::vector<float> edgeLaunchHistory_;

    std::vector<std::vector<float>> wetBuffers_;
    std::vector<float*> wetPtrs_;
    std::vector<const float*> inputPtrs_;
    std::vector<float> zeroInput_;
    std::vector<std::vector<float>> dryDelay_;
};

} // namespace s3g
