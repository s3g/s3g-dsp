#pragma once

#include "s3g_3oafx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace s3g {

// Clean-room implementation of the public AllRAD construction described by
// F. Zotter and M. Frank, "All-Round Ambisonic Panning and Decoding",
// J. Audio Eng. Soc. 60(10), 2012. No third-party implementation, layout
// table, or UI code is used here.
constexpr uint32_t kAllRadMaxRealSpeakers = 64;
constexpr uint32_t kAllRadMaxSupports = 24;
constexpr uint32_t kAllRadMaxNodes = kAllRadMaxRealSpeakers + kAllRadMaxSupports;
constexpr uint32_t kAllRadMaxEdges = 512;
constexpr uint32_t kAllRadMaxFacets = 512;
constexpr uint32_t kAllRadMaxFoldTargets = 16;

enum class AllRadNodeKind : uint8_t {
    Real = 0,
    SupportDrop = 1,
    SupportFold = 2,
};

enum class AllRadDimension : uint8_t {
    Invalid = 0,
    Ring2D = 1,
    Sphere3D = 2,
};

struct AllRadNode {
    Vec3 direction {};
    uint32_t speakerIndex = std::numeric_limits<uint32_t>::max();
    AllRadNodeKind kind = AllRadNodeKind::Real;
    uint32_t foldTargetCount = 0;
    std::array<uint32_t, kAllRadMaxFoldTargets> foldTargets {};
};

struct AllRadEdge {
    uint16_t a = 0;
    uint16_t b = 0;
};

struct AllRadFacet {
    uint16_t a = 0;
    uint16_t b = 0;
    uint16_t c = 0;
};

struct AllRadTopology {
    bool valid = false;
    AllRadDimension dimension = AllRadDimension::Invalid;
    uint32_t nodeCount = 0;
    uint32_t edgeCount = 0;
    uint32_t facetCount = 0;
    uint32_t realNodeCount = 0;
    uint32_t supportCount = 0;
    uint32_t dropSupportCount = 0;
    uint32_t foldSupportCount = 0;
    uint32_t missedVirtualDirections = 0;
    float maxGapDeg = 0.0f;
    std::array<AllRadNode, kAllRadMaxNodes> nodes {};
    std::array<AllRadEdge, kAllRadMaxEdges> edges {};
    std::array<AllRadFacet, kAllRadMaxFacets> facets {};
};

struct AllRadInputSpeaker {
    Vec3 direction {};
    uint32_t speakerIndex = 0;
    bool enabled = true;
};

namespace allrad_detail {

constexpr float kGeometryEpsilon = 0.000001f;

inline Vec3 add(Vec3 a, Vec3 b)
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

inline Vec3 subtract(Vec3 a, Vec3 b)
{
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

inline Vec3 multiply(Vec3 v, float scalar)
{
    return { v.x * scalar, v.y * scalar, v.z * scalar };
}

inline float dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(Vec3 a, Vec3 b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline float lengthSquared(Vec3 v)
{
    return dot(v, v);
}

inline float length(Vec3 v)
{
    return std::sqrt(std::max(0.0f, lengthSquared(v)));
}

inline Vec3 normalizedOr(Vec3 v, Vec3 fallback)
{
    const float magnitude = length(v);
    return magnitude > 0.000001f ? multiply(v, 1.0f / magnitude) : fallback;
}

inline float positiveAzimuth(Vec3 direction)
{
    float angle = std::atan2(direction.y, direction.x);
    if (angle < 0.0f) angle += 2.0f * kPi;
    return angle;
}

inline bool nearlySameDirection(Vec3 a, Vec3 b)
{
    return dot(normalize(a), normalize(b)) > 0.99998f;
}

inline void addEdge(AllRadTopology& topology, uint32_t a, uint32_t b)
{
    if (a == b || a >= topology.nodeCount || b >= topology.nodeCount
        || topology.edgeCount >= topology.edges.size()) {
        return;
    }
    const uint16_t lo = static_cast<uint16_t>(std::min(a, b));
    const uint16_t hi = static_cast<uint16_t>(std::max(a, b));
    for (uint32_t i = 0; i < topology.edgeCount; ++i) {
        if (topology.edges[i].a == lo && topology.edges[i].b == hi) return;
    }
    topology.edges[topology.edgeCount++] = { lo, hi };
}

inline void addFacet(AllRadTopology& topology, uint32_t a, uint32_t b, uint32_t c)
{
    if (a == b || a == c || b == c
        || a >= topology.nodeCount || b >= topology.nodeCount || c >= topology.nodeCount
        || topology.facetCount >= topology.facets.size()) {
        return;
    }
    std::array<uint16_t, 3> ids {
        static_cast<uint16_t>(a),
        static_cast<uint16_t>(b),
        static_cast<uint16_t>(c)
    };
    std::sort(ids.begin(), ids.end());
    for (uint32_t i = 0; i < topology.facetCount; ++i) {
        const auto& facet = topology.facets[i];
        if (facet.a == ids[0] && facet.b == ids[1] && facet.c == ids[2]) return;
    }
    topology.facets[topology.facetCount++] = { ids[0], ids[1], ids[2] };
    addEdge(topology, a, b);
    addEdge(topology, b, c);
    addEdge(topology, c, a);
}

inline bool appendNode(AllRadTopology& topology, const AllRadNode& node)
{
    if (topology.nodeCount >= topology.nodes.size()) return false;
    for (uint32_t i = 0; i < topology.nodeCount; ++i) {
        if (nearlySameDirection(topology.nodes[i].direction, node.direction)) return false;
    }
    topology.nodes[topology.nodeCount++] = node;
    if (node.kind == AllRadNodeKind::Real) {
        ++topology.realNodeCount;
    } else {
        ++topology.supportCount;
        if (node.kind == AllRadNodeKind::SupportDrop) ++topology.dropSupportCount;
        if (node.kind == AllRadNodeKind::SupportFold) ++topology.foldSupportCount;
    }
    return true;
}

inline bool horizontalRing(const AllRadTopology& topology)
{
    if (topology.realNodeCount < 2u) return false;
    for (uint32_t i = 0; i < topology.nodeCount; ++i) {
        if (topology.nodes[i].kind != AllRadNodeKind::Real) continue;
        if (std::fabs(topology.nodes[i].direction.z) > 0.035f) return false;
    }
    return true;
}

inline void sortedRingIds(const AllRadTopology& topology,
    std::array<uint16_t, kAllRadMaxNodes>& ids, uint32_t& count)
{
    count = topology.nodeCount;
    for (uint32_t i = 0; i < count; ++i) ids[i] = static_cast<uint16_t>(i);
    std::sort(ids.begin(), ids.begin() + static_cast<std::ptrdiff_t>(count),
        [&](uint16_t a, uint16_t b) {
            return positiveAzimuth(topology.nodes[a].direction)
                < positiveAzimuth(topology.nodes[b].direction);
        });
}

inline void buildRingTopology(AllRadTopology& topology)
{
    topology.dimension = AllRadDimension::Ring2D;
    while (topology.supportCount < kAllRadMaxSupports) {
        std::array<uint16_t, kAllRadMaxNodes> ids {};
        uint32_t count = 0;
        sortedRingIds(topology, ids, count);
        if (count < 2u) break;

        float widestGap = -1.0f;
        float gapStart = 0.0f;
        for (uint32_t i = 0; i < count; ++i) {
            const float a = positiveAzimuth(topology.nodes[ids[i]].direction);
            float b = positiveAzimuth(topology.nodes[ids[(i + 1u) % count]].direction);
            if (i + 1u == count) b += 2.0f * kPi;
            const float gap = b - a;
            if (gap > widestGap) {
                widestGap = gap;
                gapStart = a;
            }
        }
        topology.maxGapDeg = widestGap * 180.0f / kPi;
        if (widestGap < kPi - 0.001f) break;
        const float angle = gapStart + widestGap * 0.5f;
        AllRadNode support {};
        support.direction = { std::cos(angle), std::sin(angle), 0.0f };
        support.kind = AllRadNodeKind::SupportDrop;
        if (!appendNode(topology, support)) break;
    }

    std::array<uint16_t, kAllRadMaxNodes> ids {};
    uint32_t count = 0;
    sortedRingIds(topology, ids, count);
    if (count < 2u) return;
    for (uint32_t i = 0; i < count; ++i) {
        addEdge(topology, ids[i], ids[(i + 1u) % count]);
    }
    topology.valid = topology.edgeCount >= 2u && topology.maxGapDeg < 180.1f;
}

struct SupportingPlane {
    bool found = false;
    Vec3 normal {};
    float distance = std::numeric_limits<float>::max();
};

inline SupportingPlane leastOriginDistancePlane(const AllRadTopology& topology, uint32_t count)
{
    SupportingPlane result {};
    for (uint32_t a = 0; a < count; ++a) {
        for (uint32_t b = a + 1u; b < count; ++b) {
            for (uint32_t c = b + 1u; c < count; ++c) {
                const Vec3 pa = topology.nodes[a].direction;
                const Vec3 pb = topology.nodes[b].direction;
                const Vec3 pc = topology.nodes[c].direction;
                Vec3 normal = cross(subtract(pb, pa), subtract(pc, pa));
                const float magnitude = length(normal);
                if (magnitude < kGeometryEpsilon) continue;
                normal = multiply(normal, 1.0f / magnitude);

                float maxSide = -std::numeric_limits<float>::max();
                float minSide = std::numeric_limits<float>::max();
                for (uint32_t i = 0; i < count; ++i) {
                    const float side = dot(normal, subtract(topology.nodes[i].direction, pa));
                    maxSide = std::max(maxSide, side);
                    minSide = std::min(minSide, side);
                }
                if (maxSide > kGeometryEpsilon && minSide < -kGeometryEpsilon) continue;
                if (minSide >= -kGeometryEpsilon && maxSide > kGeometryEpsilon) {
                    normal = multiply(normal, -1.0f);
                } else if (maxSide <= kGeometryEpsilon && minSide >= -kGeometryEpsilon) {
                    if (dot(normal, pa) < 0.0f) normal = multiply(normal, -1.0f);
                }
                const float distance = dot(normal, pa);
                if (!result.found || distance < result.distance) {
                    result.found = true;
                    result.normal = normal;
                    result.distance = distance;
                }
            }
        }
    }
    return result;
}

struct PlaneGroup {
    Vec3 normal {};
    float distance = 0.0f;
    uint32_t vertexCount = 0;
    std::array<uint16_t, kAllRadMaxNodes> vertices {};
};

inline bool planeVertexSetContains(const PlaneGroup& superset, const PlaneGroup& subset)
{
    if (superset.vertexCount < subset.vertexCount) return false;
    uint32_t supersetIndex = 0;
    uint32_t subsetIndex = 0;
    while (supersetIndex < superset.vertexCount && subsetIndex < subset.vertexCount) {
        const uint16_t supersetVertex = superset.vertices[supersetIndex];
        const uint16_t subsetVertex = subset.vertices[subsetIndex];
        if (supersetVertex < subsetVertex) {
            ++supersetIndex;
        } else if (supersetVertex == subsetVertex) {
            ++supersetIndex;
            ++subsetIndex;
        } else {
            return false;
        }
    }
    return subsetIndex == subset.vertexCount;
}

inline uint32_t collectHullPlanes(const AllRadTopology& topology, uint32_t nodeCount,
    std::array<PlaneGroup, kAllRadMaxFacets>& groups, bool& truncated)
{
    uint32_t groupCount = 0;
    truncated = false;
    for (uint32_t a = 0; a < nodeCount; ++a) {
        for (uint32_t b = a + 1u; b < nodeCount; ++b) {
            for (uint32_t c = b + 1u; c < nodeCount; ++c) {
                const Vec3 pa = topology.nodes[a].direction;
                const Vec3 pb = topology.nodes[b].direction;
                const Vec3 pc = topology.nodes[c].direction;
                Vec3 normal = cross(subtract(pb, pa), subtract(pc, pa));
                const float magnitude = length(normal);
                if (magnitude < kGeometryEpsilon) continue;
                normal = multiply(normal, 1.0f / magnitude);

                float maxSide = -std::numeric_limits<float>::max();
                float minSide = std::numeric_limits<float>::max();
                for (uint32_t i = 0; i < nodeCount; ++i) {
                    const float side = dot(normal, subtract(topology.nodes[i].direction, pa));
                    maxSide = std::max(maxSide, side);
                    minSide = std::min(minSide, side);
                }
                if (maxSide > kGeometryEpsilon && minSide < -kGeometryEpsilon) continue;
                if (minSide >= -kGeometryEpsilon && maxSide > kGeometryEpsilon) {
                    normal = multiply(normal, -1.0f);
                } else if (maxSide <= kGeometryEpsilon && minSide >= -kGeometryEpsilon) {
                    if (dot(normal, pa) < 0.0f) normal = multiply(normal, -1.0f);
                }
                const float distance = dot(normal, pa);
                if (distance < -kGeometryEpsilon) continue;

                PlaneGroup candidate {};
                candidate.normal = normal;
                candidate.distance = distance;
                candidate.vertices[candidate.vertexCount++] = static_cast<uint16_t>(a);
                candidate.vertices[candidate.vertexCount++] = static_cast<uint16_t>(b);
                candidate.vertices[candidate.vertexCount++] = static_cast<uint16_t>(c);
                for (uint32_t i = 0; i < nodeCount; ++i) {
                    if (i == a || i == b || i == c) continue;
                    if (std::fabs(dot(normal, topology.nodes[i].direction) - distance) < 0.000001f
                        && candidate.vertexCount < candidate.vertices.size()) {
                        candidate.vertices[candidate.vertexCount++] = static_cast<uint16_t>(i);
                    }
                }
                std::sort(candidate.vertices.begin(),
                    candidate.vertices.begin() + static_cast<std::ptrdiff_t>(candidate.vertexCount));

                // Reconstructed AED coordinates can make different triples from
                // one nominally planar face yield slightly different plane
                // equations. The maximal coplanar vertex set is the stable face
                // identity; plane-normal thresholds can split that one face into
                // overlapping triangulations.
                bool containedByExisting = false;
                for (uint32_t g = 0; g < groupCount; ++g) {
                    if (planeVertexSetContains(groups[g], candidate)) {
                        containedByExisting = true;
                        break;
                    }
                }
                if (containedByExisting) continue;

                uint32_t retainedCount = 0;
                for (uint32_t g = 0; g < groupCount; ++g) {
                    if (planeVertexSetContains(candidate, groups[g])) continue;
                    if (retainedCount != g) groups[retainedCount] = groups[g];
                    ++retainedCount;
                }
                groupCount = retainedCount;
                if (groupCount >= groups.size()) {
                    truncated = true;
                    continue;
                }
                groups[groupCount++] = candidate;
            }
        }
    }
    return groupCount;
}

inline void sortFaceVertices(const AllRadTopology& topology, PlaneGroup& group)
{
    Vec3 center {};
    for (uint32_t i = 0; i < group.vertexCount; ++i) {
        center = add(center, topology.nodes[group.vertices[i]].direction);
    }
    center = normalizedOr(center, group.normal);
    Vec3 reference = subtract(topology.nodes[group.vertices[0]].direction,
        multiply(group.normal, dot(group.normal, topology.nodes[group.vertices[0]].direction)));
    reference = normalizedOr(reference,
        std::fabs(group.normal.z) < 0.9f
            ? normalizedOr(cross({ 0.0f, 0.0f, 1.0f }, group.normal), { 1.0f, 0.0f, 0.0f })
            : normalizedOr(cross({ 0.0f, 1.0f, 0.0f }, group.normal), { 1.0f, 0.0f, 0.0f }));
    const Vec3 tangent = normalizedOr(cross(group.normal, reference), { 0.0f, 1.0f, 0.0f });
    std::sort(group.vertices.begin(),
        group.vertices.begin() + static_cast<std::ptrdiff_t>(group.vertexCount),
        [&](uint16_t a, uint16_t b) {
            const Vec3 va = subtract(topology.nodes[a].direction, center);
            const Vec3 vb = subtract(topology.nodes[b].direction, center);
            return std::atan2(dot(va, tangent), dot(va, reference))
                < std::atan2(dot(vb, tangent), dot(vb, reference));
        });
}

inline bool validClosedSphereTopology(const AllRadTopology& topology)
{
    if (topology.nodeCount < 4u
        || topology.facetCount == 0u
        || topology.facetCount >= topology.facets.size()
        || topology.edgeCount == 0u
        || topology.edgeCount >= topology.edges.size()
        || topology.facetCount * 3u != topology.edgeCount * 2u) {
        return false;
    }

    std::array<bool, kAllRadMaxNodes> nodeUsed {};
    for (uint32_t facetIndex = 0; facetIndex < topology.facetCount; ++facetIndex) {
        const auto& facet = topology.facets[facetIndex];
        if (facet.a >= topology.nodeCount || facet.b >= topology.nodeCount
            || facet.c >= topology.nodeCount
            || facet.a == facet.b || facet.a == facet.c || facet.b == facet.c) {
            return false;
        }
        const Vec3 a = topology.nodes[facet.a].direction;
        const Vec3 b = topology.nodes[facet.b].direction;
        const Vec3 c = topology.nodes[facet.c].direction;
        if (std::fabs(dot(a, cross(b, c))) < kGeometryEpsilon) return false;
        nodeUsed[facet.a] = true;
        nodeUsed[facet.b] = true;
        nodeUsed[facet.c] = true;
    }

    for (uint32_t edgeIndex = 0; edgeIndex < topology.edgeCount; ++edgeIndex) {
        const auto& edge = topology.edges[edgeIndex];
        if (edge.a >= topology.nodeCount || edge.b >= topology.nodeCount
            || edge.a == edge.b) {
            return false;
        }
        uint32_t incidence = 0;
        for (uint32_t facetIndex = 0; facetIndex < topology.facetCount; ++facetIndex) {
            const auto& facet = topology.facets[facetIndex];
            const bool hasA = facet.a == edge.a || facet.b == edge.a || facet.c == edge.a;
            const bool hasB = facet.a == edge.b || facet.b == edge.b || facet.c == edge.b;
            if (hasA && hasB) ++incidence;
        }
        if (incidence != 2u) return false;
    }
    for (uint32_t nodeIndex = 0; nodeIndex < topology.nodeCount; ++nodeIndex) {
        if (!nodeUsed[nodeIndex]) return false;
    }

    const int64_t eulerCharacteristic =
        static_cast<int64_t>(topology.nodeCount)
        - static_cast<int64_t>(topology.edgeCount)
        + static_cast<int64_t>(topology.facetCount);
    return eulerCharacteristic == 2;
}

inline void buildSphereTopology(AllRadTopology& topology)
{
    topology.dimension = AllRadDimension::Sphere3D;
    if (topology.realNodeCount < 3u) return;

    bool closed = false;
    while (topology.supportCount < kAllRadMaxSupports) {
        const auto plane = leastOriginDistancePlane(topology, topology.nodeCount);
        if (!plane.found) break;
        if (plane.distance > 0.00010f && topology.nodeCount >= 4u) {
            closed = true;
            break;
        }
        AllRadNode support {};
        support.direction = normalizedOr(plane.normal, { 0.0f, 0.0f, -1.0f });
        support.kind = AllRadNodeKind::SupportDrop;
        if (!appendNode(topology, support)) break;
    }
    if (!closed) {
        const auto plane = leastOriginDistancePlane(topology, topology.nodeCount);
        closed = plane.found && plane.distance > 0.00010f && topology.nodeCount >= 4u;
    }
    if (!closed) return;

    const uint32_t hullNodeCount = topology.nodeCount;
    // Plane groups are the largest construction scratch block. Keep one bounded
    // block per thread instead of placing it on the (potentially small) audio
    // thread stack. It is cleared before every use and buildAllRadTopology is
    // otherwise allocation-free.
    static thread_local std::array<PlaneGroup, kAllRadMaxFacets> groups {};
    groups.fill(PlaneGroup {});
    bool groupsTruncated = false;
    const uint32_t groupCount =
        collectHullPlanes(topology, hullNodeCount, groups, groupsTruncated);
    if (groupsTruncated) return;
    for (uint32_t g = 0; g < groupCount; ++g) {
        auto& group = groups[g];
        if (group.vertexCount < 3u) continue;
        sortFaceVertices(topology, group);
        bool allReal = true;
        for (uint32_t i = 0; i < group.vertexCount; ++i) {
            allReal = allReal
                && topology.nodes[group.vertices[i]].kind == AllRadNodeKind::Real;
        }

        const bool canFold = allReal
            && group.vertexCount > 3u
            && group.vertexCount <= kAllRadMaxFoldTargets
            && topology.supportCount < kAllRadMaxSupports;
        if (canFold) {
            AllRadNode support {};
            Vec3 center {};
            for (uint32_t i = 0; i < group.vertexCount; ++i) {
                center = add(center, topology.nodes[group.vertices[i]].direction);
                support.foldTargets[i] = topology.nodes[group.vertices[i]].speakerIndex;
            }
            support.direction = normalizedOr(center, group.normal);
            support.kind = AllRadNodeKind::SupportFold;
            support.foldTargetCount = group.vertexCount;
            const uint32_t supportIndex = topology.nodeCount;
            if (appendNode(topology, support)) {
                for (uint32_t i = 0; i < group.vertexCount; ++i) {
                    addFacet(topology, supportIndex, group.vertices[i],
                        group.vertices[(i + 1u) % group.vertexCount]);
                }
                continue;
            }
        }

        for (uint32_t i = 1u; i + 1u < group.vertexCount; ++i) {
            addFacet(topology, group.vertices[0], group.vertices[i], group.vertices[i + 1u]);
        }
    }
    topology.valid = validClosedSphereTopology(topology);
}

inline float determinant(Vec3 a, Vec3 b, Vec3 c)
{
    return dot(a, cross(b, c));
}

} // namespace allrad_detail

inline AllRadTopology buildAllRadTopology(
    const std::array<AllRadInputSpeaker, kAllRadMaxRealSpeakers>& inputs,
    uint32_t inputCount)
{
    AllRadTopology topology {};
    inputCount = std::min<uint32_t>(inputCount, inputs.size());
    for (uint32_t i = 0; i < inputCount; ++i) {
        if (!inputs[i].enabled) continue;
        const Vec3 inputDirection = inputs[i].direction;
        if (inputs[i].speakerIndex >= kAllRadMaxRealSpeakers
            || !std::isfinite(inputDirection.x)
            || !std::isfinite(inputDirection.y)
            || !std::isfinite(inputDirection.z)
            || allrad_detail::lengthSquared(inputDirection)
                < allrad_detail::kGeometryEpsilon
                    * allrad_detail::kGeometryEpsilon) {
            topology.dimension = AllRadDimension::Invalid;
            return topology;
        }
        AllRadNode node {};
        node.direction = normalize(inputDirection);
        node.speakerIndex = inputs[i].speakerIndex;
        node.kind = AllRadNodeKind::Real;
        if (!allrad_detail::appendNode(topology, node)) {
            topology.valid = false;
            topology.dimension = AllRadDimension::Invalid;
            return topology;
        }
    }
    if (allrad_detail::horizontalRing(topology)) {
        allrad_detail::buildRingTopology(topology);
    } else {
        allrad_detail::buildSphereTopology(topology);
    }
    return topology;
}

inline bool solveAllRadVbap(const AllRadTopology& topology, Vec3 direction,
    std::array<float, kAllRadMaxRealSpeakers>& realGains,
    float* droppedEnergy = nullptr)
{
    using namespace allrad_detail;
    realGains.fill(0.0f);
    if (droppedEnergy) *droppedEnergy = 0.0f;
    if (!topology.valid || topology.nodeCount == 0u) return false;
    direction = normalize(direction);

    std::array<float, kAllRadMaxNodes> nodeGains {};
    bool found = false;
    float bestScore = -std::numeric_limits<float>::max();
    if (topology.dimension == AllRadDimension::Ring2D) {
        const float horizontal = std::sqrt(direction.x * direction.x + direction.y * direction.y);
        if (horizontal < 0.000001f) return false;
        direction = { direction.x / horizontal, direction.y / horizontal, 0.0f };
        for (uint32_t edgeIndex = 0; edgeIndex < topology.edgeCount; ++edgeIndex) {
            const uint32_t a = topology.edges[edgeIndex].a;
            const uint32_t b = topology.edges[edgeIndex].b;
            const Vec3 va = topology.nodes[a].direction;
            const Vec3 vb = topology.nodes[b].direction;
            const float det = va.x * vb.y - vb.x * va.y;
            if (std::fabs(det) < 0.000001f) continue;
            const float ga = (direction.x * vb.y - vb.x * direction.y) / det;
            const float gb = (va.x * direction.y - direction.x * va.y) / det;
            if (ga < -0.00010f || gb < -0.00010f) continue;
            const float score = std::min(ga, gb);
            if (!found || score > bestScore) {
                nodeGains.fill(0.0f);
                nodeGains[a] = std::max(0.0f, ga);
                nodeGains[b] = std::max(0.0f, gb);
                bestScore = score;
                found = true;
            }
        }
    } else if (topology.dimension == AllRadDimension::Sphere3D) {
        for (uint32_t facetIndex = 0; facetIndex < topology.facetCount; ++facetIndex) {
            const auto& facet = topology.facets[facetIndex];
            const Vec3 va = topology.nodes[facet.a].direction;
            const Vec3 vb = topology.nodes[facet.b].direction;
            const Vec3 vc = topology.nodes[facet.c].direction;
            const float det = determinant(va, vb, vc);
            if (std::fabs(det) < 0.000001f) continue;
            const float ga = determinant(direction, vb, vc) / det;
            const float gb = determinant(va, direction, vc) / det;
            const float gc = determinant(va, vb, direction) / det;
            if (ga < -0.00010f || gb < -0.00010f || gc < -0.00010f) continue;
            const float score = std::min({ ga, gb, gc });
            if (!found || score > bestScore) {
                nodeGains.fill(0.0f);
                nodeGains[facet.a] = std::max(0.0f, ga);
                nodeGains[facet.b] = std::max(0.0f, gb);
                nodeGains[facet.c] = std::max(0.0f, gc);
                bestScore = score;
                found = true;
            }
        }
    }
    if (!found) return false;

    float nodeEnergy = 0.0f;
    for (uint32_t i = 0; i < topology.nodeCount; ++i) {
        nodeEnergy += nodeGains[i] * nodeGains[i];
    }
    if (nodeEnergy <= 0.0000001f) return false;
    const float inverseNodeNorm = 1.0f / std::sqrt(nodeEnergy);
    bool usedFold = false;
    for (uint32_t i = 0; i < topology.nodeCount; ++i) {
        const float gain = nodeGains[i] * inverseNodeNorm;
        const auto& node = topology.nodes[i];
        if (node.kind == AllRadNodeKind::Real) {
            if (node.speakerIndex < realGains.size()) realGains[node.speakerIndex] += gain;
        } else if (node.kind == AllRadNodeKind::SupportDrop) {
            if (droppedEnergy) *droppedEnergy += gain * gain;
        } else if (node.foldTargetCount > 0u) {
            usedFold = true;
            const float foldGain = gain / std::sqrt(static_cast<float>(node.foldTargetCount));
            for (uint32_t target = 0; target < node.foldTargetCount; ++target) {
                if (node.foldTargets[target] < realGains.size()) {
                    realGains[node.foldTargets[target]] += foldGain;
                }
            }
        }
    }

    if (usedFold) {
        float energy = 0.0f;
        for (float gain : realGains) energy += gain * gain;
        if (energy > 0.0000001f) {
            const float inverseNorm = 1.0f / std::sqrt(energy);
            for (float& gain : realGains) gain *= inverseNorm;
        }
    }
    return true;
}

} // namespace s3g
