#pragma once

#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kNodeTrackMixerMaxChannels = 128;
constexpr uint32_t kNodeTrackMixerMaxNodes = 16;
constexpr uint32_t kAmbiNodeBusMixerMaxNodes = 8;
constexpr uint32_t kAmbiNodeBusMixerChannelsPerNode = 16;

enum class NodeTrackLayout : uint32_t {
    Stereo = 0,
    Quad = 1,
    FiveZero = 2,
    SixZero = 3,
    SevenZero = 4,
    Octo = 5,
    Cube = 6,
    FiveZeroTwo = 7,
    SevenZeroTwo = 8,
    FiveZeroFour = 9,
    SevenZeroFour = 10,
    Ring12 = 11,
    Ring16 = 12,
    DoubleRing16 = 13,
    DoubleRing24 = 14,
};

constexpr uint32_t kNodeTrackRegularLayoutCount = 15;

enum class NodeTrackMixMode : uint32_t {
    SpatialObjects = 0,
    StackedShapes = 1,
};

struct NodeTrackPoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct NodeTrackNode {
    bool active = true;
    float levelDb = 0.0f;
    NodeTrackLayout sourceLayout = NodeTrackLayout::Octo;
    uint32_t sourceChannels = 8;
    uint32_t inputStart = 1;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float scale = 1.0f;
    float focus = 1.0f;
    float rotateAzDeg = 0.0f;
    float rotateElDeg = 0.0f;
};

struct NodeTrackMixerParams {
    NodeTrackLayout outputLayout = NodeTrackLayout::Octo;
    uint32_t outputChannels = 8;
    uint32_t nodeCount = 4;
    NodeTrackMixMode mixMode = NodeTrackMixMode::SpatialObjects;
    std::array<NodeTrackNode, kNodeTrackMixerMaxNodes> nodes {};
    float cursorInfluence = 1.0f;
    float cursorX = 0.0f;
    float cursorY = 0.0f;
    float cursorZ = 0.0f;
    float stackPosition = 1.0f;
    float cursorRadius = 1.0f;
    float cursorFocus = 1.0f;
    float cursorGate = 0.02f;
    float outputGainDb = 0.0f;
    bool lockZ = true;
};

enum class AmbiNodeTrackOrder : uint32_t {
    O1 = 0,
    O2 = 1,
    O3 = 2,
};

struct AmbiNodeTrackNode {
    bool active = true;
    float levelDb = 0.0f;
    uint32_t inputStart = 1;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float radius = 0.9f;
    float focus = 1.0f;
};

struct AmbiNodeTrackMixerParams {
    AmbiNodeTrackOrder order = AmbiNodeTrackOrder::O3;
    uint32_t nodeCount = kAmbiNodeBusMixerMaxNodes;
    NodeTrackMixMode mixMode = NodeTrackMixMode::SpatialObjects;
    std::array<AmbiNodeTrackNode, kNodeTrackMixerMaxNodes> nodes {};
    float cursorInfluence = 1.0f;
    float cursorX = 0.0f;
    float cursorY = 0.0f;
    float cursorZ = 0.0f;
    float stackPosition = 1.0f;
    float cursorRadius = 1.25f;
    float cursorFocus = 1.0f;
    float cursorGate = 0.02f;
    float outputGainDb = 0.0f;
    bool lockZ = true;
};

inline float nodeTrackDbToGain(float db)
{
    return db <= -59.9f ? 0.0f : std::pow(10.0f, db / 20.0f);
}

inline const char* nodeTrackLayoutName(NodeTrackLayout layout)
{
    switch (layout) {
    case NodeTrackLayout::Stereo: return "STEREO";
    case NodeTrackLayout::Quad: return "QUAD";
    case NodeTrackLayout::FiveZero: return "5.0";
    case NodeTrackLayout::SixZero: return "6.0";
    case NodeTrackLayout::SevenZero: return "7.0";
    case NodeTrackLayout::Octo: return "OCTO";
    case NodeTrackLayout::Cube: return "CUBE";
    case NodeTrackLayout::FiveZeroTwo: return "5.0.2";
    case NodeTrackLayout::SevenZeroTwo: return "7.0.2";
    case NodeTrackLayout::FiveZeroFour: return "5.0.4";
    case NodeTrackLayout::SevenZeroFour: return "7.0.4";
    case NodeTrackLayout::Ring12: return "RING12";
    case NodeTrackLayout::Ring16: return "RING16";
    case NodeTrackLayout::DoubleRing16: return "DBL16";
    case NodeTrackLayout::DoubleRing24: return "DBL24";
    default: return "OCTO";
    }
}

inline uint32_t nodeTrackDefaultChannelsForLayout(NodeTrackLayout layout)
{
    switch (layout) {
    case NodeTrackLayout::Stereo: return 2;
    case NodeTrackLayout::Quad: return 4;
    case NodeTrackLayout::FiveZero: return 5;
    case NodeTrackLayout::SixZero: return 6;
    case NodeTrackLayout::SevenZero: return 7;
    case NodeTrackLayout::Octo: return 8;
    case NodeTrackLayout::Cube: return 8;
    case NodeTrackLayout::FiveZeroTwo: return 7;
    case NodeTrackLayout::SevenZeroTwo: return 9;
    case NodeTrackLayout::FiveZeroFour: return 9;
    case NodeTrackLayout::SevenZeroFour: return 11;
    case NodeTrackLayout::Ring12: return 12;
    case NodeTrackLayout::Ring16: return 16;
    case NodeTrackLayout::DoubleRing16: return 16;
    case NodeTrackLayout::DoubleRing24: return 24;
    default: return 8;
    }
}

inline NodeTrackLayout nodeTrackRegularLayoutFromIndex(uint32_t index)
{
    switch (index) {
    case 0: return NodeTrackLayout::Stereo;
    case 1: return NodeTrackLayout::Quad;
    case 2: return NodeTrackLayout::FiveZero;
    case 3: return NodeTrackLayout::SixZero;
    case 4: return NodeTrackLayout::SevenZero;
    case 5: return NodeTrackLayout::Octo;
    case 6: return NodeTrackLayout::Cube;
    case 7: return NodeTrackLayout::FiveZeroTwo;
    case 8: return NodeTrackLayout::SevenZeroTwo;
    case 9: return NodeTrackLayout::FiveZeroFour;
    case 10: return NodeTrackLayout::SevenZeroFour;
    case 11: return NodeTrackLayout::Ring12;
    case 12: return NodeTrackLayout::Ring16;
    case 13: return NodeTrackLayout::DoubleRing16;
    case 14: return NodeTrackLayout::DoubleRing24;
    default: return NodeTrackLayout::Octo;
    }
}

inline uint32_t nodeTrackRegularLayoutIndex(NodeTrackLayout layout)
{
    switch (layout) {
    case NodeTrackLayout::Stereo: return 0;
    case NodeTrackLayout::Quad: return 1;
    case NodeTrackLayout::FiveZero: return 2;
    case NodeTrackLayout::SixZero: return 3;
    case NodeTrackLayout::SevenZero: return 4;
    case NodeTrackLayout::Octo: return 5;
    case NodeTrackLayout::Cube: return 6;
    case NodeTrackLayout::FiveZeroTwo: return 7;
    case NodeTrackLayout::SevenZeroTwo: return 8;
    case NodeTrackLayout::FiveZeroFour: return 9;
    case NodeTrackLayout::SevenZeroFour: return 10;
    case NodeTrackLayout::Ring12: return 11;
    case NodeTrackLayout::Ring16: return 12;
    case NodeTrackLayout::DoubleRing16: return 13;
    case NodeTrackLayout::DoubleRing24: return 14;
    default: return 5;
    }
}

inline NodeTrackLayout nodeTrackRegularLayout(NodeTrackLayout layout)
{
    return nodeTrackRegularLayoutFromIndex(nodeTrackRegularLayoutIndex(layout));
}

inline uint32_t ambiNodeTrackOrderChannels(AmbiNodeTrackOrder order)
{
    switch (order) {
    case AmbiNodeTrackOrder::O1: return 4;
    case AmbiNodeTrackOrder::O2: return 10;
    case AmbiNodeTrackOrder::O3: return 16;
    default: return 16;
    }
}

inline NodeTrackPoint nodeTrackAzElPoint(float azDeg, float elDeg = 0.0f, float distance = 1.0f)
{
    const float az = azDeg * kPi / 180.0f;
    const float el = elDeg * kPi / 180.0f;
    NodeTrackPoint p {};
    p.x = -std::sin(az) * std::cos(el) * distance;
    p.y = std::cos(az) * std::cos(el) * distance;
    p.z = std::sin(el) * distance;
    return p;
}

inline NodeTrackPoint nodeTrackRotatePoint(NodeTrackPoint p, float azDeg, float elDeg)
{
    const float az = azDeg * kPi / 180.0f;
    const float el = elDeg * kPi / 180.0f;
    const float ca = std::cos(az);
    const float sa = std::sin(az);
    const float ce = std::cos(el);
    const float se = std::sin(el);

    const float x1 = p.x * ca - p.y * sa;
    const float y1 = p.x * sa + p.y * ca;
    const float z1 = p.z;

    p.x = x1 * ce + z1 * se;
    p.y = y1;
    p.z = -x1 * se + z1 * ce;
    return p;
}

inline NodeTrackPoint nodeTrackLayoutPoint(uint32_t channel, uint32_t count, NodeTrackLayout layout)
{
    count = std::max<uint32_t>(1u, count);
    const uint32_t idx = channel % count;
    NodeTrackPoint p {};
    if (layout == NodeTrackLayout::Stereo) {
        static constexpr float az[] { 30.0f, -30.0f };
        return nodeTrackAzElPoint(az[idx % 2u]);
    }
    if (layout == NodeTrackLayout::Quad) {
        static constexpr float az[] { 45.0f, -45.0f, -135.0f, 135.0f };
        return nodeTrackAzElPoint(az[idx % 4u]);
    }
    if (layout == NodeTrackLayout::FiveZero) {
        static constexpr float az[] { 30.0f, -30.0f, 0.0f, 110.0f, -110.0f };
        return nodeTrackAzElPoint(az[idx % 5u]);
    }
    if (layout == NodeTrackLayout::SixZero) {
        static constexpr float az[] { 30.0f, -30.0f, -90.0f, -150.0f, 150.0f, 90.0f };
        return nodeTrackAzElPoint(az[idx % 6u]);
    }
    if (layout == NodeTrackLayout::SevenZero) {
        static constexpr float az[] { 30.0f, -30.0f, 0.0f, 90.0f, -90.0f, 135.0f, -135.0f };
        return nodeTrackAzElPoint(az[idx % 7u]);
    }
    if (layout == NodeTrackLayout::FiveZeroTwo) {
        if (idx < 5u) return nodeTrackLayoutPoint(idx, 5u, NodeTrackLayout::FiveZero);
        static constexpr float az[] { 45.0f, -45.0f };
        return nodeTrackAzElPoint(az[(idx - 5u) % 2u], 60.0f, 0.88f);
    }
    if (layout == NodeTrackLayout::SevenZeroTwo) {
        if (idx < 7u) return nodeTrackLayoutPoint(idx, 7u, NodeTrackLayout::SevenZero);
        static constexpr float az[] { 45.0f, -45.0f };
        return nodeTrackAzElPoint(az[(idx - 7u) % 2u], 60.0f, 0.88f);
    }
    if (layout == NodeTrackLayout::FiveZeroFour) {
        if (idx < 5u) return nodeTrackLayoutPoint(idx, 5u, NodeTrackLayout::FiveZero);
        static constexpr float az[] { 45.0f, -45.0f, -135.0f, 135.0f };
        return nodeTrackAzElPoint(az[(idx - 5u) % 4u], 55.0f, 0.9f);
    }
    if (layout == NodeTrackLayout::SevenZeroFour) {
        if (idx < 7u) return nodeTrackLayoutPoint(idx, 7u, NodeTrackLayout::SevenZero);
        static constexpr float az[] { 45.0f, -45.0f, -135.0f, 135.0f };
        return nodeTrackAzElPoint(az[(idx - 7u) % 4u], 55.0f, 0.9f);
    }
    if (layout == NodeTrackLayout::Cube) {
        p.x = (idx & 1u) ? -1.0f : 1.0f;
        p.y = (idx & 2u) ? -1.0f : 1.0f;
        p.z = (idx & 4u) ? 1.0f : -1.0f;
        return p;
    }
    uint32_t layer = 0;
    uint32_t pos = idx;
    uint32_t ringCount = count;
    float ring = 1.0f;
    float elevationDeg = 0.0f;
    if (layout == NodeTrackLayout::DoubleRing16 || layout == NodeTrackLayout::DoubleRing24) {
        const uint32_t half = std::max<uint32_t>(1u, count / 2u);
        layer = idx < half ? 0u : 1u;
        pos = idx % half;
        ringCount = half;
        ring = layer ? 0.74f : 1.0f;
        elevationDeg = layer ? 40.0f : -6.0f;
    }
    const float azDeg = -45.0f - static_cast<float>(pos) * 360.0f / static_cast<float>(std::max<uint32_t>(1u, ringCount));
    const float az = azDeg * kPi / 180.0f;
    const float el = elevationDeg * kPi / 180.0f;
    p.x = -std::sin(az) * std::cos(el) * ring;
    p.y = std::cos(az) * std::cos(el) * ring;
    p.z = std::sin(el);
    return p;
}

inline float nodeTrackApplyGate(float v, float gate)
{
    gate = clamp(gate, 0.0f, 0.95f);
    v = clamp(v, 0.0f, 1.0f);
    if (gate <= 0.0f) return v;
    return std::pow(v, 1.0f + gate * 3.0f);
}

inline float nodeTrackCursorWeight(float distance, float radius, float focus, float gate)
{
    radius = std::max(0.001f, radius);
    if (distance >= radius) return 0.0f;
    const float edgeToCenter = clamp(1.0f - distance / radius, 0.0f, 1.0f);
    const float raw = edgeToCenter * edgeToCenter * (3.0f - 2.0f * edgeToCenter);
    const float curve = clamp(focus, 0.5f, 4.0f);
    return nodeTrackApplyGate(std::pow(raw, curve), gate);
}

inline NodeTrackMixerParams sanitizeNodeTrackMixerParams(NodeTrackMixerParams p)
{
    p.outputLayout = nodeTrackRegularLayout(p.outputLayout);
    p.outputChannels = nodeTrackDefaultChannelsForLayout(p.outputLayout);
    p.nodeCount = static_cast<uint32_t>(std::round(clamp(static_cast<float>(p.nodeCount), 1.0f, 16.0f)));
    p.mixMode = NodeTrackMixMode::SpatialObjects;
    uint32_t nextInputStart = 1u;
    for (auto& n : p.nodes) {
        n.levelDb = clamp(n.levelDb, -60.0f, 12.0f);
        n.sourceLayout = nodeTrackRegularLayout(n.sourceLayout);
        n.sourceChannels = nodeTrackDefaultChannelsForLayout(n.sourceLayout);
        n.inputStart = std::min<uint32_t>(nextInputStart, kNodeTrackMixerMaxChannels + 1u);
        nextInputStart = std::min<uint32_t>(kNodeTrackMixerMaxChannels + 1u, nextInputStart + n.sourceChannels);
        n.x = clamp(n.x, -2.0f, 2.0f);
        n.y = clamp(n.y, -2.0f, 2.0f);
        n.z = clamp(n.z, -2.0f, 2.0f);
        n.scale = clamp(n.scale, 0.05f, 4.0f);
        n.focus = clamp(n.focus, 0.5f, 4.0f);
        n.rotateAzDeg = clamp(n.rotateAzDeg, -180.0f, 180.0f);
        n.rotateElDeg = clamp(n.rotateElDeg, -90.0f, 90.0f);
    }
    p.cursorInfluence = clamp(p.cursorInfluence, 0.0f, 1.0f);
    p.cursorX = clamp(p.cursorX, -2.0f, 2.0f);
    p.cursorY = clamp(p.cursorY, -2.0f, 2.0f);
    p.cursorZ = clamp(p.cursorZ, -2.0f, 2.0f);
    if (p.lockZ) {
        p.cursorZ = 0.0f;
        for (auto& n : p.nodes) n.z = 0.0f;
    }
    p.stackPosition = clamp(p.stackPosition, 1.0f, 16.0f);
    p.cursorRadius = clamp(p.cursorRadius, 0.05f, 8.0f);
    p.cursorFocus = clamp(p.cursorFocus, 0.5f, 2.0f);
    p.cursorGate = clamp(p.cursorGate, 0.0f, 0.95f);
    p.outputGainDb = clamp(p.outputGainDb, -60.0f, 12.0f);
    return p;
}

inline AmbiNodeTrackMixerParams sanitizeAmbiNodeTrackMixerParams(AmbiNodeTrackMixerParams p)
{
    p.order = AmbiNodeTrackOrder::O3;
    p.nodeCount = static_cast<uint32_t>(std::round(clamp(static_cast<float>(p.nodeCount), 1.0f, static_cast<float>(kAmbiNodeBusMixerMaxNodes))));
    p.mixMode = NodeTrackMixMode::SpatialObjects;
    for (uint32_t i = 0; i < kNodeTrackMixerMaxNodes; ++i) {
        auto& n = p.nodes[i];
        n.levelDb = clamp(n.levelDb, -60.0f, 12.0f);
        n.inputStart = std::min<uint32_t>(kNodeTrackMixerMaxChannels, i * kAmbiNodeBusMixerChannelsPerNode + 1u);
        n.x = clamp(n.x, -2.0f, 2.0f);
        n.y = clamp(n.y, -2.0f, 2.0f);
        n.z = clamp(n.z, -2.0f, 2.0f);
        n.radius = clamp(n.radius, 0.05f, 8.0f);
        n.focus = clamp(n.focus, 0.5f, 4.0f);
    }
    p.cursorInfluence = clamp(p.cursorInfluence, 0.0f, 1.0f);
    p.cursorX = clamp(p.cursorX, -2.0f, 2.0f);
    p.cursorY = clamp(p.cursorY, -2.0f, 2.0f);
    p.cursorZ = clamp(p.cursorZ, -2.0f, 2.0f);
    if (p.lockZ) {
        p.cursorZ = 0.0f;
        for (auto& n : p.nodes) n.z = 0.0f;
    }
    p.stackPosition = clamp(p.stackPosition, 1.0f, 16.0f);
    p.cursorRadius = clamp(p.cursorRadius, 0.05f, 8.0f);
    p.cursorFocus = clamp(p.cursorFocus, 0.5f, 2.0f);
    p.cursorGate = clamp(p.cursorGate, 0.0f, 0.95f);
    p.outputGainDb = clamp(p.outputGainDb, -60.0f, 12.0f);
    return p;
}

class AmbiNodeTrackMixer {
public:
    AmbiNodeTrackMixer() { setParams(params_); }
    void prepare(double sampleRate) { sampleRate_ = std::max(1000.0, sampleRate); (void)sampleRate_; }
    void reset() {}
    void setParams(AmbiNodeTrackMixerParams params) { params_ = sanitizeAmbiNodeTrackMixerParams(params); rebuildWeights(); }
    const AmbiNodeTrackMixerParams& params() const { return params_; }
    const std::array<float, kNodeTrackMixerMaxNodes>& nodeWeights() const { return weights_; }
    uint32_t ambiChannels() const { return kAmbiNodeBusMixerChannelsPerNode; }

    template <typename Sample>
    void process(Sample* const* inputs, uint32_t inputChannels, Sample* const* outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        for (uint32_t ch = 0; ch < std::min<uint32_t>(outputChannels, kNodeTrackMixerMaxChannels); ++ch) {
            if (outputs[ch]) std::fill(outputs[ch], outputs[ch] + frames, Sample {});
        }
        if (!inputs) return;
        const uint32_t lanes = std::min<uint32_t>({ ambiChannels(), outputChannels, kNodeTrackMixerMaxChannels });
        for (uint32_t i = 0; i < frames; ++i) {
            for (uint32_t node = 0; node < params_.nodeCount; ++node) {
                const auto& n = params_.nodes[node];
                const float w = weights_[node];
                if (w <= 0.000001f) continue;
                const uint32_t inputStart = node * kAmbiNodeBusMixerChannelsPerNode;
                for (uint32_t lane = 0; lane < lanes; ++lane) {
                    const uint32_t inCh = inputStart + lane;
                    if (inCh < inputChannels && inputs[inCh] && outputs[lane]) {
                        outputs[lane][i] += static_cast<Sample>(inputs[inCh][i] * static_cast<Sample>(w));
                    }
                }
            }
            for (uint32_t ch = 0; ch < lanes; ++ch) {
                if (outputs[ch]) outputs[ch][i] = static_cast<Sample>(flushDenormal(std::clamp(static_cast<float>(outputs[ch][i]), -8.0f, 8.0f)));
            }
        }
    }

private:
    void rebuildWeights()
    {
        weights_.fill(0.0f);
        const float out = nodeTrackDbToGain(params_.outputGainDb);
        std::array<float, kNodeTrackMixerMaxNodes> fieldWeights {};
        float fieldPower = 0.0f;
        float fieldPeak = 0.0f;
        for (uint32_t node = 0; node < params_.nodeCount; ++node) {
            const auto& n = params_.nodes[node];
            if (!n.active) continue;
            float raw = 1.0f;
            if (params_.mixMode == NodeTrackMixMode::StackedShapes) {
                raw = nodeTrackCursorWeight(std::abs(static_cast<float>(node + 1u) - params_.stackPosition),
                                            params_.cursorRadius, params_.cursorFocus, params_.cursorGate);
            } else {
                const float dx = n.x - params_.cursorX;
                const float dy = n.y - params_.cursorY;
                const float dz = n.z - params_.cursorZ;
                raw = nodeTrackCursorWeight(std::sqrt(dx * dx + dy * dy + dz * dz),
                                            n.radius,
                                            n.focus,
                                            params_.cursorGate);
            }
            const float weighted = nodeTrackDbToGain(n.levelDb) * raw;
            fieldWeights[node] = weighted;
            fieldPower += weighted * weighted;
            fieldPeak = std::max(fieldPeak, weighted);
        }
        const float fieldScale = fieldPower > 0.00000001f ? fieldPeak / std::sqrt(fieldPower) : 0.0f;
        for (uint32_t node = 0; node < params_.nodeCount; ++node) {
            const auto& n = params_.nodes[node];
            if (!n.active) continue;
            const float level = nodeTrackDbToGain(n.levelDb);
            const float cursor = (1.0f - params_.cursorInfluence) * level
                + params_.cursorInfluence * fieldWeights[node] * fieldScale;
            weights_[node] = cursor * out;
        }
    }

    double sampleRate_ = 48000.0;
    AmbiNodeTrackMixerParams params_ {};
    std::array<float, kNodeTrackMixerMaxNodes> weights_ {};
};

class NodeTrackMixer {
public:
    NodeTrackMixer() { setParams(params_); }
    void prepare(double sampleRate) { sampleRate_ = std::max(1000.0, sampleRate); (void)sampleRate_; }
    void reset() {}
    void setParams(NodeTrackMixerParams params) { params_ = sanitizeNodeTrackMixerParams(params); rebuildWeights(); }
    const NodeTrackMixerParams& params() const { return params_; }
    const std::array<float, kNodeTrackMixerMaxNodes>& nodeWeights() const { return nodeWeights_; }

    template <typename Sample>
    void process(Sample* const* inputs, uint32_t inputChannels, Sample* const* outputs, uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        for (uint32_t ch = 0; ch < std::min<uint32_t>(outputChannels, kNodeTrackMixerMaxChannels); ++ch) {
            if (outputs[ch]) std::fill(outputs[ch], outputs[ch] + frames, Sample {});
        }
        if (!inputs) return;
        const uint32_t outCh = std::min<uint32_t>({ params_.outputChannels, outputChannels, kNodeTrackMixerMaxChannels });
        for (uint32_t i = 0; i < frames; ++i) {
            for (uint32_t node = 0; node < params_.nodeCount; ++node) {
                const auto& n = params_.nodes[node];
                if (!n.active || nodeWeights_[node] <= 0.000001f) continue;
                const uint32_t srcCh = std::min<uint32_t>(n.sourceChannels, kNodeTrackMixerMaxChannels);
                const uint32_t inputStart = n.inputStart - 1u;
                for (uint32_t sc = 0; sc < srcCh; ++sc) {
                    const uint32_t inCh = inputStart + sc;
                    if (inCh >= inputChannels || !inputs[inCh]) continue;
                    const float sig = static_cast<float>(inputs[inCh][i]);
                    if (sig == 0.0f) continue;
                    for (uint32_t ch = 0; ch < outCh; ++ch) {
                        const float w = weights_[node][sc][ch];
                        if (w > 0.000001f && outputs[ch]) {
                            outputs[ch][i] += static_cast<Sample>(sig * w);
                        }
                    }
                }
            }
            for (uint32_t ch = 0; ch < outCh; ++ch) {
                if (outputs[ch]) outputs[ch][i] = static_cast<Sample>(flushDenormal(std::clamp(static_cast<float>(outputs[ch][i]), -8.0f, 8.0f)));
            }
        }
    }

private:
    void rebuildWeights()
    {
        for (auto& node : weights_) for (auto& src : node) src.fill(0.0f);
        nodeWeights_.fill(0.0f);
        const float out = nodeTrackDbToGain(params_.outputGainDb);
        std::array<float, kNodeTrackMixerMaxNodes> fieldWeights {};
        float fieldPower = 0.0f;
        float fieldPeak = 0.0f;
        for (uint32_t node = 0; node < params_.nodeCount; ++node) {
            const auto& n = params_.nodes[node];
            if (!n.active) continue;
            const float distance = params_.mixMode == NodeTrackMixMode::StackedShapes
                ? std::abs(static_cast<float>(node + 1u) - params_.stackPosition)
                : std::sqrt((n.x - params_.cursorX) * (n.x - params_.cursorX)
                    + (n.y - params_.cursorY) * (n.y - params_.cursorY)
                    + (n.z - params_.cursorZ) * (n.z - params_.cursorZ));
            const float radius = params_.mixMode == NodeTrackMixMode::StackedShapes
                ? params_.cursorRadius
                : n.scale;
            const float raw = nodeTrackCursorWeight(distance, radius, n.focus, params_.cursorGate);
            const float weighted = nodeTrackDbToGain(n.levelDb) * raw;
            fieldWeights[node] = weighted;
            fieldPower += weighted * weighted;
            fieldPeak = std::max(fieldPeak, weighted);
        }
        const float fieldScale = fieldPower > 0.00000001f ? fieldPeak / std::sqrt(fieldPower) : 0.0f;
        for (uint32_t node = 0; node < params_.nodeCount; ++node) {
            const auto& n = params_.nodes[node];
            if (!n.active) continue;
            const float level = nodeTrackDbToGain(n.levelDb);
            nodeWeights_[node] = ((1.0f - params_.cursorInfluence) * level
                + params_.cursorInfluence * fieldWeights[node] * fieldScale) * out;
            const uint32_t srcCh = std::min<uint32_t>(n.sourceChannels, kNodeTrackMixerMaxChannels);
            const uint32_t outCh = std::min<uint32_t>(params_.outputChannels, kNodeTrackMixerMaxChannels);
            for (uint32_t sc = 0; sc < srcCh; ++sc) {
                float norm = 0.0f;
                for (uint32_t ch = 0; ch < outCh; ++ch) {
                    auto src = nodeTrackLayoutPoint(sc, srcCh, n.sourceLayout);
                    src = nodeTrackRotatePoint(src, n.rotateAzDeg, n.rotateElDeg);
                    if (params_.mixMode == NodeTrackMixMode::SpatialObjects) {
                        src.x = n.x + src.x * n.scale;
                        src.y = n.y + src.y * n.scale;
                        src.z = n.z + src.z * n.scale;
                    }
                    const auto dst = nodeTrackLayoutPoint(ch, outCh, params_.outputLayout);
                    const float dx = dst.x - src.x;
                    const float dy = dst.y - src.y;
                    const float dz = dst.z - src.z;
                    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                    const float w = 1.0f / std::pow(std::max(0.0001f, dist), std::max(0.2f, n.focus));
                    weights_[node][sc][ch] = w;
                    norm += w * w;
                }
                norm = std::sqrt(std::max(0.0000000001f, norm));
                for (uint32_t ch = 0; ch < outCh; ++ch) {
                    weights_[node][sc][ch] = weights_[node][sc][ch] / norm * nodeWeights_[node];
                }
            }
        }
    }

    double sampleRate_ = 48000.0;
    NodeTrackMixerParams params_ {};
    std::array<float, kNodeTrackMixerMaxNodes> nodeWeights_ {};
    std::array<std::array<std::array<float, kNodeTrackMixerMaxChannels>, kNodeTrackMixerMaxChannels>, kNodeTrackMixerMaxNodes> weights_ {};
};

} // namespace s3g
