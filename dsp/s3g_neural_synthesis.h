#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kNeuralSynthesisNodes = 16u;
constexpr uint32_t kNeuralSynthesisClusters = 4u;
constexpr uint32_t kNeuralNodesPerCluster = 4u;

struct NeuralSynthesisParams {
    float drive = 1.85f;
    float feedback = 0.76f;
    float coupling = 0.38f;
    float hierarchy = 0.52f;
    float phaseShift = 0.28f;
    float brownian = 0.08f;
    float drift = 0.10f;
    float selfModulation = 0.32f;
    float audioFeedback = 0.0f;
    uint32_t seed = 0x4e455552u;
};

inline NeuralSynthesisParams sanitizeNeuralSynthesisParams(NeuralSynthesisParams params)
{
    params.drive = clamp(params.drive, 0.25f, 5.0f);
    params.feedback = clamp(params.feedback, 0.0f, 1.25f);
    params.coupling = clamp(params.coupling, 0.0f, 1.25f);
    params.hierarchy = clamp(params.hierarchy, 0.0f, 1.0f);
    params.phaseShift = clamp(params.phaseShift, 0.0f, 1.0f);
    params.brownian = clamp(params.brownian, 0.0f, 1.0f);
    params.drift = clamp(params.drift, 0.0f, 1.0f);
    params.selfModulation = clamp(params.selfModulation, 0.0f, 1.0f);
    params.audioFeedback = clamp(params.audioFeedback, 0.0f, 1.0f);
    if (params.seed == 0u) params.seed = 1u;
    return params;
}

struct NeuralSynthesisFrame {
    std::array<float, kNeuralSynthesisNodes> nodes {};
    std::array<float, kNeuralSynthesisClusters> clusters {};
    float output = 0.0f;
};

// A clean-room, Tudor-inspired recurrent audio circuit. It is not a model of
// the 80170NX: sixteen leaky tanh nodes form four signed rings, with a sparse
// cross-cluster matrix, hierarchical gates, phase-bearing paths and slow
// control feedback. All state and storage are fixed-size for the audio thread.
class NeuralSynthesisNetwork {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        reset();
    }

    void setParams(NeuralSynthesisParams params)
    {
        const uint32_t previousSeed = params_.seed;
        params_ = sanitizeNeuralSynthesisParams(params);
        if (params_.seed != previousSeed) resetRandomState();
    }

    const NeuralSynthesisParams& params() const { return params_; }

    void reset()
    {
        resetRandomState();
        state_.fill(0.0f);
        output_.fill(0.0f);
        previousOutput_.fill(0.0f);
        clusterOutput_.fill(0.0f);
        phaseMemory_.fill(0.0f);
        brownianState_.fill(0.0f);
        brownianVelocity_.fill(0.0f);
        driftPhase_.fill(0.0);
        driftRate_.fill(0.0);
        controlCounter_ = 0u;
        externalInput_ = 0.0f;
        externalHighpass_ = 0.0f;
        networkFeedback_ = 0.0f;
        frame_ = {};

        for (uint32_t node = 0u; node < kNeuralSynthesisNodes; ++node) {
            const float seedValue = randomSigned() * 0.08f;
            state_[node] = seedValue;
            output_[node] = std::tanh(seedValue * params_.drive);
            driftPhase_[node] = static_cast<double>(randomUnit());
            driftRate_[node] = 0.0015 + static_cast<double>(randomUnit()) * 0.018;
        }
        updateClusterOutputs();
        frame_.nodes = output_;
        frame_.clusters = clusterOutput_;
    }

    NeuralSynthesisFrame process(float externalAudio = 0.0f)
    {
        previousOutput_ = output_;
        const auto previousClusters = clusterOutput_;

        if (controlCounter_ == 0u) updateSlowControl();
        controlCounter_ = (controlCounter_ + 1u) % kControlInterval;

        const float hpPole = std::exp(-2.0f * kPi * 12.0f / static_cast<float>(sampleRate_));
        const float externalHp = externalAudio - externalInput_ + hpPole * externalHighpass_;
        externalInput_ = externalAudio;
        externalHighpass_ = flushDenormal(externalHp);
        const float cyberneticInput = std::tanh((externalHighpass_ * 0.72f + networkFeedback_ * 0.38f) * 1.6f);

        static constexpr std::array<float, kNeuralNodesPerCluster> kRingForward {{ 1.31f, 1.18f, 1.42f, -1.27f }};
        static constexpr std::array<float, kNeuralNodesPerCluster> kRingReverse {{ -0.24f, 0.19f, 0.22f, 0.17f }};
        static constexpr std::array<float, kNeuralNodesPerCluster> kBias {{ -0.10f, 0.07f, -0.045f, 0.12f }};
        static constexpr std::array<float, kNeuralSynthesisClusters> kTauMs {{ 80.0f, 32.0f, 9.5f, 2.0f }};
        static constexpr std::array<float, kNeuralSynthesisClusters * kNeuralSynthesisClusters> kClusterMatrix {{
             0.00f,  0.31f, -0.17f,  0.10f,
            -0.28f,  0.00f,  0.23f,  0.13f,
             0.16f, -0.26f,  0.00f,  0.29f,
            -0.12f,  0.19f, -0.25f,  0.00f,
        }};
        static constexpr std::array<float, kNeuralSynthesisNodes> kAudioSigns {{
             1.0f, -0.7f,  0.5f, -0.9f,
            -0.6f,  1.0f, -0.8f,  0.4f,
             0.7f, -0.5f,  1.0f, -0.7f,
            -1.0f,  0.6f, -0.4f,  0.8f,
        }};

        for (uint32_t cluster = 0u; cluster < kNeuralSynthesisClusters; ++cluster) {
            const uint32_t base = cluster * kNeuralNodesPerCluster;
            const float slowSignal = cluster == 0u
                ? previousClusters[0]
                : 0.67f * previousClusters[0] + 0.33f * previousClusters[cluster - 1u];
            const float timeMod = std::exp2(-params_.selfModulation * slowSignal * (0.35f + 0.28f * static_cast<float>(cluster)));
            const float tauSeconds = kTauMs[cluster] * 0.001f * clamp(timeMod, 0.38f, 2.6f);
            const float alpha = 1.0f - std::exp(-1.0f / std::max(1.0f, static_cast<float>(sampleRate_) * tauSeconds));
            const float parentGate = cluster == 0u ? 1.0f
                : lerp(1.0f, 0.12f + 0.88f * (previousClusters[cluster - 1u] * 0.5f + 0.5f), params_.hierarchy);

            float cross = 0.0f;
            for (uint32_t source = 0u; source < kNeuralSynthesisClusters; ++source) {
                cross += previousClusters[source] * kClusterMatrix[cluster * kNeuralSynthesisClusters + source];
            }

            for (uint32_t local = 0u; local < kNeuralNodesPerCluster; ++local) {
                const uint32_t node = base + local;
                const uint32_t previous = base + ((local + kNeuralNodesPerCluster - 1u) % kNeuralNodesPerCluster);
                const uint32_t next = base + ((local + 1u) % kNeuralNodesPerCluster);
                const float phasePath = phaseShift(previousOutput_[previous], node, cluster);
                const float ring = phasePath * kRingForward[local] + previousOutput_[next] * kRingReverse[local];
                const float drift = std::sin(static_cast<float>(driftPhase_[node]) * 6.28318530717958647692f)
                    * params_.drift * (0.12f + 0.025f * static_cast<float>(cluster));
                const float weightWander = brownianState_[node] * params_.brownian;
                const float bias = kBias[local] + drift + weightWander * 0.20f;
                const float feedback = ring * params_.feedback * (1.0f + weightWander * 0.32f);
                const float hierarchyDrive = cluster == 0u ? 0.0f
                    : previousClusters[cluster - 1u] * params_.hierarchy * 0.62f;
                const float audioDrive = cyberneticInput * params_.audioFeedback * kAudioSigns[node] * 0.58f;
                const float summed = bias + feedback * parentGate + cross * params_.coupling + hierarchyDrive + audioDrive;
                const float target = std::tanh(summed * params_.drive);
                state_[node] = flushDenormal(state_[node] + (target - state_[node]) * alpha);
                output_[node] = std::tanh(state_[node] * 1.42f);
            }
        }

        updateClusterOutputs();
        frame_.nodes = output_;
        frame_.clusters = clusterOutput_;
        frame_.output = std::tanh((clusterOutput_[0] * 0.16f + clusterOutput_[1] * 0.22f
            + clusterOutput_[2] * 0.28f + clusterOutput_[3] * 0.42f) * 1.55f);
        networkFeedback_ = frame_.output;
        return frame_;
    }

    const NeuralSynthesisFrame& frame() const { return frame_; }

private:
    static constexpr uint32_t kControlInterval = 16u;

    void resetRandomState() { randomState_ = params_.seed ? params_.seed : 1u; }

    float randomUnit()
    {
        randomState_ += 0x9e3779b9u;
        uint32_t value = randomState_;
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return static_cast<float>(value & 0x00ffffffu) / static_cast<float>(0x01000000u);
    }

    float randomSigned() { return randomUnit() * 2.0f - 1.0f; }

    void updateSlowControl()
    {
        const float controlSeconds = static_cast<float>(kControlInterval / sampleRate_);
        for (uint32_t node = 0u; node < kNeuralSynthesisNodes; ++node) {
            // A bounded, lightly mean-reverting random walk. The parameter sets
            // its audible depth, while cluster-dependent rates keep nodes from
            // moving as one correlated noise source.
            const uint32_t cluster = node / kNeuralNodesPerCluster;
            const float rate = 0.18f + 0.13f * static_cast<float>(cluster) + 0.025f * static_cast<float>(node % 4u);
            const float step = std::sqrt(std::max(0.0f, controlSeconds * rate));
            brownianVelocity_[node] += randomSigned() * step * 0.065f;
            brownianVelocity_[node] *= std::exp(-controlSeconds * 2.2f);
            brownianState_[node] += brownianVelocity_[node] * step;
            brownianState_[node] -= brownianState_[node] * controlSeconds * 0.11f;
            if (brownianState_[node] > 1.0f) {
                brownianState_[node] = 1.0f;
                brownianVelocity_[node] = -std::fabs(brownianVelocity_[node]) * 0.55f;
            } else if (brownianState_[node] < -1.0f) {
                brownianState_[node] = -1.0f;
                brownianVelocity_[node] = std::fabs(brownianVelocity_[node]) * 0.55f;
            }
            driftPhase_[node] += driftRate_[node] * static_cast<double>(kControlInterval) / sampleRate_;
            driftPhase_[node] -= std::floor(driftPhase_[node]);
        }
    }

    float phaseShift(float input, uint32_t node, uint32_t cluster)
    {
        // First-order allpass sections stand in for the phase-bearing paths
        // Tudor placed inside feedback matrices. The direct/allpass blend keeps
        // phase at zero a transparent topology control.
        const float coefficient = 0.18f + 0.13f * static_cast<float>(cluster);
        const float allpass = -coefficient * input + phaseMemory_[node];
        phaseMemory_[node] = flushDenormal(input + coefficient * allpass);
        return lerp(input, allpass, params_.phaseShift);
    }

    void updateClusterOutputs()
    {
        for (uint32_t cluster = 0u; cluster < kNeuralSynthesisClusters; ++cluster) {
            const uint32_t base = cluster * kNeuralNodesPerCluster;
            clusterOutput_[cluster] = (output_[base] + output_[base + 1u]
                + output_[base + 2u] + output_[base + 3u]) * 0.25f;
        }
    }

    double sampleRate_ = 48000.0;
    NeuralSynthesisParams params_ {};
    NeuralSynthesisFrame frame_ {};
    std::array<float, kNeuralSynthesisNodes> state_ {};
    std::array<float, kNeuralSynthesisNodes> output_ {};
    std::array<float, kNeuralSynthesisNodes> previousOutput_ {};
    std::array<float, kNeuralSynthesisClusters> clusterOutput_ {};
    std::array<float, kNeuralSynthesisNodes> phaseMemory_ {};
    std::array<float, kNeuralSynthesisNodes> brownianState_ {};
    std::array<float, kNeuralSynthesisNodes> brownianVelocity_ {};
    std::array<double, kNeuralSynthesisNodes> driftPhase_ {};
    std::array<double, kNeuralSynthesisNodes> driftRate_ {};
    uint32_t controlCounter_ = 0u;
    uint32_t randomState_ = 1u;
    float externalInput_ = 0.0f;
    float externalHighpass_ = 0.0f;
    float networkFeedback_ = 0.0f;
};

constexpr uint32_t kNeuralCaptureSize = 2048u;
constexpr uint32_t kNeuralCaptureMipLevels = 8u;

class NeuralWaveformCapture {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        reset();
    }

    void reset()
    {
        historyPulsaret_.fill(0.0f);
        historyEnvelope_.fill(0.0f);
        historyFm_.fill(0.0f);
        writeIndex_ = 0u;
        filled_ = 0u;
        generation_ = 0u;
        decimatorPhase_ = 0.0;
        decimatorPulsaret_ = 0.0f;
        decimatorEnvelope_ = 0.0f;
        decimatorFm_ = 0.0f;
        morph_ = 1.0f;
        initializeTables();
        previousPulsaret_ = pulsaret_;
        previousEnvelope_ = envelope_;
        previousFm_ = fm_;
    }

    void push(const NeuralSynthesisFrame& frame)
    {
        constexpr double kCaptureRate = 4096.0;
        const float pole = std::exp(-2.0f * kPi * 1200.0f / static_cast<float>(sampleRate_));
        const float pulsaretSource = clamp(frame.clusters[3] * 0.74f + frame.nodes[13] * 0.26f, -1.0f, 1.0f);
        const float envelopeSource = clamp(std::fabs(frame.clusters[0] * 0.42f + frame.clusters[1] * 0.58f), 0.0f, 1.0f);
        const float fmSource = clamp(frame.clusters[2] * 0.62f + frame.clusters[3] * 0.38f, -1.0f, 1.0f);
        decimatorPulsaret_ = pulsaretSource * (1.0f - pole) + decimatorPulsaret_ * pole;
        decimatorEnvelope_ = envelopeSource * (1.0f - pole) + decimatorEnvelope_ * pole;
        decimatorFm_ = fmSource * (1.0f - pole) + decimatorFm_ * pole;
        morph_ = std::min(1.0f, morph_ + 1.0f / std::max(1.0f, static_cast<float>(sampleRate_ * 0.080)));

        decimatorPhase_ += kCaptureRate / sampleRate_;
        while (decimatorPhase_ >= 1.0) {
            decimatorPhase_ -= 1.0;
            historyPulsaret_[writeIndex_] = decimatorPulsaret_;
            historyEnvelope_[writeIndex_] = decimatorEnvelope_;
            historyFm_[writeIndex_] = decimatorFm_;
            writeIndex_ = (writeIndex_ + 1u) % kNeuralCaptureSize;
            filled_ = std::min<uint32_t>(filled_ + 1u, kNeuralCaptureSize);
        }
    }

    bool ready() const { return filled_ == kNeuralCaptureSize; }
    uint32_t filled() const { return filled_; }

    bool capture()
    {
        if (filled_ < 64u) return false;
        previousPulsaret_ = pulsaret_;
        previousEnvelope_ = envelope_;
        previousFm_ = fm_;
        for (uint32_t index = 0u; index < kNeuralCaptureSize; ++index) {
            const uint32_t available = std::min<uint32_t>(filled_, kNeuralCaptureSize);
            const uint32_t padding = kNeuralCaptureSize - available;
            const uint32_t historyOffset = index < padding ? 0u : index - padding;
            const uint32_t oldest = (writeIndex_ + kNeuralCaptureSize - available) % kNeuralCaptureSize;
            const uint32_t source = (oldest + std::min<uint32_t>(historyOffset, available - 1u)) % kNeuralCaptureSize;
            pulsaret_[0][index] = historyPulsaret_[source];
            envelope_[index] = historyEnvelope_[source];
            fm_[0][index] = historyFm_[source];
        }

        makeSeamlessAndNormalize(pulsaret_[0]);
        makeSeamlessAndNormalize(fm_[0]);
        normalizeEnvelope();
        buildMipmaps(pulsaret_);
        buildMipmaps(fm_);
        morph_ = 0.0f;
        ++generation_;
        return true;
    }

    float pulsaret(float phase, float phaseIncrement) const
    {
        return lerp(sampleMip(previousPulsaret_, phase, phaseIncrement),
            sampleMip(pulsaret_, phase, phaseIncrement), morph_);
    }
    float fm(float phase, float phaseIncrement = 1.0f / static_cast<float>(kNeuralCaptureSize)) const
    {
        return lerp(sampleMip(previousFm_, phase, phaseIncrement), sampleMip(fm_, phase, phaseIncrement), morph_);
    }
    float envelope(float phase) const
    {
        return lerp(sampleTable(previousEnvelope_, phase), sampleTable(envelope_, phase), morph_);
    }
    uint32_t generation() const { return generation_; }

private:
    using Table = std::array<float, kNeuralCaptureSize>;
    using MipBank = std::array<Table, kNeuralCaptureMipLevels>;

    static float sampleTable(const Table& table, float phase)
    {
        phase -= std::floor(phase);
        const float position = phase * static_cast<float>(kNeuralCaptureSize);
        const uint32_t index = static_cast<uint32_t>(position) % kNeuralCaptureSize;
        const uint32_t next = (index + 1u) % kNeuralCaptureSize;
        return lerp(table[index], table[next], position - static_cast<float>(index));
    }

    static float sampleMip(const MipBank& bank, float phase, float phaseIncrement)
    {
        const float footprint = std::max(1.0f, std::fabs(phaseIncrement) * static_cast<float>(kNeuralCaptureSize));
        const float levelValue = clamp(std::log2(footprint), 0.0f, static_cast<float>(kNeuralCaptureMipLevels - 1u));
        const uint32_t level = static_cast<uint32_t>(levelValue);
        const uint32_t next = std::min<uint32_t>(level + 1u, kNeuralCaptureMipLevels - 1u);
        return lerp(sampleTable(bank[level], phase), sampleTable(bank[next], phase), levelValue - static_cast<float>(level));
    }

    static void makeSeamlessAndNormalize(Table& table)
    {
        const float mismatch = table.back() - table.front();
        float mean = 0.0f;
        for (uint32_t index = 0u; index < kNeuralCaptureSize; ++index) {
            table[index] -= mismatch * static_cast<float>(index) / static_cast<float>(kNeuralCaptureSize - 1u);
            mean += table[index];
        }
        mean /= static_cast<float>(kNeuralCaptureSize);
        float peak = 1.0e-6f;
        for (float& sample : table) {
            sample -= mean;
            peak = std::max(peak, std::fabs(sample));
        }
        const float gain = 0.98f / peak;
        for (float& sample : table) sample *= gain;
    }

    void normalizeEnvelope()
    {
        float minimum = envelope_[0];
        float maximum = envelope_[0];
        for (float sample : envelope_) {
            minimum = std::min(minimum, sample);
            maximum = std::max(maximum, sample);
        }
        const float inverseRange = 1.0f / std::max(1.0e-5f, maximum - minimum);
        constexpr uint32_t kEdgeSamples = 96u;
        for (uint32_t index = 0u; index < kNeuralCaptureSize; ++index) {
            float fade = 1.0f;
            if (index < kEdgeSamples) fade = static_cast<float>(index) / static_cast<float>(kEdgeSamples);
            else if (index >= kNeuralCaptureSize - kEdgeSamples) {
                fade = static_cast<float>(kNeuralCaptureSize - 1u - index) / static_cast<float>(kEdgeSamples);
            }
            envelope_[index] = clamp((envelope_[index] - minimum) * inverseRange * fade, 0.0f, 1.0f);
        }
    }

    static void buildMipmaps(MipBank& bank)
    {
        Table scratch {};
        for (uint32_t level = 1u; level < kNeuralCaptureMipLevels; ++level) {
            const uint32_t passes = 1u << (level - 1u);
            bank[level] = bank[level - 1u];
            for (uint32_t pass = 0u; pass < passes; ++pass) {
                for (uint32_t index = 0u; index < kNeuralCaptureSize; ++index) {
                    const uint32_t m2 = (index + kNeuralCaptureSize - 2u) % kNeuralCaptureSize;
                    const uint32_t m1 = (index + kNeuralCaptureSize - 1u) % kNeuralCaptureSize;
                    const uint32_t p1 = (index + 1u) % kNeuralCaptureSize;
                    const uint32_t p2 = (index + 2u) % kNeuralCaptureSize;
                    scratch[index] = (bank[level][m2] + bank[level][p2]
                        + 4.0f * (bank[level][m1] + bank[level][p1]) + 6.0f * bank[level][index]) * (1.0f / 16.0f);
                }
                bank[level] = scratch;
            }
        }
    }

    void initializeTables()
    {
        for (uint32_t index = 0u; index < kNeuralCaptureSize; ++index) {
            const float phase = static_cast<float>(index) / static_cast<float>(kNeuralCaptureSize);
            pulsaret_[0][index] = std::sin(phase * 6.28318530717958647692f);
            fm_[0][index] = std::sin(phase * 6.28318530717958647692f * 2.0f + 0.41f);
            envelope_[index] = 0.5f - 0.5f * std::cos(phase * 6.28318530717958647692f);
        }
        buildMipmaps(pulsaret_);
        buildMipmaps(fm_);
    }

    double sampleRate_ = 48000.0;
    double decimatorPhase_ = 0.0;
    float decimatorPulsaret_ = 0.0f;
    float decimatorEnvelope_ = 0.0f;
    float decimatorFm_ = 0.0f;
    std::array<float, kNeuralCaptureSize> historyPulsaret_ {};
    std::array<float, kNeuralCaptureSize> historyEnvelope_ {};
    std::array<float, kNeuralCaptureSize> historyFm_ {};
    MipBank pulsaret_ {};
    MipBank fm_ {};
    Table envelope_ {};
    MipBank previousPulsaret_ {};
    MipBank previousFm_ {};
    Table previousEnvelope_ {};
    uint32_t writeIndex_ = 0u;
    uint32_t filled_ = 0u;
    uint32_t generation_ = 0u;
    float morph_ = 1.0f;
};

} // namespace s3g
