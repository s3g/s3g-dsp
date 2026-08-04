#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"
#include "s3g_topology.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace s3g {

constexpr int kDelayProcessorMaxChannels = 128;

struct DelayRouteParams {
    float route = 0.0f;
    float turn = 0.0f;
    float branch = 0.35f;
    float loss = 0.25f;
};

inline DelayRouteParams sanitizeDelayRouteParams(DelayRouteParams params)
{
    params.route = clamp(params.route, 0.0f, 1.0f);
    params.turn = clamp(params.turn, -1.0f, 1.0f);
    params.branch = clamp(params.branch, 0.0f, 1.0f);
    params.loss = clamp(params.loss, 0.0f, 1.0f);
    return params;
}

class DelayProcessor {
public:
    class ParameterUpdateGuard {
    public:
        explicit ParameterUpdateGuard(DelayProcessor& processor) noexcept
            : processor_(processor)
        {
            processor_.beginParameterUpdate();
        }

        ~ParameterUpdateGuard() { processor_.endParameterUpdate(); }
        ParameterUpdateGuard(const ParameterUpdateGuard&) = delete;
        ParameterUpdateGuard& operator=(const ParameterUpdateGuard&) = delete;

    private:
        DelayProcessor& processor_;
    };

    void prepare(double sampleRate, int channels, double maxDelaySeconds = 2.0)
    {
        parameterUpdateDepth_ = 0u;
        routeTargetsDirty_ = false;
        routeTargetsSnap_ = false;
        routeTailEstimateDirty_ = false;
        routeActivationPending_ = false;
        sampleRate_ = std::max(1.0, sampleRate);
        channels_ = std::clamp(channels, 0, kDelayProcessorMaxChannels);
        maxDelaySamples_ = std::max(2, static_cast<int>(std::ceil(sampleRate_ * maxDelaySeconds)) + 2);
        buffer_.assign(static_cast<size_t>(channels_) * static_cast<size_t>(maxDelaySamples_), 0.0f);
        writeIndex_ = 0;
        historyValidSamples_ = 0;
        feedbackFilter_.assign(static_cast<size_t>(channels_), 0.0f);
        delayMs_.assign(static_cast<size_t>(channels_), 250.0f);
        delayTargetMs_.assign(static_cast<size_t>(channels_), 250.0f);
        delayFromMs_.assign(static_cast<size_t>(channels_), 250.0f);
        delayToMs_.assign(static_cast<size_t>(channels_), 250.0f);
        delayFade_.assign(static_cast<size_t>(channels_), 1.0f);
        delaySettleSamples_.assign(static_cast<size_t>(channels_), delaySettleSampleCount());
        delayPending_.assign(static_cast<size_t>(channels_), 0u);
        feedback_.assign(static_cast<size_t>(channels_), 0.30f);
        feedbackTarget_.assign(static_cast<size_t>(channels_), 0.30f);
        tone_.assign(static_cast<size_t>(channels_), 0.65f);
        toneTarget_.assign(static_cast<size_t>(channels_), 0.65f);
        pitchSemitones_.assign(static_cast<size_t>(channels_), 0.0f);
        pitchTargetSemitones_.assign(static_cast<size_t>(channels_), 0.0f);
        pitchPhase_.assign(static_cast<size_t>(channels_), 0.0f);
        network_.assign(static_cast<size_t>(channels_), 0.0f);
        networkTarget_.assign(static_cast<size_t>(channels_), 0.0f);
        networkNeighborA_.assign(static_cast<size_t>(channels_), 0);
        networkNeighborB_.assign(static_cast<size_t>(channels_), 0);
        networkNeighborC_.assign(static_cast<size_t>(channels_), 0);
        networkNeighborCount_.assign(static_cast<size_t>(channels_), 2);
        networkCentroid_.assign(static_cast<size_t>(channels_), 0.22f);
        networkCentroidTarget_.assign(static_cast<size_t>(channels_), 0.22f);
        character_.assign(static_cast<size_t>(channels_), 0.0f);
        characterTarget_.assign(static_cast<size_t>(channels_), 0.0f);
        smearAmount_.assign(static_cast<size_t>(channels_), 0.0f);
        smearTargetAmount_.assign(static_cast<size_t>(channels_), 0.0f);
        smearFast_.assign(static_cast<size_t>(channels_), 0.0f);
        smearSlow_.assign(static_cast<size_t>(channels_), 0.0f);
        delayedFrame_.assign(static_cast<size_t>(channels_), 0.0f);
        filteredFrame_.assign(static_cast<size_t>(channels_), 0.0f);
        legacyFeedbackFrame_.assign(static_cast<size_t>(channels_), 0.0f);
        localFeedbackFrame_.assign(static_cast<size_t>(channels_), 0.0f);
        routedFeedbackFrame_.assign(static_cast<size_t>(channels_), 0.0f);

        const size_t routeCapacity = static_cast<size_t>(channels_) * static_cast<size_t>(channels_);
        routeWeightTarget_.assign(routeCapacity, 0.0f);
        routeWeightCurrent_.assign(routeCapacity, 0.0f);
        centroidWeightTarget_.assign(routeCapacity, 0.0f);
        centroidWeightCurrent_.assign(routeCapacity, 0.0f);
        routeDistanceTarget_.assign(routeCapacity, 0.0f);
        routeDistanceCurrent_.assign(routeCapacity, 0.0f);
        routeGainTarget_.assign(routeCapacity, 1.0f);
        routeGainCurrent_.assign(routeCapacity, 1.0f);
        routeFilterCoeffTarget_.assign(routeCapacity, 1.0f);
        routeFilterCoeffCurrent_.assign(routeCapacity, 1.0f);
        routeFilterState_.assign(routeCapacity, 0.0f);
        edgeEnergyState_.assign(routeCapacity, 0.0f);
        edgePhaseState_.assign(routeCapacity, 0.0f);
        edgePreviousLaunch_.assign(routeCapacity, 0.0f);
        routePairDistanceCache_.assign(routeCapacity, 0.0);
        routeTurnBiasCache_.assign(routeCapacity, 1.0f);
        routeCentroidDistanceCache_.assign(
            static_cast<size_t>(channels_), 0.0);
        activeRouteEdges_.assign(routeCapacity, 0u);
        edgeEnergyPublished_ = std::make_unique<std::atomic<float>[]>(
            kRouteTelemetryCapacity);
        edgePhasePublished_ = std::make_unique<std::atomic<float>[]>(
            kRouteTelemetryCapacity);
        activeRouteEdgeCount_ = 0u;
        routeParamSmoothing_ = smoothingCoefficient(0.018f);
        routeMatrixSmoothing_ = smoothingCoefficient(0.032f);
        routeDistanceSmoothing_ = smoothingCoefficient(0.045f);
        routeEnergyAttack_ = smoothingCoefficient(0.006f);
        routeEnergyRelease_ = smoothingCoefficient(0.420f);
        nodeEnergyAttack_ = smoothingCoefficient(0.008f);
        nodeEnergyRelease_ = smoothingCoefficient(0.260f);
        routeParams_ = sanitizeDelayRouteParams(routeParams_);
        routeCurrent_ = routeParams_;
        routeTailEstimateFrames_ = 0u;
        routeTailRemaining_ = 0u;
        routeTailRemainingPublished_.store(0u, std::memory_order_relaxed);
        routeTelemetryActive_ = false;
        hasProcessed_ = false;
        for (int ch = 0; ch < channels_; ++ch) {
            const size_t index = static_cast<size_t>(ch);
            networkNeighborA_[index] = (ch - 1 + channels_) % std::max(1, channels_);
            networkNeighborB_[index] = (ch + 1) % std::max(1, channels_);
            networkNeighborC_[index] = (ch + channels_ / 2) % std::max(1, channels_);
        }
        if (requestedActiveLaneCount_ == 0u) {
            requestedActiveLaneCount_ = static_cast<uint32_t>(channels_);
            for (uint32_t lane = 0u; lane < requestedActiveLaneCount_; ++lane) {
                requestedActiveLanes_[lane] = lane;
            }
        }
        rebuildRouteTargets(true);
        resetRouteTelemetry();
        updateRouteTailEstimate();
    }

    void reset()
    {
        parameterUpdateDepth_ = 0u;
        routeTargetsDirty_ = false;
        routeTargetsSnap_ = false;
        routeTailEstimateDirty_ = false;
        routeActivationPending_ = false;
        std::fill(feedbackFilter_.begin(), feedbackFilter_.end(), 0.0f);
        writeIndex_ = 0;
        // Logical invalidation is equivalent to clearing the ring and keeps
        // CLAP reset bounded when it is called on the audio thread.
        historyValidSamples_ = 0;
        delayTargetMs_ = delayMs_;
        delayFromMs_ = delayMs_;
        delayToMs_ = delayMs_;
        std::fill(delayFade_.begin(), delayFade_.end(), 1.0f);
        std::fill(delaySettleSamples_.begin(), delaySettleSamples_.end(), delaySettleSampleCount());
        std::fill(delayPending_.begin(), delayPending_.end(), 0u);
        std::fill(pitchPhase_.begin(), pitchPhase_.end(), 0.0f);
        std::fill(smearFast_.begin(), smearFast_.end(), 0.0f);
        std::fill(smearSlow_.begin(), smearSlow_.end(), 0.0f);
        std::fill(routeFilterState_.begin(), routeFilterState_.end(), 0.0f);
        std::fill(edgePreviousLaunch_.begin(), edgePreviousLaunch_.end(), 0.0f);
        routeCurrent_ = routeParams_;
        routeTailRemaining_ = 0u;
        routeTailRemainingPublished_.store(0u, std::memory_order_relaxed);
        rebuildRouteTargets(true);
        resetRouteTelemetry();
        routeTelemetryActive_ = false;
        hasProcessed_ = false;
    }

    // A wrapper may update many lane parameters from one coherent control
    // snapshot. Deferring route/tail maintenance keeps that publication O(1)
    // per snapshot instead of repeating it for every lane setter.
    void beginParameterUpdate() noexcept
    {
        ++parameterUpdateDepth_;
    }

    void endParameterUpdate()
    {
        if (parameterUpdateDepth_ == 0u) return;
        --parameterUpdateDepth_;
        if (parameterUpdateDepth_ != 0u) return;
        if (routeTargetsDirty_) {
            const bool snap = routeTargetsSnap_;
            routeTargetsDirty_ = false;
            routeTargetsSnap_ = false;
            rebuildRouteTargets(snap);
        }
        if (routeTailEstimateDirty_) {
            routeTailEstimateDirty_ = false;
            updateRouteTailEstimate();
        }
        if (routeActivationPending_) {
            routeActivationPending_ = false;
            routeTailRemaining_ = routeTailFramesPublished_.load(
                std::memory_order_relaxed);
            routeTailRemainingPublished_.store(
                routeTailRemaining_, std::memory_order_relaxed);
        }
    }

    ParameterUpdateGuard scopedParameterUpdate() noexcept
    {
        return ParameterUpdateGuard(*this);
    }

    void setChannelDelayMs(int channel, float delayMs)
    {
        if (validChannel(channel)) {
            const size_t index = static_cast<size_t>(channel);
            const float target = clamp(delayMs, 1.0f, static_cast<float>(maxDelayMs()));
            if (!hasProcessed_) {
                delayMs_[index] = target;
                delayTargetMs_[index] = target;
                delayFromMs_[index] = target;
                delayToMs_[index] = target;
                delayFade_[index] = 1.0f;
                delaySettleSamples_[index] = delaySettleSampleCount();
                delayPending_[index] = 0u;
                return;
            }

            if (std::fabs(target - delayTargetMs_[index]) < 0.25f) {
                return;
            }
            delayTargetMs_[index] = target;
            // Start the debounce window on the first meaningful change, not
            // every subsequent target update. Continuous topology motion can
            // therefore reach the crossfade instead of postponing it forever.
            if (delayPending_[index] == 0u) {
                delayPending_[index] = 1u;
                delaySettleSamples_[index] = 0;
            }
            requestRouteTailEstimate();
        }
    }

    void setChannelFeedback(int channel, float feedback)
    {
        if (validChannel(channel)) {
            const size_t index = static_cast<size_t>(channel);
            const float target = clamp(feedback, 0.0f, 0.82f);
            if (std::fabs(target - feedbackTarget_[index]) < 0.000001f) {
                if (!hasProcessed_) feedback_[index] = target;
                return;
            }
            feedbackTarget_[index] = target;
            if (!hasProcessed_) {
                feedback_[index] = feedbackTarget_[index];
            }
            requestRouteTailEstimate();
        }
    }

    void setChannelTone(int channel, float tone)
    {
        if (validChannel(channel)) {
            const size_t index = static_cast<size_t>(channel);
            toneTarget_[index] = clamp(tone, 0.0f, 1.0f);
            if (!hasProcessed_) {
                tone_[index] = toneTarget_[index];
            }
        }
    }

    void setChannelPitchSemitones(int channel, float semitones)
    {
        if (validChannel(channel)) {
            const size_t index = static_cast<size_t>(channel);
            pitchTargetSemitones_[index] = clamp(semitones, -24.0f, 24.0f);
            if (!hasProcessed_) {
                pitchSemitones_[index] = pitchTargetSemitones_[index];
            }
        }
    }

    void setChannelNetwork(int channel, float amount)
    {
        if (validChannel(channel)) {
            const size_t index = static_cast<size_t>(channel);
            networkTarget_[index] = clamp(amount, 0.0f, 0.75f);
            if (!hasProcessed_) {
                network_[index] = networkTarget_[index];
            }
        }
    }

    void setChannelNetworkNeighbors(int channel, int neighborA, int neighborB)
    {
        if (validChannel(channel)) {
            const size_t index = static_cast<size_t>(channel);
            networkNeighborA_[index] = validChannel(neighborA) ? neighborA : channel;
            networkNeighborB_[index] = validChannel(neighborB) ? neighborB : networkNeighborA_[index];
            networkNeighborC_[index] = networkNeighborB_[index];
            networkNeighborCount_[index] = 2;
        }
    }

    void setChannelNetworkTopology(int channel, int neighborA, int neighborB, int neighborC, int neighborCount, float centroidAmount)
    {
        if (validChannel(channel)) {
            const size_t index = static_cast<size_t>(channel);
            networkNeighborA_[index] = validChannel(neighborA) ? neighborA : channel;
            networkNeighborB_[index] = validChannel(neighborB) ? neighborB : networkNeighborA_[index];
            networkNeighborC_[index] = validChannel(neighborC) ? neighborC : networkNeighborB_[index];
            networkNeighborCount_[index] = std::clamp(neighborCount, 1, 3);
            networkCentroidTarget_[index] = clamp(centroidAmount, 0.0f, 1.0f);
            if (!hasProcessed_) {
                networkCentroid_[index] = networkCentroidTarget_[index];
            }
        }
    }

    void setChannelCharacter(int channel, float amount)
    {
        if (validChannel(channel)) {
            const size_t index = static_cast<size_t>(channel);
            characterTarget_[index] = clamp(amount, 0.0f, 1.0f);
            if (!hasProcessed_) {
                character_[index] = characterTarget_[index];
            }
        }
    }

    void setChannelSmearAmount(int channel, float amount)
    {
        if (validChannel(channel)) {
            const size_t index = static_cast<size_t>(channel);
            smearTargetAmount_[index] = clamp(amount, 0.0f, 1.0f);
            if (!hasProcessed_) {
                smearAmount_[index] = smearTargetAmount_[index];
            }
        }
    }

    void setRouteParams(const DelayRouteParams& params)
    {
        const DelayRouteParams next = sanitizeDelayRouteParams(params);
        const bool changed = std::fabs(next.route - routeParams_.route) > 0.000001f
            || std::fabs(next.turn - routeParams_.turn) > 0.000001f
            || std::fabs(next.branch - routeParams_.branch) > 0.000001f
            || std::fabs(next.loss - routeParams_.loss) > 0.000001f;
        if (!changed) return;
        const bool activating = routeParams_.route <= 0.000001f
            && next.route > 0.000001f;
        routeParams_ = next;
        if (!hasProcessed_) {
            routeCurrent_ = routeParams_;
        }
        requestRouteTargets(!hasProcessed_);
        requestRouteTailEstimate();
        if (activating) {
            if (parameterUpdateDepth_ > 0u) {
                routeActivationPending_ = true;
            } else {
                routeTailRemaining_ = routeTailFramesPublished_.load(
                    std::memory_order_relaxed);
                routeTailRemainingPublished_.store(
                    routeTailRemaining_, std::memory_order_relaxed);
            }
        }
    }

    DelayRouteParams routeParams() const { return routeParams_; }

    void setTopology(const TopologyState& topology,
                     const uint32_t* activeLanes,
                     uint32_t activeLaneCount)
    {
        TopologyState next = topology;
        next.amount = std::clamp(next.amount, 0.0, 1.0);
        next.jitter = std::clamp(next.jitter, 0.0, 1.0);
        next.collapse = std::clamp(next.collapse, 0.0, 1.0);
        next.dirX = std::clamp(next.dirX, -1.0, 1.0);
        next.dirY = std::clamp(next.dirY, -1.0, 1.0);
        next.dirZ = std::clamp(next.dirZ, -1.0, 1.0);
        next.twist = std::clamp(next.twist, -1.0, 1.0);
        next.flare = std::clamp(next.flare, -1.0, 1.0);
        next.shape = std::min<uint32_t>(next.shape, kTopologyShapeCount - 1u);
        next.neighborCount = std::clamp<uint32_t>(next.neighborCount, 1u, 3u);
        next.neighborRadius = std::clamp(next.neighborRadius, 0.0, 1.0);
        next.centroidAmount = std::clamp(next.centroidAmount, 0.0, 1.0);

        std::array<uint32_t, kDelayProcessorMaxChannels> nextActiveLanes {};
        uint32_t nextActiveLaneCount = 0u;
        std::array<bool, kDelayProcessorMaxChannels> seen {};
        if (activeLanes) {
            const uint32_t limit = std::min<uint32_t>(
                activeLaneCount, static_cast<uint32_t>(kDelayProcessorMaxChannels));
            for (uint32_t index = 0u; index < limit; ++index) {
                const uint32_t lane = activeLanes[index];
                const uint32_t channelLimit = channels_ > 0
                    ? static_cast<uint32_t>(channels_)
                    : static_cast<uint32_t>(kDelayProcessorMaxChannels);
                if (lane >= channelLimit || seen[lane]) {
                    continue;
                }
                seen[lane] = true;
                nextActiveLanes[nextActiveLaneCount++] = lane;
            }
        }
        if (nextActiveLaneCount == 0u && channels_ > 0) {
            nextActiveLaneCount = static_cast<uint32_t>(std::max(0, channels_));
            for (uint32_t lane = 0u; lane < nextActiveLaneCount; ++lane) {
                nextActiveLanes[lane] = lane;
            }
        }

        bool changed = !topologyStateMatches(routeTopology_, next)
            || requestedActiveLaneCount_ != nextActiveLaneCount;
        if (!changed) {
            for (uint32_t lane = 0u; lane < nextActiveLaneCount; ++lane) {
                if (requestedActiveLanes_[lane] != nextActiveLanes[lane]) {
                    changed = true;
                    break;
                }
            }
        }
        if (!changed) return;

        routeTopology_ = next;
        requestedActiveLaneCount_ = nextActiveLaneCount;
        std::copy_n(nextActiveLanes.begin(), nextActiveLaneCount,
            requestedActiveLanes_.begin());
        requestRouteTargets(!hasProcessed_);
        requestRouteTailEstimate();
    }

    float edgeEnergy(uint32_t source, uint32_t destination) const
    {
        if (source >= static_cast<uint32_t>(std::max(0, channels_))
            || destination >= static_cast<uint32_t>(std::max(0, channels_))) {
            return 0.0f;
        }
        return edgeEnergyPublished_[telemetryIndex(source, destination)].load(
            std::memory_order_relaxed);
    }

    float edgePhase(uint32_t source, uint32_t destination) const
    {
        if (source >= static_cast<uint32_t>(std::max(0, channels_))
            || destination >= static_cast<uint32_t>(std::max(0, channels_))) {
            return 0.0f;
        }
        return edgePhasePublished_[telemetryIndex(source, destination)].load(
            std::memory_order_relaxed);
    }

    float nodeEnergy(uint32_t lane) const
    {
        if (lane >= static_cast<uint32_t>(std::max(0, channels_))) return 0.0f;
        return nodeEnergyPublished_[lane].load(std::memory_order_relaxed);
    }

    float centroidEnergy() const
    {
        return centroidEnergyPublished_.load(std::memory_order_relaxed);
    }

    float centroidPhase() const
    {
        return centroidPhasePublished_.load(std::memory_order_relaxed);
    }

    uint32_t routeTailFrames() const
    {
        return routeTailFramesPublished_.load(std::memory_order_relaxed);
    }

    uint32_t routeTailRemainingFrames() const
    {
        return routeTailRemainingPublished_.load(std::memory_order_relaxed);
    }

    void processFrame(const float* input, float* wetOutput)
    {
        if (!input || !wetOutput || channels_ <= 0 || maxDelaySamples_ <= 0) {
            return;
        }

        for (int ch = 0; ch < channels_; ++ch) {
            const size_t index = static_cast<size_t>(ch);
            updateDelayCommit(index);
            feedback_[index] += (feedbackTarget_[index] - feedback_[index]) * 0.0008f;
            tone_[index] += (toneTarget_[index] - tone_[index]) * 0.0030f;
            pitchSemitones_[index] += (pitchTargetSemitones_[index] - pitchSemitones_[index]) * 0.00025f;
            network_[index] += (networkTarget_[index] - network_[index]) * 0.0012f;
            networkCentroid_[index] += (networkCentroidTarget_[index] - networkCentroid_[index]) * 0.0012f;
            character_[index] += (characterTarget_[index] - character_[index]) * 0.0010f;
            smearAmount_[index] += (smearTargetAmount_[index] - smearAmount_[index]) * 0.0010f;

            const float delayed = readCommittedDelay(ch, index);
            const float age = character_[index];
            const float toneCoeff = 0.006f + tone_[index] * tone_[index] * (0.28f - age * 0.16f);
            float& filter = feedbackFilter_[index];
            filter = flushDenormal(filter + toneCoeff * (delayed - filter));

            delayedFrame_[index] = delayed;
            filteredFrame_[index] = filter;
            const float colored = (delayed + (filter - delayed) * (age * 0.42f)) * (1.0f - age * 0.18f);
            wetOutput[ch] = safetyLimit(applySmearTexture(index, colored));
        }

        float centroid = 0.0f;
        for (int ch = 0; ch < channels_; ++ch) {
            centroid += filteredFrame_[static_cast<size_t>(ch)];
        }
        centroid /= static_cast<float>(std::max(1, channels_));

        const bool routeActive = routeParams_.route > 0.000001f
            || routeCurrent_.route > 0.000001f || routeTailRemaining_ > 0u;
        if (!routeActive) {
            if (routeTelemetryActive_) {
                resetRouteTelemetry();
                routeTelemetryActive_ = false;
            }
            // Keep this as the original junction, including operation order.
            // It is the compatibility path for every v1-v10 state and for a
            // freshly instantiated processor with ROUTE at zero.
            for (int ch = 0; ch < channels_; ++ch) {
                const size_t index = static_cast<size_t>(ch);
                const int neighborA = validChannel(networkNeighborA_[index]) ? networkNeighborA_[index] : ch;
                const int neighborB = validChannel(networkNeighborB_[index]) ? networkNeighborB_[index] : neighborA;
                const int neighborC = validChannel(networkNeighborC_[index]) ? networkNeighborC_[index] : neighborB;
                const int neighborCount = std::clamp(networkNeighborCount_[index], 1, 3);
                float neighbor = filteredFrame_[static_cast<size_t>(neighborA)];
                if (neighborCount >= 2) {
                    neighbor += filteredFrame_[static_cast<size_t>(neighborB)];
                }
                if (neighborCount >= 3) {
                    neighbor += filteredFrame_[static_cast<size_t>(neighborC)];
                }
                neighbor /= static_cast<float>(neighborCount);
                const float centroidMix = networkCentroid_[index];
                const float networked = neighbor * (1.0f - centroidMix) + centroid * centroidMix;
                const float feedbackSource = filteredFrame_[index] + (networked - filteredFrame_[index]) * network_[index];
                const float character = character_[index];
                const float feedbackTrim = 1.0f - character * 0.28f;
                const float feedbackSend = flushDenormal(tapeSaturate(feedbackSource * feedback_[index] * feedbackTrim, character));
                const float writeValue = input[ch] + feedbackSend;
                writeSample(ch, safetyLimit(tapeSaturate(writeValue, character * 0.35f)));
            }
        } else {
            processRoutedFeedback(input, wetOutput, centroid);
        }

        writeIndex_ = (writeIndex_ + 1) % maxDelaySamples_;
        historyValidSamples_ = std::min(
            maxDelaySamples_, historyValidSamples_ + 1);
        hasProcessed_ = true;
    }

    int channels() const { return channels_; }
    double maxDelayMs() const { return (static_cast<double>(maxDelaySamples_ - 2) / sampleRate_) * 1000.0; }

private:
    static constexpr size_t kRouteTelemetryCapacity =
        static_cast<size_t>(kDelayProcessorMaxChannels)
        * static_cast<size_t>(kDelayProcessorMaxChannels);
    static constexpr uint32_t kRouteTelemetryPublishInterval = 16u;

    bool validChannel(int channel) const
    {
        return channel >= 0 && channel < channels_;
    }

    static constexpr size_t telemetryIndex(uint32_t source, uint32_t destination)
    {
        return static_cast<size_t>(source) * kDelayProcessorMaxChannels
            + static_cast<size_t>(destination);
    }

    size_t routeIndex(uint32_t source, uint32_t destination) const
    {
        return static_cast<size_t>(source) * static_cast<size_t>(channels_)
            + static_cast<size_t>(destination);
    }

    float smoothingCoefficient(float seconds) const
    {
        return 1.0f - std::exp(-1.0f /
            std::max(1.0f, static_cast<float>(sampleRate_) * seconds));
    }

    float effectiveDelaySamples(size_t index) const
    {
        float delay = delayMs_[index];
        if (delayFade_[index] < 1.0f) {
            const float fade = smoothstep(delayFade_[index]);
            delay = delayFromMs_[index]
                + (delayToMs_[index] - delayFromMs_[index]) * fade;
        }
        return std::max(2.0f, delay * static_cast<float>(sampleRate_ * 0.001));
    }

    void resetRouteTelemetry()
    {
        std::fill(edgeEnergyState_.begin(), edgeEnergyState_.end(), 0.0f);
        std::fill(edgePhaseState_.begin(), edgePhaseState_.end(), 0.0f);
        nodeEnergyState_.fill(0.0f);
        centroidEnergyState_ = 0.0f;
        centroidPhaseState_ = 0.0f;
        centroidPreviousLaunch_ = 0.0f;
        routeTelemetryPublishCounter_ = 0u;
        // Only the prepared physical lanes are observable. Keeping this reset
        // to the active capacity avoids a 32k-atomic spike when a routed tail
        // finally hands the audio thread back to the legacy path.
        for (uint32_t source = 0u;
             source < static_cast<uint32_t>(std::max(0, channels_)); ++source) {
            for (uint32_t destination = 0u;
                 destination < static_cast<uint32_t>(std::max(0, channels_));
                 ++destination) {
                const size_t edge = telemetryIndex(source, destination);
                if (edgeEnergyPublished_) {
                    edgeEnergyPublished_[edge].store(
                        0.0f, std::memory_order_relaxed);
                }
                if (edgePhasePublished_) {
                    edgePhasePublished_[edge].store(
                        0.0f, std::memory_order_relaxed);
                }
            }
        }
        for (uint32_t lane = 0u;
             lane < static_cast<uint32_t>(std::max(0, channels_)); ++lane) {
            nodeEnergyPublished_[lane].store(0.0f, std::memory_order_relaxed);
        }
        centroidEnergyPublished_.store(0.0f, std::memory_order_relaxed);
        centroidPhasePublished_.store(0.0f, std::memory_order_relaxed);
    }

    static bool topologyStateMatches(const TopologyState& a,
                                     const TopologyState& b)
    {
        return a.amount == b.amount
            && a.jitter == b.jitter
            && a.collapse == b.collapse
            && a.dirX == b.dirX
            && a.dirY == b.dirY
            && a.dirZ == b.dirZ
            && a.twist == b.twist
            && a.flare == b.flare
            && a.shape == b.shape
            && a.motionMode == b.motionMode
            && a.motionVariant == b.motionVariant
            && a.motionRateHz == b.motionRateHz
            && a.motionDepth == b.motionDepth
            && a.motionPhase == b.motionPhase
            && a.neighborCount == b.neighborCount
            && a.neighborRadius == b.neighborRadius
            && a.centroidAmount == b.centroidAmount;
    }

    void requestRouteTargets(bool snap)
    {
        if (parameterUpdateDepth_ > 0u) {
            routeTargetsDirty_ = true;
            routeTargetsSnap_ = routeTargetsSnap_ || snap;
            return;
        }
        rebuildRouteTargets(snap);
    }

    void requestRouteTailEstimate()
    {
        if (parameterUpdateDepth_ > 0u) {
            routeTailEstimateDirty_ = true;
            return;
        }
        updateRouteTailEstimate();
    }

    void updateRouteTailEstimate()
    {
        if (channels_ <= 0 || maxDelaySamples_ <= 0) {
            routeTailEstimateFrames_ = 0u;
            routeTailFramesPublished_.store(0u, std::memory_order_relaxed);
            return;
        }
        float maximumFeedback = 0.0f;
        for (int lane = 0; lane < channels_; ++lane) {
            maximumFeedback = std::max(maximumFeedback,
                clamp(feedbackTarget_[static_cast<size_t>(lane)], 0.0f, 0.82f));
        }
        const double repeats = maximumFeedback > 0.0001f
            ? std::ceil(std::log(0.001) / std::log(static_cast<double>(maximumFeedback)))
            : 1.0;
        const double frames = std::max(1.0, repeats)
            * static_cast<double>(std::max(2, maxDelaySamples_ - 2));
        routeTailEstimateFrames_ = static_cast<uint32_t>(std::min<double>(
            frames, static_cast<double>(std::numeric_limits<uint32_t>::max())));
        routeTailFramesPublished_.store(
            routeParams_.route > 0.000001f ? routeTailEstimateFrames_ : 0u,
            std::memory_order_relaxed);
    }

    void rebuildRouteTargets(bool snap)
    {
        if (channels_ <= 0 || routeWeightTarget_.empty()) return;

        std::fill(routeWeightTarget_.begin(), routeWeightTarget_.end(), 0.0f);
        std::fill(centroidWeightTarget_.begin(), centroidWeightTarget_.end(), 0.0f);
        std::fill(routeDistanceTarget_.begin(), routeDistanceTarget_.end(), 0.0f);
        std::fill(routeGainTarget_.begin(), routeGainTarget_.end(), 1.0f);
        std::fill(routeFilterCoeffTarget_.begin(), routeFilterCoeffTarget_.end(), 1.0f);

        std::array<bool, kDelayProcessorMaxChannels> active {};
        uint32_t activeCount = 0u;
        for (uint32_t ordinal = 0u; ordinal < requestedActiveLaneCount_; ++ordinal) {
            const uint32_t lane = requestedActiveLanes_[ordinal];
            if (lane >= static_cast<uint32_t>(channels_) || active[lane]) continue;
            active[lane] = true;
            activeLanes_[activeCount] = lane;
            ++activeCount;
        }
        activeLaneCount_ = activeCount;
        if (activeLaneCount_ == 0u) {
            activeLaneCount_ = static_cast<uint32_t>(channels_);
            for (uint32_t lane = 0u; lane < activeLaneCount_; ++lane) {
                active[lane] = true;
                activeLanes_[lane] = lane;
                routePoints_[lane] = topologyPointForLane(
                    lane, activeLaneCount_, routeTopology_);
            }
        } else {
            for (uint32_t ordinal = 0u; ordinal < activeLaneCount_; ++ordinal) {
                const uint32_t lane = activeLanes_[ordinal];
                routePoints_[lane] = topologyPointForLane(
                    ordinal, activeLaneCount_, routeTopology_);
            }
        }

        TopologyPoint centroidPoint {};
        for (uint32_t ordinal = 0u; ordinal < activeLaneCount_; ++ordinal) {
            const auto& point = routePoints_[activeLanes_[ordinal]];
            centroidPoint.x += point.x;
            centroidPoint.y += point.y;
            centroidPoint.z += point.z;
        }
        const double inverseCount = 1.0 / static_cast<double>(std::max<uint32_t>(1u, activeLaneCount_));
        centroidPoint.x *= inverseCount;
        centroidPoint.y *= inverseCount;
        centroidPoint.z *= inverseCount;

        // Distance and turn bias are invariant for the rest of this rebuild.
        // Cache them once: the route candidate, hub, and weighting passes all
        // revisit the same source/destination pairs, and repeated sqrt/exp
        // calls dominated block-rate topology automation at small buffers.
        for (uint32_t ordinal = 0u; ordinal < activeLaneCount_; ++ordinal) {
            const uint32_t lane = activeLanes_[ordinal];
            const auto& point = routePoints_[lane];
            const double dx = point.x - centroidPoint.x;
            const double dy = point.y - centroidPoint.y;
            const double dz = point.z - centroidPoint.z;
            routeCentroidDistanceCache_[lane] =
                std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        double maximumPairDistance = 0.001;
        for (uint32_t a = 0u; a < activeLaneCount_; ++a) {
            const uint32_t firstLane = activeLanes_[a];
            const auto& first = routePoints_[firstLane];
            routePairDistanceCache_[routeIndex(firstLane, firstLane)] = 0.0;
            for (uint32_t b = a + 1u; b < activeLaneCount_; ++b) {
                const uint32_t secondLane = activeLanes_[b];
                const auto& second = routePoints_[secondLane];
                const double dx = first.x - second.x;
                const double dy = first.y - second.y;
                const double dz = first.z - second.z;
                const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                routePairDistanceCache_[routeIndex(firstLane, secondLane)] = distance;
                routePairDistanceCache_[routeIndex(secondLane, firstLane)] = distance;
                maximumPairDistance = std::max(maximumPairDistance, distance);
            }
        }

        double axisX = routeTopology_.dirX;
        double axisY = routeTopology_.dirY;
        double axisZ = routeTopology_.dirZ;
        normalize3(axisX, axisY, axisZ);
        for (uint32_t sourceOrdinal = 0u;
             sourceOrdinal < activeLaneCount_; ++sourceOrdinal) {
            const uint32_t source = activeLanes_[sourceOrdinal];
            const auto& first = routePoints_[source];
            const double sx = first.x - centroidPoint.x;
            const double sy = first.y - centroidPoint.y;
            const double sz = first.z - centroidPoint.z;
            const double sourceLength = routeCentroidDistanceCache_[source];
            for (uint32_t destinationOrdinal = 0u;
                 destinationOrdinal < activeLaneCount_; ++destinationOrdinal) {
                const uint32_t destination = activeLanes_[destinationOrdinal];
                const auto& second = routePoints_[destination];
                const double dx = second.x - centroidPoint.x;
                const double dy = second.y - centroidPoint.y;
                const double dz = second.z - centroidPoint.z;
                const double destinationLength =
                    routeCentroidDistanceCache_[destination];
                double circulation = 0.0;
                if (sourceLength > 0.00001 && destinationLength > 0.00001) {
                    const double crossX = sy * dz - sz * dy;
                    const double crossY = sz * dx - sx * dz;
                    const double crossZ = sx * dy - sy * dx;
                    circulation =
                        (crossX * axisX + crossY * axisY + crossZ * axisZ)
                        / (sourceLength * destinationLength);
                }
                if (std::fabs(circulation) < 0.0001
                    && activeLaneCount_ > 2u) {
                    const uint32_t forward =
                        (destinationOrdinal + activeLaneCount_ - sourceOrdinal)
                        % activeLaneCount_;
                    circulation =
                        forward > 0u && forward <= activeLaneCount_ / 2u
                        ? 0.5 : -0.5;
                }
                routeTurnBiasCache_[routeIndex(source, destination)] =
                    static_cast<float>(std::exp(
                        static_cast<double>(routeParams_.turn)
                        * circulation * 2.4));
            }
        }

        auto directDistance = [&](uint32_t firstLane, uint32_t secondLane) {
            return routePairDistanceCache_[routeIndex(firstLane, secondLane)];
        };
        auto centroidDistance = [&](uint32_t lane) {
            return routeCentroidDistanceCache_[lane];
        };
        auto turnBias = [&](uint32_t sourceOrdinal, uint32_t destinationOrdinal) {
            return routeTurnBiasCache_[routeIndex(
                activeLanes_[sourceOrdinal], activeLanes_[destinationOrdinal])];
        };

        const uint32_t requestedNeighbors = std::clamp<uint32_t>(
            routeTopology_.neighborCount, 1u, 3u);
        const double radius = 0.18 + routeTopology_.neighborRadius * 3.20;
        const float rankOpenness = lerp(0.08f, 1.0f, routeParams_.branch);
        const float centroidMixRequested = static_cast<float>(
            std::clamp(routeTopology_.centroidAmount, 0.0, 1.0));

        for (uint32_t sourceOrdinal = 0u; sourceOrdinal < activeLaneCount_; ++sourceOrdinal) {
            const uint32_t source = activeLanes_[sourceOrdinal];
            if (activeLaneCount_ < 2u) {
                routeWeightTarget_[routeIndex(source, source)] = 1.0f;
                continue;
            }

            std::array<double, 3> neighborScore {
                std::numeric_limits<double>::max(),
                std::numeric_limits<double>::max(),
                std::numeric_limits<double>::max()
            };
            std::array<double, 3> neighborDistance {
                std::numeric_limits<double>::max(),
                std::numeric_limits<double>::max(),
                std::numeric_limits<double>::max()
            };
            std::array<uint32_t, 3> neighborOrdinal {};
            for (uint32_t destinationOrdinal = 0u;
                 destinationOrdinal < activeLaneCount_; ++destinationOrdinal) {
                if (destinationOrdinal == sourceOrdinal) continue;
                const uint32_t destination = activeLanes_[destinationOrdinal];
                const double distance = directDistance(source, destination);
                // Direction must participate in candidate selection, not only
                // in the weights that are normalized afterwards. Otherwise a
                // one-neighbor graph makes TURN mathematically inert.
                const double score = distance / std::max(
                    0.10f, turnBias(sourceOrdinal, destinationOrdinal));
                for (uint32_t slot = 0u; slot < requestedNeighbors; ++slot) {
                    if (score >= neighborScore[slot]) continue;
                    for (uint32_t move = requestedNeighbors - 1u; move > slot; --move) {
                        neighborScore[move] = neighborScore[move - 1u];
                        neighborDistance[move] = neighborDistance[move - 1u];
                        neighborOrdinal[move] = neighborOrdinal[move - 1u];
                    }
                    neighborScore[slot] = score;
                    neighborDistance[slot] = distance;
                    neighborOrdinal[slot] = destinationOrdinal;
                    break;
                }
            }
            uint32_t neighborCount = 0u;
            for (uint32_t slot = 0u; slot < requestedNeighbors; ++slot) {
                if (!std::isfinite(neighborScore[slot])) continue;
                if (neighborCount > 0u && neighborDistance[slot] > radius) continue;
                if (neighborCount != slot) {
                    neighborScore[neighborCount] = neighborScore[slot];
                    neighborDistance[neighborCount] = neighborDistance[slot];
                    neighborOrdinal[neighborCount] = neighborOrdinal[slot];
                }
                ++neighborCount;
            }

            std::array<bool, kDelayProcessorMaxChannels> directCandidate {};
            std::array<float, kDelayProcessorMaxChannels> neighborRaw {};
            std::array<float, kDelayProcessorMaxChannels> hubRaw {};
            std::array<float, kDelayProcessorMaxChannels> directPath {};
            std::array<float, kDelayProcessorMaxChannels> hubPath {};
            float neighborSum = 0.0f;
            for (uint32_t slot = 0u; slot < neighborCount; ++slot) {
                const uint32_t destinationOrdinal = neighborOrdinal[slot];
                const uint32_t destination = activeLanes_[destinationOrdinal];
                directCandidate[destination] = true;
                const float distance = static_cast<float>(neighborDistance[slot]);
                const float rank = std::pow(rankOpenness, static_cast<float>(slot));
                const float raw = rank * turnBias(sourceOrdinal, destinationOrdinal)
                    / std::max(0.08f, 0.08f + distance);
                neighborRaw[destination] = raw;
                directPath[destination] = distance;
                neighborSum += raw;
            }

            std::array<double, 3> hubScore {
                std::numeric_limits<double>::max(),
                std::numeric_limits<double>::max(),
                std::numeric_limits<double>::max()
            };
            std::array<uint32_t, 3> hubOrdinal {};
            bool foundNonNeighbor = false;
            for (uint32_t destinationOrdinal = 0u;
                 destinationOrdinal < activeLaneCount_; ++destinationOrdinal) {
                if (destinationOrdinal == sourceOrdinal) continue;
                const uint32_t destination = activeLanes_[destinationOrdinal];
                if (!directCandidate[destination]) foundNonNeighbor = true;
            }
            for (uint32_t destinationOrdinal = 0u;
                 destinationOrdinal < activeLaneCount_; ++destinationOrdinal) {
                if (destinationOrdinal == sourceOrdinal) continue;
                const uint32_t destination = activeLanes_[destinationOrdinal];
                if (foundNonNeighbor && directCandidate[destination]) continue;
                const double path = centroidDistance(source) + centroidDistance(destination);
                const double score = path /
                    std::max(0.10f, turnBias(sourceOrdinal, destinationOrdinal));
                for (uint32_t slot = 0u; slot < requestedNeighbors; ++slot) {
                    if (score >= hubScore[slot]) continue;
                    for (uint32_t move = requestedNeighbors - 1u; move > slot; --move) {
                        hubScore[move] = hubScore[move - 1u];
                        hubOrdinal[move] = hubOrdinal[move - 1u];
                    }
                    hubScore[slot] = score;
                    hubOrdinal[slot] = destinationOrdinal;
                    break;
                }
            }
            float hubSum = 0.0f;
            for (uint32_t slot = 0u; slot < requestedNeighbors; ++slot) {
                if (!std::isfinite(hubScore[slot])) continue;
                const uint32_t destinationOrdinal = hubOrdinal[slot];
                const uint32_t destination = activeLanes_[destinationOrdinal];
                const float path = static_cast<float>(
                    centroidDistance(source) + centroidDistance(destination));
                const float rank = std::pow(rankOpenness, static_cast<float>(slot));
                const float raw = rank * turnBias(sourceOrdinal, destinationOrdinal)
                    / std::max(0.08f, 0.08f + path);
                hubRaw[destination] = raw;
                hubPath[destination] = path;
                hubSum += raw;
            }

            float centroidMix = hubSum > 0.000001f ? centroidMixRequested : 0.0f;
            if (neighborSum <= 0.000001f) centroidMix = 1.0f;
            float rowSum = 0.0f;
            for (uint32_t destinationOrdinal = 0u;
                 destinationOrdinal < activeLaneCount_; ++destinationOrdinal) {
                const uint32_t destination = activeLanes_[destinationOrdinal];
                if (destination == source) continue;
                const float neighborWeight = neighborSum > 0.000001f
                    ? (1.0f - centroidMix) * neighborRaw[destination] / neighborSum : 0.0f;
                const float hubWeight = hubSum > 0.000001f
                    ? centroidMix * hubRaw[destination] / hubSum : 0.0f;
                const float weight = neighborWeight + hubWeight;
                if (weight <= 0.000001f) continue;
                const size_t edge = routeIndex(source, destination);
                routeWeightTarget_[edge] = weight;
                centroidWeightTarget_[edge] = hubWeight;
                routeDistanceTarget_[edge] =
                    (neighborWeight * directPath[destination]
                        + hubWeight * hubPath[destination]) / weight;
                rowSum += weight;
            }
            if (rowSum <= 0.000001f) {
                routeWeightTarget_[routeIndex(source, source)] = 1.0f;
            } else if (std::fabs(rowSum - 1.0f) > 0.000001f) {
                const float inverse = 1.0f / rowSum;
                for (uint32_t destinationOrdinal = 0u;
                     destinationOrdinal < activeLaneCount_; ++destinationOrdinal) {
                    const uint32_t destination = activeLanes_[destinationOrdinal];
                    const size_t edge = routeIndex(source, destination);
                    routeWeightTarget_[edge] *= inverse;
                    centroidWeightTarget_[edge] *= inverse;
                }
            }
        }

        for (uint32_t lane = 0u; lane < static_cast<uint32_t>(channels_); ++lane) {
            if (!active[lane]) routeWeightTarget_[routeIndex(lane, lane)] = 1.0f;
        }

        for (uint32_t source = 0u; source < static_cast<uint32_t>(channels_); ++source) {
            for (uint32_t destination = 0u;
                 destination < static_cast<uint32_t>(channels_); ++destination) {
                const size_t edge = routeIndex(source, destination);
                if (routeWeightTarget_[edge] <= 0.000001f) continue;
                const bool hubRoute = centroidWeightTarget_[edge] > 0.000001f;
                const float denominator = static_cast<float>(maximumPairDistance)
                    * (hubRoute ? 2.0f : 1.0f);
                const float distance = clamp(
                    routeDistanceTarget_[edge] / std::max(0.001f, denominator),
                    0.0f, 1.0f);
                const float absorption = routeParams_.loss * distance;
                routeGainTarget_[edge] = std::pow(10.0f, -absorption * 12.0f / 20.0f);
                if (absorption <= 0.000001f) {
                    routeFilterCoeffTarget_[edge] = 1.0f;
                } else {
                    const float cutoff = std::min(
                        static_cast<float>(sampleRate_ * 0.45),
                        18000.0f * std::pow(650.0f / 18000.0f, absorption));
                    routeFilterCoeffTarget_[edge] = 1.0f - std::exp(
                        -2.0f * static_cast<float>(M_PI) * cutoff
                        / static_cast<float>(sampleRate_));
                }
            }
        }

        if (snap) {
            std::copy(routeWeightTarget_.begin(), routeWeightTarget_.end(),
                routeWeightCurrent_.begin());
            std::copy(centroidWeightTarget_.begin(), centroidWeightTarget_.end(),
                centroidWeightCurrent_.begin());
            std::copy(routeDistanceTarget_.begin(), routeDistanceTarget_.end(),
                routeDistanceCurrent_.begin());
            std::copy(routeGainTarget_.begin(), routeGainTarget_.end(),
                routeGainCurrent_.begin());
            std::copy(routeFilterCoeffTarget_.begin(), routeFilterCoeffTarget_.end(),
                routeFilterCoeffCurrent_.begin());
        }
        activeRouteEdgeCount_ = 0u;
        for (size_t edge = 0u; edge < routeWeightTarget_.size(); ++edge) {
            if (routeWeightTarget_[edge] > 0.000001f
                || routeWeightCurrent_[edge] > 0.000001f) {
                activeRouteEdges_[activeRouteEdgeCount_++] = static_cast<uint32_t>(edge);
            } else {
                routeFilterState_[edge] = 0.0f;
                edgeEnergyState_[edge] = 0.0f;
                edgePhaseState_[edge] = 0.0f;
                edgePreviousLaunch_[edge] = 0.0f;
                const uint32_t source = static_cast<uint32_t>(
                    edge / static_cast<size_t>(channels_));
                const uint32_t destination = static_cast<uint32_t>(
                    edge % static_cast<size_t>(channels_));
                edgeEnergyPublished_[telemetryIndex(source, destination)].store(
                    0.0f, std::memory_order_relaxed);
                edgePhasePublished_[telemetryIndex(source, destination)].store(
                    0.0f, std::memory_order_relaxed);
            }
        }
    }

    void processRoutedFeedback(const float* input, const float* wetOutput, float centroid)
    {
        routeTelemetryActive_ = true;
        const bool publishTelemetry = routeTelemetryPublishCounter_ == 0u;
        routeTelemetryPublishCounter_ =
            (routeTelemetryPublishCounter_ + 1u)
            % kRouteTelemetryPublishInterval;
        routeCurrent_.route += (routeParams_.route - routeCurrent_.route)
            * routeParamSmoothing_;
        if (routeParams_.route <= 0.000001f && routeCurrent_.route < 0.000001f) {
            routeCurrent_.route = 0.0f;
        }
        const float routeAmount = routeCurrent_.route;
        std::fill(routedFeedbackFrame_.begin(), routedFeedbackFrame_.end(), 0.0f);

        float activeCentroid = 0.0f;
        for (uint32_t ordinal = 0u; ordinal < activeLaneCount_; ++ordinal) {
            activeCentroid += filteredFrame_[activeLanes_[ordinal]];
        }
        activeCentroid /= static_cast<float>(std::max<uint32_t>(1u, activeLaneCount_));
        // Preserve the all-channel v10 centroid exactly at ROUTE zero, then
        // shed inactive prepared lanes continuously as the explicit route
        // graph takes ownership of the return.
        const float routedCentroid = centroid
            + (activeCentroid - centroid) * routeAmount;

        for (int ch = 0; ch < channels_; ++ch) {
            const size_t index = static_cast<size_t>(ch);
            const int neighborA = validChannel(networkNeighborA_[index]) ? networkNeighborA_[index] : ch;
            const int neighborB = validChannel(networkNeighborB_[index]) ? networkNeighborB_[index] : neighborA;
            const int neighborC = validChannel(networkNeighborC_[index]) ? networkNeighborC_[index] : neighborB;
            const int neighborCount = std::clamp(networkNeighborCount_[index], 1, 3);
            float neighbor = filteredFrame_[static_cast<size_t>(neighborA)];
            if (neighborCount >= 2) neighbor += filteredFrame_[static_cast<size_t>(neighborB)];
            if (neighborCount >= 3) neighbor += filteredFrame_[static_cast<size_t>(neighborC)];
            neighbor /= static_cast<float>(neighborCount);
            const float centroidMix = networkCentroid_[index];
            const float networked = neighbor * (1.0f - centroidMix)
                + routedCentroid * centroidMix;
            const float legacySource = filteredFrame_[index]
                + (networked - filteredFrame_[index]) * network_[index];
            const float character = character_[index];
            const float feedbackTrim = 1.0f - character * 0.28f;
            legacyFeedbackFrame_[index] = flushDenormal(tapeSaturate(
                legacySource * feedback_[index] * feedbackTrim, character));
            localFeedbackFrame_[index] = flushDenormal(tapeSaturate(
                filteredFrame_[index] * feedback_[index] * feedbackTrim, character));
        }

        float centroidLaunch = 0.0f;
        float centroidDelayWeight = 0.0f;
        float centroidDelaySum = 0.0f;
        bool routedLaunch = false;
        for (uint32_t activeIndex = 0u; activeIndex < activeRouteEdgeCount_; ++activeIndex) {
            const size_t edge = activeRouteEdges_[activeIndex];
            const uint32_t source = static_cast<uint32_t>(edge / static_cast<size_t>(channels_));
            const uint32_t destination = static_cast<uint32_t>(edge % static_cast<size_t>(channels_));
            routeWeightCurrent_[edge] += (routeWeightTarget_[edge] - routeWeightCurrent_[edge])
                * routeMatrixSmoothing_;
            centroidWeightCurrent_[edge] +=
                (centroidWeightTarget_[edge] - centroidWeightCurrent_[edge])
                * routeMatrixSmoothing_;
            routeDistanceCurrent_[edge] +=
                (routeDistanceTarget_[edge] - routeDistanceCurrent_[edge])
                * routeDistanceSmoothing_;
            routeGainCurrent_[edge] += (routeGainTarget_[edge] - routeGainCurrent_[edge])
                * routeMatrixSmoothing_;
            routeFilterCoeffCurrent_[edge] +=
                (routeFilterCoeffTarget_[edge] - routeFilterCoeffCurrent_[edge])
                * routeMatrixSmoothing_;
            if (routeWeightTarget_[edge] <= 0.000001f
                && routeWeightCurrent_[edge] < 0.000001f) {
                routeWeightCurrent_[edge] = 0.0f;
            }

            float& filtered = routeFilterState_[edge];
            filtered = flushDenormal(filtered
                + routeFilterCoeffCurrent_[edge]
                    * (localFeedbackFrame_[source] - filtered));
            const float contribution = flushDenormal(filtered
                * routeWeightCurrent_[edge] * routeGainCurrent_[edge]);
            routedFeedbackFrame_[destination] += contribution;

            const float activity = std::fabs(contribution) * routeAmount;
            float& energy = edgeEnergyState_[edge];
            energy += (activity - energy)
                * (activity > energy ? routeEnergyAttack_ : routeEnergyRelease_);
            energy = flushDenormal(energy);
            const float previousLaunch = edgePreviousLaunch_[edge];
            if (activity > 0.00001f
                && activity > previousLaunch * 1.65f + 0.000002f) {
                edgePhaseState_[edge] = 0.0f;
            }
            edgePreviousLaunch_[edge] = activity;
            edgePhaseState_[edge] += 1.0f /
                effectiveDelaySamples(static_cast<size_t>(destination));
            edgePhaseState_[edge] -= std::floor(edgePhaseState_[edge]);
            if (publishTelemetry) {
                edgeEnergyPublished_[telemetryIndex(source, destination)].store(
                    energy, std::memory_order_relaxed);
                edgePhasePublished_[telemetryIndex(source, destination)].store(
                    edgePhaseState_[edge], std::memory_order_relaxed);
            }
            routedLaunch = routedLaunch || activity > 0.00001f;

            const float centroidFraction = routeWeightCurrent_[edge] > 0.000001f
                ? clamp(centroidWeightCurrent_[edge] / routeWeightCurrent_[edge], 0.0f, 1.0f)
                : 0.0f;
            const float centroidActivity = activity * centroidFraction;
            centroidLaunch += centroidActivity;
            centroidDelaySum += centroidActivity
                * effectiveDelaySamples(static_cast<size_t>(destination));
            centroidDelayWeight += centroidActivity;
        }

        for (int ch = 0; ch < channels_; ++ch) {
            const size_t index = static_cast<size_t>(ch);
            const float feedbackSend = legacyFeedbackFrame_[index]
                + (routedFeedbackFrame_[index] - legacyFeedbackFrame_[index])
                    * routeAmount;
            const float character = character_[index];
            const float writeValue = input[ch] + feedbackSend;
            writeSample(ch, safetyLimit(tapeSaturate(writeValue, character * 0.35f)));

            const float activity = std::fabs(wetOutput[ch]) * routeAmount;
            float& energy = nodeEnergyState_[index];
            energy += (activity - energy)
                * (activity > energy ? nodeEnergyAttack_ : nodeEnergyRelease_);
            energy = flushDenormal(energy);
            if (publishTelemetry) {
                nodeEnergyPublished_[index].store(
                    energy, std::memory_order_relaxed);
            }
        }

        centroidEnergyState_ += (centroidLaunch - centroidEnergyState_)
            * (centroidLaunch > centroidEnergyState_
                    ? routeEnergyAttack_ : routeEnergyRelease_);
        centroidEnergyState_ = flushDenormal(centroidEnergyState_);
        if (centroidLaunch > 0.00001f
            && centroidLaunch > centroidPreviousLaunch_ * 1.65f + 0.000002f) {
            centroidPhaseState_ = 0.0f;
        }
        centroidPreviousLaunch_ = centroidLaunch;
        const float centroidDelay = centroidDelayWeight > 0.000001f
            ? centroidDelaySum / centroidDelayWeight
            : static_cast<float>(sampleRate_ * 0.25);
        centroidPhaseState_ += 1.0f / std::max(2.0f, centroidDelay);
        centroidPhaseState_ -= std::floor(centroidPhaseState_);
        if (publishTelemetry) {
            centroidEnergyPublished_.store(
                centroidEnergyState_, std::memory_order_relaxed);
            centroidPhasePublished_.store(
                centroidPhaseState_, std::memory_order_relaxed);
        }

        if (routeParams_.route > 0.000001f || routedLaunch) {
            routeTailRemaining_ = std::max(routeTailRemaining_, routeTailEstimateFrames_);
        } else if (routeTailRemaining_ > 0u) {
            --routeTailRemaining_;
        }
        routeTailRemainingPublished_.store(routeTailRemaining_, std::memory_order_relaxed);
    }

    size_t indexFor(int channel, int sample) const
    {
        return static_cast<size_t>(channel) * static_cast<size_t>(maxDelaySamples_) + static_cast<size_t>(sample);
    }

    float readDelay(int channel, float delaySamples) const
    {
        // Pitch heads add a window offset to the published base delay. Near
        // the maximum delay that effective value can exceed the ring, leaving
        // a negative index even after the single wrap below. Pin the read to
        // the same representable limit exposed by maxDelayMs().
        const float maximumDelay = static_cast<float>(
            std::max(1, maxDelaySamples_ - 2));
        const float boundedDelay = std::isfinite(delaySamples)
            ? std::clamp(delaySamples, 1.0f, maximumDelay)
            : 1.0f;
        const float read = static_cast<float>(writeIndex_) - boundedDelay;
        const float wrapped = read < 0.0f ? read + static_cast<float>(maxDelaySamples_) : read;
        const int i0 = static_cast<int>(std::floor(wrapped)) % maxDelaySamples_;
        const int i1 = (i0 + 1) % maxDelaySamples_;
        const int im1 = (i0 - 1 + maxDelaySamples_) % maxDelaySamples_;
        const int i2 = (i0 + 2) % maxDelaySamples_;
        const float frac = wrapped - std::floor(wrapped);
        const auto sample = [&](int position) {
            if (historyValidSamples_ < maxDelaySamples_) {
                int age = writeIndex_ - position;
                if (age <= 0) age += maxDelaySamples_;
                if (age > historyValidSamples_) return 0.0f;
            }
            return buffer_[indexFor(channel, position)];
        };
        return cubicInterpolate(sample(im1), sample(i0), sample(i1),
            sample(i2), frac);
    }

    float readPitchShiftedDelay(int channel, size_t index, float baseDelaySamples, bool advancePhase)
    {
        const float semitones = pitchSemitones_[index];
        if (std::fabs(semitones) < 0.01f) {
            return readDelay(channel, baseDelaySamples);
        }

        const float normal = readDelay(channel, baseDelaySamples);
        const float shiftBlend = std::clamp((std::fabs(semitones) - 0.02f) / 0.24f, 0.0f, 1.0f);
        if (shiftBlend <= 0.0f) {
            return normal;
        }

        const float ratio = std::pow(2.0f, semitones / 12.0f);
        const float windowSamples = std::clamp(
            static_cast<float>(sampleRate_ * 0.110),
            static_cast<float>(sampleRate_ * 0.050),
            std::max(16.0f, baseDelaySamples * 0.55f));
        const float increment = std::clamp(std::fabs(1.0f - ratio) / windowSamples, 0.0f, 0.025f);
        if (advancePhase) {
            pitchPhase_[index] += increment;
            if (pitchPhase_[index] >= 1.0f) {
                pitchPhase_[index] -= std::floor(pitchPhase_[index]);
            }
        }

        auto readHead = [&](float phase) {
            phase -= std::floor(phase);
            const float offset = ratio >= 1.0f ? (1.0f - phase) * windowSamples : phase * windowSamples;
            return readDelay(channel, baseDelaySamples + offset);
        };

        const float phaseA = pitchPhase_[index];
        const float phaseB = phaseA + 0.5f;
        const float wrappedB = phaseB - std::floor(phaseB);
        const float envA = std::sin(static_cast<float>(M_PI) * phaseA);
        const float envB = std::sin(static_cast<float>(M_PI) * wrappedB);
        const float weightA = envA * envA;
        const float weightB = envB * envB;
        const float weightSum = std::max(0.0001f, weightA + weightB);
        const float shifted = (readHead(phaseA) * weightA + readHead(phaseB) * weightB) / weightSum;
        return normal + (shifted - normal) * shiftBlend;
    }

    float readCommittedDelay(int channel, size_t index)
    {
        if (delayFade_[index] >= 1.0f) {
            const float delaySamples = delayMs_[index] * static_cast<float>(sampleRate_ * 0.001);
            return readPitchShiftedDelay(channel, index, delaySamples, true);
        }

        const float fade = smoothstep(delayFade_[index]);
        const float fromSamples = delayFromMs_[index] * static_cast<float>(sampleRate_ * 0.001);
        const float toSamples = delayToMs_[index] * static_cast<float>(sampleRate_ * 0.001);
        const float from = readPitchShiftedDelay(channel, index, fromSamples, true);
        const float to = readPitchShiftedDelay(channel, index, toSamples, false);
        return from + (to - from) * fade;
    }

    float applySmearTexture(size_t index, float primary)
    {
        const float amount = smearAmount_[index];
        if (amount <= 0.0001f) {
            return primary;
        }

        const float fastCoeff = 0.22f - amount * 0.12f;
        const float slowCoeff = 0.035f + amount * 0.020f;
        smearFast_[index] += (primary - smearFast_[index]) * fastCoeff;
        smearSlow_[index] += (smearFast_[index] - smearSlow_[index]) * slowCoeff;
        const float texture = smearFast_[index] * 0.56f + smearSlow_[index] * 0.44f;
        const float mix = std::min(0.52f, amount * 0.46f);
        return primary + (texture - primary) * mix;
    }

    void updateDelayCommit(size_t index)
    {
        if (delayFade_[index] < 1.0f) {
            delayFade_[index] = std::min(1.0f, delayFade_[index] + 1.0f / (static_cast<float>(sampleRate_) * delayFadeSeconds()));
            if (delayFade_[index] >= 1.0f) {
                delayMs_[index] = delayToMs_[index];
                delayFromMs_[index] = delayMs_[index];
                if (std::fabs(delayTargetMs_[index] - delayMs_[index]) >= 0.25f) {
                    delayPending_[index] = 1u;
                    delaySettleSamples_[index] = 0;
                } else {
                    delayPending_[index] = 0u;
                    delaySettleSamples_[index] = delaySettleSampleCount();
                }
            }
            return;
        }

        if (delayPending_[index] == 0u) {
            if (std::fabs(delayTargetMs_[index] - delayMs_[index]) < 0.25f) {
                return;
            }
            delayPending_[index] = 1u;
            delaySettleSamples_[index] = 0;
        }

        if (delaySettleSamples_[index] < delaySettleSampleCount()) {
            ++delaySettleSamples_[index];
        }
        if (delaySettleSamples_[index] < delaySettleSampleCount()) {
            return;
        }

        if (std::fabs(delayTargetMs_[index] - delayMs_[index]) < 0.25f) {
            delayPending_[index] = 0u;
            delaySettleSamples_[index] = delaySettleSampleCount();
            return;
        }

        delayFromMs_[index] = delayMs_[index];
        delayToMs_[index] = delayTargetMs_[index];
        delayFade_[index] = 0.0f;
        delayPending_[index] = 0u;
    }

    int delaySettleSampleCount() const
    {
        return std::max(1, static_cast<int>(sampleRate_ * 0.120));
    }

    float delayFadeSeconds() const
    {
        return 0.250f;
    }

    float smoothstep(float value) const
    {
        value = clamp(value, 0.0f, 1.0f);
        return value * value * (3.0f - 2.0f * value);
    }

    float cubicInterpolate(float y0, float y1, float y2, float y3, float t) const
    {
        const float a0 = y3 - y2 - y0 + y1;
        const float a1 = y0 - y1 - a0;
        const float a2 = y2 - y0;
        const float a3 = y1;
        return ((a0 * t + a1) * t + a2) * t + a3;
    }

    float softClip(float value) const
    {
        return std::tanh(value * 0.95f) * 1.0526316f;
    }

    float tapeSaturate(float value, float amount) const
    {
        const float drive = 0.95f + amount * 1.35f;
        const float shaped = std::tanh(value * drive) / std::max(1.0f, drive);
        return value + (shaped - value) * (amount * 0.72f);
    }

    float safetyLimit(float value) const
    {
        const float magnitude = std::fabs(value);
        if (magnitude <= 1.0f) {
            return value;
        }
        const float limited = 1.0f + std::tanh((magnitude - 1.0f) * 0.65f) * 0.85f;
        return std::copysign(limited, value);
    }

    void writeSample(int channel, float value)
    {
        buffer_[indexFor(channel, writeIndex_)] = value;
    }

    double sampleRate_ = 48000.0;
    int channels_ = 0;
    int maxDelaySamples_ = 0;
    int writeIndex_ = 0;
    int historyValidSamples_ = 0;
    bool hasProcessed_ = false;
    std::vector<float> buffer_;
    std::vector<float> feedbackFilter_;
    std::vector<float> delayMs_;
    std::vector<float> delayTargetMs_;
    std::vector<float> delayFromMs_;
    std::vector<float> delayToMs_;
    std::vector<float> delayFade_;
    std::vector<int> delaySettleSamples_;
    std::vector<uint8_t> delayPending_;
    std::vector<float> feedback_;
    std::vector<float> feedbackTarget_;
    std::vector<float> tone_;
    std::vector<float> toneTarget_;
    std::vector<float> pitchSemitones_;
    std::vector<float> pitchTargetSemitones_;
    std::vector<float> pitchPhase_;
    std::vector<float> network_;
    std::vector<float> networkTarget_;
    std::vector<int> networkNeighborA_;
    std::vector<int> networkNeighborB_;
    std::vector<int> networkNeighborC_;
    std::vector<int> networkNeighborCount_;
    std::vector<float> networkCentroid_;
    std::vector<float> networkCentroidTarget_;
    std::vector<float> character_;
    std::vector<float> characterTarget_;
    std::vector<float> smearAmount_;
    std::vector<float> smearTargetAmount_;
    std::vector<float> smearFast_;
    std::vector<float> smearSlow_;
    std::vector<float> delayedFrame_;
    std::vector<float> filteredFrame_;
    std::vector<float> legacyFeedbackFrame_;
    std::vector<float> localFeedbackFrame_;
    std::vector<float> routedFeedbackFrame_;

    DelayRouteParams routeParams_ {};
    DelayRouteParams routeCurrent_ {};
    TopologyState routeTopology_ {};
    std::array<uint32_t, kDelayProcessorMaxChannels> requestedActiveLanes_ {};
    uint32_t requestedActiveLaneCount_ = 0u;
    std::array<uint32_t, kDelayProcessorMaxChannels> activeLanes_ {};
    uint32_t activeLaneCount_ = 0u;
    std::array<TopologyPoint, kDelayProcessorMaxChannels> routePoints_ {};
    std::vector<float> routeWeightTarget_;
    std::vector<float> routeWeightCurrent_;
    std::vector<float> centroidWeightTarget_;
    std::vector<float> centroidWeightCurrent_;
    std::vector<float> routeDistanceTarget_;
    std::vector<float> routeDistanceCurrent_;
    std::vector<float> routeGainTarget_;
    std::vector<float> routeGainCurrent_;
    std::vector<float> routeFilterCoeffTarget_;
    std::vector<float> routeFilterCoeffCurrent_;
    std::vector<float> routeFilterState_;
    std::vector<float> edgeEnergyState_;
    std::vector<float> edgePhaseState_;
    std::vector<float> edgePreviousLaunch_;
    std::vector<double> routePairDistanceCache_;
    std::vector<float> routeTurnBiasCache_;
    std::vector<double> routeCentroidDistanceCache_;
    std::vector<uint32_t> activeRouteEdges_;
    uint32_t activeRouteEdgeCount_ = 0u;
    float routeParamSmoothing_ = 1.0f;
    float routeMatrixSmoothing_ = 1.0f;
    float routeDistanceSmoothing_ = 1.0f;
    float routeEnergyAttack_ = 1.0f;
    float routeEnergyRelease_ = 1.0f;
    float nodeEnergyAttack_ = 1.0f;
    float nodeEnergyRelease_ = 1.0f;
    std::array<float, kDelayProcessorMaxChannels> nodeEnergyState_ {};
    float centroidEnergyState_ = 0.0f;
    float centroidPhaseState_ = 0.0f;
    float centroidPreviousLaunch_ = 0.0f;
    bool routeTelemetryActive_ = false;
    uint32_t routeTelemetryPublishCounter_ = 0u;
    uint32_t routeTailEstimateFrames_ = 0u;
    uint32_t routeTailRemaining_ = 0u;
    uint32_t parameterUpdateDepth_ = 0u;
    bool routeTargetsDirty_ = false;
    bool routeTargetsSnap_ = false;
    bool routeTailEstimateDirty_ = false;
    bool routeActivationPending_ = false;
    std::unique_ptr<std::atomic<float>[]> edgeEnergyPublished_;
    std::unique_ptr<std::atomic<float>[]> edgePhasePublished_;
    std::array<std::atomic<float>, kDelayProcessorMaxChannels> nodeEnergyPublished_ {};
    std::atomic<float> centroidEnergyPublished_ { 0.0f };
    std::atomic<float> centroidPhasePublished_ { 0.0f };
    std::atomic<uint32_t> routeTailFramesPublished_ { 0u };
    std::atomic<uint32_t> routeTailRemainingPublished_ { 0u };
};

} // namespace s3g
